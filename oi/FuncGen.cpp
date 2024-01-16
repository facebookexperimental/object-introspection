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
#include "oi/FuncGen.h"

#include <glog/logging.h>

#include <boost/format.hpp>
#include <map>

#include "oi/ContainerInfo.h"

namespace oi::detail {
namespace {

const std::string typedValueFunc = R"(
  void getSizeType(const %1%& t, size_t& returnArg)
  {
  const uint8_t KindOfPersistentDict = 14;
  const uint8_t KindOfDict = 15;
  const uint8_t KindOfPersistentVec = 22;
  const uint8_t KindOfVec = 23;
  const uint8_t KindOfPersistentKeyset = 26;
  const uint8_t KindOfKeyset = 27;
  const uint8_t KindOfRecord = 29;
  const uint8_t KindOfPersistentString = 38;
  const uint8_t KindOfString = 39;
  const uint8_t KindOfObject = 43;
  const uint8_t KindOfResource = 45;
  const uint8_t KindOfRFunc = 51;
  const uint8_t KindOfRClsMeth = 53;
  const uint8_t KindOfClsMeth = 56;
  const uint8_t KindOfBoolean = 70;
  const uint8_t KindOfInt64 = 74;
  const uint8_t KindOfDouble = 76;
  const uint8_t KindOfFunc = 82;
  const uint8_t KindOfClass = 84;
  const uint8_t KindOfLazyClass = 88;
  const uint8_t KindOfUninit = 98;
  const uint8_t KindOfNull = 100;

  SAVE_DATA((uintptr_t)t.m_type);
   switch(t.m_type) {
     case KindOfInt64:
     case KindOfBoolean:
       SAVE_DATA(0);
       getSizeType(t.m_data.num, returnArg);
       break;

     case KindOfDouble:
       SAVE_DATA(1);
       getSizeType(t.m_data.dbl, returnArg);
       break;

     case KindOfPersistentString:
     case KindOfString:
       SAVE_DATA(2);
       getSizeType(t.m_data.pstr, returnArg);
       break;

     case KindOfPersistentDict:
     case KindOfDict:
     case KindOfPersistentVec:
     case KindOfVec:
     case KindOfPersistentKeyset:
     case KindOfKeyset:
       SAVE_DATA(3);
       getSizeType(t.m_data.parr, returnArg);
       break;

     case KindOfObject:
       SAVE_DATA(4);
       getSizeType(t.m_data.pobj, returnArg);
       break;

     case KindOfResource:
       SAVE_DATA(5);
       getSizeType(t.m_data.pres, returnArg);
       break;

     case KindOfFunc:
       SAVE_DATA(8);
       getSizeType(t.m_data.pfunc, returnArg);
       break;

     case KindOfRFunc:
       SAVE_DATA(9);
       getSizeType(t.m_data.prfunc, returnArg);
       break;

     case KindOfClass:
       SAVE_DATA(10);
       getSizeType(t.m_data.pclass, returnArg);
       break;

     case KindOfClsMeth:
       SAVE_DATA(11);
       getSizeType(t.m_data.pclsmeth, returnArg);
       break;

     case KindOfRClsMeth:
       SAVE_DATA(12);
       getSizeType(t.m_data.prclsmeth, returnArg);
       break;

     case KindOfRecord:
       SAVE_DATA(13);
       getSizeType(t.m_data.prec, returnArg);
       break;

     case KindOfLazyClass:
       SAVE_DATA(14);
       getSizeType(t.m_data.plazyclass, returnArg);
       break;

     case KindOfUninit:
     case KindOfNull:
       break;

   }
  }
  )";

}  // namespace

void FuncGen::DeclareGetSize(std::string& testCode, const std::string& type) {
  boost::format fmt =
      boost::format("void getSizeType(const %1% &t, size_t& returnArg);\n") %
      type;
  testCode.append(fmt.str());
}

void FuncGen::DeclareExterns(std::string& code) {
  constexpr std::string_view vars = R"(
extern uint8_t* dataBase;
extern size_t dataSize;
extern uintptr_t cookieValue;
  )";
  code.append(vars);
}

void FuncGen::DefineJitLog(std::string& code, FeatureSet features) {
  if (features[Feature::JitLogging]) {
    code += R"(
extern int logFile;

void __jlogptr(uintptr_t ptr) {
  static constexpr char hexdigits[] = "0123456789abcdef";
  static constexpr size_t ptrlen = 2 * sizeof(ptr);

  static char hexstr[ptrlen + 1] = {};

  size_t i = ptrlen;
  while (i--) {
    hexstr[i] = hexdigits[ptr & 0xf];
    ptr = ptr >> 4;
  }
  hexstr[ptrlen] = '\n';
  write(logFile, hexstr, sizeof(hexstr));
}

#define JLOG(str)                           \
  do {                                      \
    if (__builtin_expect(logFile, 0)) {     \
      write(logFile, str, sizeof(str) - 1); \
    }                                       \
  } while (false)

#define JLOGPTR(ptr)                    \
  do {                                  \
    if (__builtin_expect(logFile, 0)) { \
      __jlogptr((uintptr_t)ptr);        \
    }                                   \
  } while (false)
)";
  } else {
    code += R"(
#define JLOG(str)
#define JLOGPTR(ptr)
)";
  }
}

void FuncGen::DeclareStoreData(std::string& testCode) {
  testCode.append("void StoreData(uintptr_t data, size_t& dataSegOffset);\n");
}
void FuncGen::DeclareEncodeData(std::string& testCode) {
  testCode.append("size_t EncodeVarint(uint64_t val, uint8_t* buf);\n");
}
void FuncGen::DeclareEncodeDataSize(std::string& testCode) {
  testCode.append("size_t EncodeVarintSize(uint64_t val);\n");
}
void FuncGen::DefineEncodeData(std::string& testCode) {
  std::string func = R"(
      size_t EncodeVarint(uint64_t val, uint8_t* buf) {
        uint8_t* p = buf;
        while (val >= 128) {
          *p++ = 0x80 | (val & 0x7f);
          val >>= 7;
        }
        *p++ = uint8_t(val);
        return size_t(p - buf);
      }
      )";
  testCode.append(func);
}
void FuncGen::DefineEncodeDataSize(std::string& testCode) {
  std::string func = R"(
      size_t EncodeVarintSize(uint64_t val) {
        int s = 1;
        while (val >= 128) {
          ++s;
          val >>= 7;
        }
        return s;
      }
    )";
  testCode.append(func);
}

void FuncGen::DefineStoreData(std::string& testCode) {
  // TODO: We are encoding twice. Once to check the size and later to
  // actually encode. Maybe just do it once leaving a max of uintptr_t
  // space at the end.
  std::string func = R"(
    void StoreData(uint64_t data, size_t& dataSegOffset) {
      size_t sz = EncodeVarintSize(data);
      if (sz + dataSegOffset < dataSize) {
        auto data_base = reinterpret_cast<uint8_t*>(dataBase);
        data_base += dataSegOffset;
        size_t data_size = EncodeVarint(data, data_base);
        dataSegOffset += data_size;
      } else {
        dataSegOffset += sz;
      }
    }
    )";

  testCode.append(func);
}

void FuncGen::DefineTopLevelIntrospect(std::string& code,
                                       const std::string& type) {
  std::string func = R"(
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-attributes"
/* RawType: %1% */
void __attribute__((used, retain)) introspect_%2$016x(
    const OIInternal::__ROOT_TYPE__& t,
    std::vector<uint8_t>& v)
#pragma GCC diagnostic pop
{
  v.clear();
  v.reserve(4096);

  auto pointers = std::make_unique<PointerHashSet<>>();
  pointers->initialize();

  struct Context {
    using DataBuffer = DataBuffer::BackInserter<std::vector<uint8_t>>;

    PointerHashSet<>& pointers;
  };
  Context ctx{ .pointers = *pointers };
  ctx.pointers.add((uintptr_t)&t);

  using ContentType = OIInternal::TypeHandler<Context, OIInternal::__ROOT_TYPE__>::type;

  ContentType ret{Context::DataBuffer{v}};
  OIInternal::getSizeType<Context>(ctx, t, ret);
}
)";

  code.append(
      (boost::format(func) % type % std::hash<std::string>{}(type)).str());
}

void FuncGen::DefineTopLevelIntrospectNamed(std::string& code,
                                            const std::string& type,
                                            const std::string& linkageName) {
  std::string typeHash =
      (boost::format("%1$016x") % std::hash<std::string>{}(type)).str();

  code += "/* RawType: ";
  code += type;
  code += " */\n";
  code += "extern \"C\" IntrospectionResult ";
  code += linkageName;
  code += "(const OIInternal::__ROOT_TYPE__& t) {\n";
  code += "  std::vector<uint8_t> v{};\n";
  code += "  introspect_";
  code += typeHash;
  code += "(t, v);\n";
  code += "  return IntrospectionResult{std::move(v), treeBuilderInstructions";
  code += typeHash;
  code += "};\n";
  code += "}\n";
}

void FuncGen::DefineTopLevelGetSizeRef(std::string& testCode,
                                       const std::string& rawType,
                                       FeatureSet features) {
  std::string func = R"(
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunknown-attributes"
    /* RawType: %1% */
    void __attribute__((used, retain)) getSize_%2$016x(const OIInternal::__ROOT_TYPE__& t)
    #pragma GCC diagnostic pop
    {
    )";
  if (features[Feature::JitTiming]) {
    func += "      const auto startTime = std::chrono::steady_clock::now();\n";
  }
  func += R"(
      ctx.pointers.initialize();
      ctx.pointers.add((uintptr_t)&t);
      auto data = reinterpret_cast<uintptr_t*>(dataBase);

      size_t dataSegOffset = 0;
      data[dataSegOffset++] = oidMagicId;
      data[dataSegOffset++] = cookieValue;
      uintptr_t& writtenSize = data[dataSegOffset++];
      writtenSize = 0;
      uintptr_t& timeTakenNs = data[dataSegOffset++];
      size_t& pointersSize = data[dataSegOffset++];
      size_t& pointersCapacity = data[dataSegOffset++];

      dataSegOffset *= sizeof(uintptr_t);
      JLOG("%1% @");
      JLOGPTR(&t);
      OIInternal::getSizeType(t, dataSegOffset);
      OIInternal::StoreData((uintptr_t)123456789, dataSegOffset);
      OIInternal::StoreData((uintptr_t)123456789, dataSegOffset);
      writtenSize = dataSegOffset;
      dataBase += dataSegOffset;
      pointersSize = ctx.pointers.size();
      pointersCapacity = ctx.pointers.capacity();
    )";
  if (features[Feature::JitTiming]) {
    func += R"(
      timeTakenNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - startTime).count();
      )";
  }
  func += R"(
    }
    )";

  boost::format fmt =
      boost::format(func) % rawType % std::hash<std::string>{}(rawType);
  testCode.append(fmt.str());
}

void FuncGen::DefineTreeBuilderInstructions(
    std::string& code,
    const std::string& rawType,
    size_t exclusiveSize,
    std::span<const std::string_view> typeNames) {
  std::string typeHash =
      (boost::format("%1$016x") % std::hash<std::string>{}(rawType)).str();

  code += R"(
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-attributes"
namespace {
struct FakeContext {
  using DataBuffer = int;
};
const std::array<std::string_view, )";
  code += std::to_string(typeNames.size());
  code += "> typeNames";
  code += typeHash;
  code += '{';
  for (const auto& name : typeNames) {
    code += '"';
    code += name;
    code += "\",";
  }
  code += "};\n";
  code += "const exporters::inst::Field rootInstructions";
  code += typeHash;
  code += "{sizeof(OIInternal::__ROOT_TYPE__), ";
  code += std::to_string(exclusiveSize);
  code += ", \"a0\", typeNames";
  code += typeHash;
  code +=
      ", OIInternal::TypeHandler<FakeContext, "
      "OIInternal::__ROOT_TYPE__>::fields, "
      "OIInternal::TypeHandler<FakeContext, "
      "OIInternal::__ROOT_TYPE__>::processors, "
      "std::is_fundamental_v<OIInternal::__ROOT_TYPE__>};\n";
  code += "} // namespace\n";
  code +=
      "extern const exporters::inst::Inst __attribute__((used, retain)) "
      "treeBuilderInstructions";
  code += typeHash;
  code += " = rootInstructions";
  code += typeHash;
  code += ";\n";
  code += "#pragma GCC diagnostic pop\n";
}

void FuncGen::DefineTopLevelGetSizeSmartPtr(std::string& testCode,
                                            const std::string& rawType,
                                            FeatureSet features) {
  std::string func = R"(
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunknown-attributes"
    /* RawType: %1% */
    void __attribute__((used, retain)) getSize_%2$016x(const OIInternal::__ROOT_TYPE__& t)
    #pragma GCC diagnostic pop
    {
    )";
  if (features[Feature::JitTiming]) {
    func += "      const auto startTime = std::chrono::steady_clock::now();\n";
  }
  func += R"(
      ctx.pointers.initialize();
      auto data = reinterpret_cast<uintptr_t*>(dataBase);

      size_t dataSegOffset = 0;
      data[dataSegOffset++] = oidMagicId;
      data[dataSegOffset++] = cookieValue;
      uintptr_t& writtenSize = data[dataSegOffset++];
      writtenSize = 0;
      uintptr_t& timeTakenNs = data[dataSegOffset++];
      size_t& pointersSize = data[dataSegOffset++];
      size_t& pointersCapacity = data[dataSegOffset++];

      dataSegOffset *= sizeof(uintptr_t);

      OIInternal::getSizeType(t, dataSegOffset);
      OIInternal::StoreData((uintptr_t)123456789, dataSegOffset);
      OIInternal::StoreData((uintptr_t)123456789, dataSegOffset);
      writtenSize = dataSegOffset;
      dataBase += dataSegOffset;
      pointersSize = ctx.pointers.size();
      pointersCapacity = ctx.pointers.capacity();
    )";
  if (features[Feature::JitTiming]) {
    func += R"(
      timeTakenNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - startTime).count();
      )";
  }
  func += R"(
    }
    )";

  boost::format fmt =
      boost::format(func) % rawType % std::hash<std::string>{}(rawType);
  testCode.append(fmt.str());
}

bool FuncGen::DeclareGetSizeFuncs(std::string& testCode,
                                  const ContainerInfoRefSet& containerInfo,
                                  FeatureSet features) {
  for (const ContainerInfo& cInfo : containerInfo) {
    std::string ctype = cInfo.typeName;
    ctype = ctype.substr(0, ctype.find("<", 0));

    auto& decl = cInfo.codegen.decl;
    boost::format fmt = boost::format(decl) % ctype;
    testCode.append(fmt.str());
  }

  if (features[Feature::ChaseRawPointers]) {
    testCode.append(
        "template<typename T, typename = "
        "std::enable_if_t<!std::is_pointer_v<std::decay_t<T>>>>\n");
  } else {
    testCode.append("template<typename T>\n");
  }
  testCode.append("void getSizeType(const T &t, size_t& returnArg);");

  return true;
}

bool FuncGen::DefineGetSizeFuncs(std::string& testCode,
                                 const ContainerInfoRefSet& containerInfo,
                                 FeatureSet features) {
  for (const ContainerInfo& cInfo : containerInfo) {
    std::string ctype = cInfo.typeName;
    ctype = ctype.substr(0, ctype.find("<", 0));

    auto& func = cInfo.codegen.func;
    boost::format fmt = boost::format(func) % ctype;
    testCode.append(fmt.str());
  }

  if (features[Feature::ChaseRawPointers]) {
    testCode.append("template<typename T, typename C>\n");
  } else {
    testCode.append("template<typename T>\n");
  }

  testCode.append(R"(
      void getSizeType(const T &t, size_t& returnArg) {
        JLOG("obj @");
        JLOGPTR(&t);
        SAVE_SIZE(sizeof(T));
      }
    )");

  return true;
}

void FuncGen::DefineGetSizeTypedValueFunc(std::string& testCode,
                                          const std::string& ctype) {
  boost::format fmt = boost::format(typedValueFunc) % ctype;
  testCode.append(fmt.str());
}

void FuncGen::DeclareGetContainer(std::string& testCode) {
  std::string func = R"(
      template <class ContainerAdapter>
      const typename ContainerAdapter::container_type & get_container (ContainerAdapter &ca)
      {
          struct unwrap : ContainerAdapter {
              static const typename ContainerAdapter::container_type & get (ContainerAdapter &ca) {
                  return ca.*&unwrap::c;
              }
          };
          return unwrap::get(ca);
      }
      )";
  testCode.append(func);
}

/*
 * DefineDataSegmentDataBuffer
 *
 * Provides a DataBuffer implementation that stores data in the setup Data
 * Segment. If more data is written than space available in the data segment,
 * the offset continues to increment but the data is not written. This allows
 * OID to report the size needed to process the data successfully.
 */
void FuncGen::DefineDataSegmentDataBuffer(std::string& testCode) {
  constexpr std::string_view func = R"(
    namespace oi::detail::DataBuffer {

    class DataSegment {
      public:
        DataSegment(size_t offset) : buf(dataBase + offset) {}

        void write_byte(uint8_t byte) {
          // TODO: Change the inputs to dataBase / dataEnd to improve this check
          if (buf < (dataBase + dataSize)) {
            *buf = byte;
          }
          buf++;
        }

        size_t offset() {
          return buf - dataBase;
        }

      private:
        uint8_t* buf;
    };

    } // namespace oi::detail::DataBuffer
  )";

  testCode.append(func);
}

/*
 * DefineBackInserterDataBuffer
 *
 * Provides a DataBuffer implementation that takes anything convertible with
 * std::back_inserter.
 */
void FuncGen::DefineBackInserterDataBuffer(std::string& code) {
  constexpr std::string_view buf = R"(
namespace oi::detail::DataBuffer {

template <class Container>
class BackInserter {
 public:
  BackInserter(Container& v) : buf(v) {}

  void write_byte(uint8_t byte) {
    *buf = byte;
  }
 private:
  std::back_insert_iterator<Container> buf;
};

} // namespace oi::detail::DataBuffer
  )";

  code.append(buf);
}

/*
 * DefineBasicTypeHandlers
 *
 * Provides TypeHandler implementations for types T, T*, and void. T is of type
 * Unit type and stores nothing. It should be overridden to provide an
 * implementation. T* is of type Pair<VarInt, Sum<Unit, T::type>. It stores the
 * pointer's value always, then the value of the pointer if it is unique. void
 * is of type Unit and always stores nothing.
 */
void FuncGen::DefineBasicTypeHandlers(std::string& code) {
  code += R"(
template <typename Ctx, typename T>
struct TypeHandler;
)";

  code += R"(
template <typename Ctx, typename T>
constexpr inst::Field make_field(std::string_view name) {
  return inst::Field{
      sizeof(T),
      ExclusiveSizeProvider<T>::size,
      name,
      NameProvider<T>::names,
      TypeHandler<Ctx, T>::fields,
      TypeHandler<Ctx, T>::processors,
      std::is_fundamental_v<T>,
  };
}
)";
  code += R"(
template <typename Ctx, typename T>
struct TypeHandler {
  using DB = typename Ctx::DataBuffer;

 private:
  static void process_pointer(result::Element& el,
                              std::function<void(inst::Inst)> stack_ins,
                              ParsedData d) {
    el.pointer = std::get<ParsedData::VarInt>(d.val).value;
  }
  static void process_pointer_content(result::Element& el,
                                      std::function<void(inst::Inst)> stack_ins,
                                      ParsedData d) {
    using U = std::decay_t<std::remove_pointer_t<T>>;
    const ParsedData::Sum& sum = std::get<ParsedData::Sum>(d.val);

    el.container_stats.emplace(result::Element::ContainerStats{ .capacity = 1, .length = 0 });
    if (sum.index == 0)
      return;
    el.container_stats->length = 1;

    if constexpr (oi_is_complete<U>) {
      static constexpr auto childField = make_field<Ctx, U>("*");
      stack_ins(childField);
    }
  }

  static auto choose_type() {
    if constexpr (std::is_pointer_v<T>) {
      return std::type_identity<types::st::Pair<
          DB,
          types::st::VarInt<DB>,
          types::st::Sum<
              DB,
              types::st::Unit<DB>,
              typename TypeHandler<Ctx, std::remove_pointer_t<T>>::type>>>();
    } else {
      return std::type_identity<types::st::Unit<DB>>();
    }
  }
  static constexpr auto choose_processors() {
    if constexpr (std::is_pointer_v<T>) {
      return std::array<inst::ProcessorInst, 2>{
          exporters::inst::ProcessorInst{types::st::VarInt<DB>::describe,
                                         &process_pointer},
          exporters::inst::ProcessorInst{
              types::st::Sum<
                  DB,
                  types::st::Unit<DB>,
                  typename TypeHandler<Ctx, std::remove_pointer_t<T>>::type>::
                  describe,
              &process_pointer_content},
      };
    } else {
      return std::array<inst::ProcessorInst, 0>{};
    }
  }

 public:
  using type = typename decltype(choose_type())::type;

  static constexpr std::array<exporters::inst::Field, 0> fields{};
  static constexpr auto processors = choose_processors();

  static types::st::Unit<DB> getSizeType(
      Ctx& ctx, const T& t, typename TypeHandler<Ctx, T>::type returnArg) {
    if constexpr (std::is_pointer_v<T>) {
      JLOG("ptr val @");
      JLOGPTR(t);
      auto r0 = returnArg.write((uintptr_t)t);
      if (t && ctx.pointers.add((uintptr_t)t)) {
        return r0.template delegate<1>([&ctx, &t](auto ret) {
          using U = std::decay_t<std::remove_pointer_t<T>>;
          if constexpr (oi_is_complete<U>) {
            return TypeHandler<Ctx, U>::getSizeType(ctx, *t, ret);
          } else {
            return ret;
          }
        });
      } else {
        return r0.template delegate<0>(std::identity());
      }
    } else {
      return returnArg;
    }
  }
};
)";

  code += R"(
template <typename Ctx>
class TypeHandler<Ctx, void> {
  using DB = typename Ctx::DataBuffer;

 public:
  using type = types::st::Unit<DB>;
  static constexpr std::array<exporters::inst::Field, 0> fields{};
  static constexpr std::array<exporters::inst::ProcessorInst, 0> processors{};
};
)";
}

ContainerInfo FuncGen::GetOiArrayContainerInfo() {
  ContainerInfo oiArray{"OIArray",
                        UNKNOWN_TYPE,
                        "cstdint"};  // TODO: remove the need for a dummy header

  oiArray.codegen.traversalFunc = R"(
auto tail = returnArg.write(N0);
for (size_t i=0; i<N0; i++) {
  tail = tail.delegate([&ctx, &container, i](auto ret) {
      return TypeHandler<Ctx, T0>::getSizeType(ctx, container.vals[i], ret);
  });
}
return tail.finish();
)";
  oiArray.codegen.processors.emplace_back(ContainerInfo::Processor{
      .type = "types::st::List<DB, typename TypeHandler<Ctx, T0>::type>",
      .func = R"(
static constexpr auto childField = make_field<Ctx, T0>("[]");

el.exclusive_size = 0;
el.container_stats.emplace(result::Element::ContainerStats{ .capacity = N0, .length = N0 });

auto list = std::get<ParsedData::List>(d.val);
// assert(list.length == N0);
for (size_t i = 0; i < N0; i++)
  stack_ins(childField);
)",
  });

  return oiArray;
}

}  // namespace oi::detail
