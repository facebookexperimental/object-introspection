/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "OICache.h"

#include <glog/logging.h>

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <fstream>

#include "Descs.h"
#include "OICodeGen.h"
#include "Serialize.h"

#ifndef OSS_ENABLE
#include "cea/object-introspection/internal/ManifoldCache.h"
#endif

static std::optional<std::reference_wrapper<const std::string>> getEntName(
    SymbolService &symbols, const irequest &req, OICache::Entity ent) {
  if (ent == OICache::Entity::FuncDescs ||
      ent == OICache::Entity::GlobalDescs) {
    return req.func;
  } else {
    if (req.type == "global") {
      const auto &globalDesc = symbols.findGlobalDesc(req.func);
      if (!globalDesc) {
        return std::nullopt;
      }

      return globalDesc->typeName;
    } else {
      const auto &funcDesc = symbols.findFuncDesc(req);
      if (!funcDesc) {
        return std::nullopt;
      }

      const auto &arg = funcDesc->getArgument(req.arg);
      if (!arg) {
        return std::nullopt;
      }

      return arg->typeName;
    }
  }
}

std::optional<fs::path> OICache::getPath(const irequest &req,
                                         Entity ent) const {
  auto hash = [](const std::string &str) {
    return std::to_string(std::hash<std::string>{}(str));
  };

  auto ext = extensions[static_cast<size_t>(ent)];

  const auto &entName = getEntName(*symbols, req, ent);
  if (!entName.has_value()) {
    return std::nullopt;
  }

  return basePath / (hash(*entName) + ext);
}

template <typename T>
bool OICache::load(const irequest &req, Entity ent, T &data) {
  if (!isEnabled())
    return false;
  try {
    auto buildID = symbols->locateBuildID();
    if (!buildID) {
      LOG(ERROR) << "Failed to locate buildID";
      return false;
    }

    auto cachePath = getPath(req, ent);
    if (!cachePath.has_value()) {
      LOG(ERROR) << "Failed to get cache path for " << req.type << ':'
                 << req.func << ':' << req.arg << '/'
                 << static_cast<size_t>(ent);
      return false;
    }

    LOG(INFO) << "Loading cache " << *cachePath;
    std::ifstream ifs(*cachePath);
    boost::archive::text_iarchive ia(ifs);

    std::string cacheBuildId;
    ia >> cacheBuildId;
    if (cacheBuildId != *buildID) {
      LOG(ERROR) << "The cache's build id '" << cacheBuildId
                 << "' doesn't match the target's build id '" << *buildID
                 << "'";
      return false;
    }

    ia >> data;
    return true;
  } catch (const std::exception &e) {
    LOG(WARNING) << "Failed to load from cache: " << e.what();
    return false;
  }
}

template <typename T>
bool OICache::store(const irequest &req, Entity ent, const T &data) {
  if (!isEnabled())
    return false;
  try {
    auto buildID = symbols->locateBuildID();
    if (!buildID) {
      LOG(ERROR) << "Failed to locate buildID";
      return false;
    }

    auto cachePath = getPath(req, ent);
    if (!cachePath.has_value()) {
      LOG(ERROR) << "Failed to get cache path for " << req.type << ':'
                 << req.func << ':' << req.arg << '/'
                 << static_cast<size_t>(ent);
      return false;
    }

    LOG(INFO) << "Storing cache " << *cachePath;
    std::ofstream ofs(*cachePath);
    boost::archive::text_oarchive oa(ofs);

    oa << *buildID;
    oa << data;
    return true;
  } catch (const std::exception &e) {
    LOG(WARNING) << "Failed to write to cache: " << e.what();
    return false;
  }
}

#define INSTANTIATE_ARCHIVE(...)                                        \
  template bool OICache::load(const irequest &, Entity, __VA_ARGS__ &); \
  template bool OICache::store(const irequest &, Entity, const __VA_ARGS__ &);

INSTANTIATE_ARCHIVE(std::pair<RootInfo, TypeHierarchy>)
INSTANTIATE_ARCHIVE(std::unordered_map<std::string, std::shared_ptr<FuncDesc>>)
INSTANTIATE_ARCHIVE(
    std::unordered_map<std::string, std::shared_ptr<GlobalDesc>>)
INSTANTIATE_ARCHIVE(std::map<std::string, PaddingInfo>)

#undef INSTANTIATE_ARCHIVE

// Upload all contents of cache for this request
bool OICache::upload([[maybe_unused]] const irequest &req) {
#ifndef OSS_ENABLE
  if (!isEnabled() || downloadedRemote || !enableUpload)
    return true;
  std::vector<std::filesystem::path> files;

  for (size_t i = 0; i < static_cast<size_t>(OICache::Entity::MAX); i++) {
    auto cachePath = getPath(req, static_cast<OICache::Entity>(i));
    if (!cachePath.has_value()) {
      LOG(ERROR) << "Failed to get cache path for " << req.type << ':'
                 << req.func << ':' << req.arg << '/' << static_cast<size_t>(i);
      return false;
    }
    files.push_back(*cachePath);
  }

  auto hash = generateRemoteHash(req);
  if (hash.empty()) {
    LOG(ERROR) << "failed to generate remote lookup hash";
    return false;
  }

  return ObjectIntrospection::ManifoldCache::upload(hash, files);
#else
  if (isEnabled() && !downloadedRemote && enableUpload) {
    // We tried to download when support is not enabled!
    LOG(ERROR) << "Tried to upload artifacts when support is not enabled!";
    return false;
  }
  return true;
#endif
}

// Try to fetch contents of cache
bool OICache::download([[maybe_unused]] const irequest &req) {
#ifndef OSS_ENABLE
  if (!isEnabled() || !enableDownload)
    return true;

  auto hash = generateRemoteHash(req);
  if (hash.empty()) {
    LOG(ERROR) << "failed to generate remote lookup hash";
    return false;
  }

  if (basePath.filename() != hash) {
    // Use a subdirectory per hash shard to avoid conflicts
    basePath /= hash;
    if (fs::exists(basePath.parent_path()) && !fs::exists(basePath))
      fs::create_directory(basePath);
  }
  if (ObjectIntrospection::ManifoldCache::download(hash, basePath)) {
    downloadedRemote = true;
    return true;
  }
  if (abortOnLoadFail) {
    LOG(ERROR) << "We weren't able to pull artifacts when requested, "
                  "aborting run!";
    // If we aren't uploading, quit early as we requested a download and
    // weren't able to get it.
    return false;
  }
  return true;
#else
  if (isEnabled() && enableDownload) {
    // We tried to download when support is not enabled!
    LOG(ERROR) << "Tried to download artifacts when support is not enabled!";
    return false;
  }
  return true;
#endif
}

std::string OICache::generateRemoteHash(const irequest &req) {
  auto buildID = symbols->locateBuildID();
  if (!buildID) {
    LOG(ERROR) << "Failed to locate buildID";
    return "";
  }

  std::string remote_cache_id = *buildID + "/" + req.func + "/" + req.arg +
                                "/" + generatorConfig.toString();

  LOG(INFO) << "generating remote hash from: " << remote_cache_id;
  return std::to_string(std::hash<std::string>{}(remote_cache_id));
}
