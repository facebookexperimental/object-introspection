#
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
import sys

import toml


def main():
    if len(sys.argv) < 2:
        print("usage: combine_configs.py OUTPUT_PATH INPUT_PATHS...")
        return

    out = {
        "types": {
            "containers": [],
        },
        "headers": {
            "user_paths": [],
            "system_paths": [],
        },
        "codegen": {
            "default_headers": set(),
            "default_namespaces": set(),
        },
    }
    for cfg_path in sys.argv[2:]:
        cfg = toml.load(cfg_path)

        types = cfg.get("types", None)
        if types is not None:
            out["types"]["containers"] += types.get("containers", [])

        headers = cfg.get("headers", None)
        if headers is not None:
            out["headers"]["user_paths"] += headers.get("user_paths", [])
            out["headers"]["system_paths"] += headers.get("system_paths", [])

        codegen = cfg.get("codegen", None)
        if codegen is not None:
            out["codegen"]["default_headers"].update(
                codegen.get("default_headers", set())
            )
            out["codegen"]["default_namespaces"].update(
                codegen.get("default_namespaces", set())
            )

    with open(sys.argv[1], "w") as f:
        toml.dump(out, f)


if __name__ == "__main__":
    main()
