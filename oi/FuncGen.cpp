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

void FuncGen::DeclareTopLevelGetSize(std::string& testCode,
                                     const std::string& type) {
  boost::format fmt = boost::format("void getSizeType(const %1% &t);\n") % type;
  testCode.append(fmt.str());
}
void FuncGen::DeclareStoreData(std::string& testCode) {
  testCode.append("void StoreData(uintptr_t data, size_t& dataSegOffset);\n");
}
void FuncGen::DeclareAddData(std::string& testCode) {
  testCode.append("void AddData(uint64_t data, size_t& dataSegOffset);\n");
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

void FuncGen::DefineAddData(std::string& testCode) {
  std::string func = R"(
    void AddData(uint64_t data, size_t& output) {
      output += data;
    }
    )";

  testCode.append(func);
}

void FuncGen::DefineTopLevelGetObjectSize(std::string& testCode,
                                          const std::string& rawType,
                                          const std::string& linkageName) {
  std::string func = R"(
    /* RawType: %1% */
    extern "C" int %2%(const OIInternal::__ROOT_TYPE__* ObjectAddr, size_t* ObjectSize)
    {
      *ObjectSize = 0;
      OIInternal::getSizeType(*ObjectAddr, *ObjectSize);
      return 0;
    }
  )";

  boost::format fmt = boost::format(func) % rawType % linkageName;
  testCode.append(fmt.str());
}

void FuncGen::DefineTopLevelGetSizeRef(std::string& testCode,
                                       const std::string& rawType) {
  std::string func = R"(
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunknown-attributes"
    /* RawType: %1% */
    void __attribute__((used, retain)) getSize_%2$016x(const OIInternal::__ROOT_TYPE__& t)
    #pragma GCC diagnostic pop
    {
      pointers.initialize();
      pointers.add((uintptr_t)&t);
      auto data = reinterpret_cast<uintptr_t*>(dataBase);
      data[0] = oidMagicId;
      data[1] = cookieValue;
      data[2] = 0;

      size_t dataSegOffset = 3 * sizeof(uintptr_t);
      JLOG("%1% @");
      JLOGPTR(&t);
      OIInternal::getSizeType(t, dataSegOffset);
      OIInternal::StoreData((uintptr_t)123456789, dataSegOffset);
      OIInternal::StoreData((uintptr_t)123456789, dataSegOffset);
      data[2] = dataSegOffset;
      dataBase += dataSegOffset;
    }
    )";

  boost::format fmt =
      boost::format(func) % rawType % std::hash<std::string>{}(rawType);
  testCode.append(fmt.str());
}

void FuncGen::DefineTopLevelGetSizeRefTyped(std::string& testCode,
                                            const std::string& rawType) {
  std::string func = R"(
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunknown-attributes"
    /* RawType: %1% */
    void __attribute__((used, retain)) getSize_%2$016x(const OIInternal::__ROOT_TYPE__& t)
    #pragma GCC diagnostic pop
    {
      pointers.initialize();
      pointers.add((uintptr_t)&t);
      auto data = reinterpret_cast<uintptr_t*>(dataBase);
      data[0] = oidMagicId;
      data[1] = cookieValue;
      data[2] = 0;
      size_t dataSegOffset = 3 * sizeof(uintptr_t);
      JLOG("%1% @");
      JLOGPTR(&t);
      using DataBufferType = OIInternal::TypeHandler<DataBuffer::DataSegment, OIInternal::__ROOT_TYPE__>::type;
      DataBufferType db = DataBuffer::DataSegment(dataSegOffset);
      StaticTypes::Unit<DataBuffer::DataSegment> out = OIInternal::getSizeType<DataBuffer::DataSegment>(t, db);
      StaticTypes::Unit<DataBuffer::DataSegment> final = out.template cast<StaticTypes::Pair<
        DataBuffer::DataSegment,
        StaticTypes::VarInt<DataBuffer::DataSegment>,
        StaticTypes::VarInt<DataBuffer::DataSegment>
      >>()
        .write(123456789)
        .write(123456789);
      dataSegOffset = final.offset();
      data[2] = dataSegOffset;
      dataBase += dataSegOffset;
    }
    )";

  boost::format fmt =
      boost::format(func) % rawType % std::hash<std::string>{}(rawType);
  testCode.append(fmt.str());
}

void FuncGen::DefineTopLevelGetSizeRefRet(std::string& testCode,
                                          const std::string& rawType) {
  std::string func = R"(
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunknown-attributes"
    /* Raw Type: %1% */
    size_t __attribute__((used, retain)) getSize(const OIInternal::__ROOT_TYPE__& t)
    #pragma GCC diagnostic pop
    {
      pointers.initialize();
      size_t ret = 0;
      pointers.add((uintptr_t)&t);
      OIInternal::getSizeType(t, ret);
      return ret;
    }
    )";

  boost::format fmt = boost::format(func) % rawType;
  testCode.append(fmt.str());
}

void FuncGen::DefineTopLevelGetSizeSmartPtr(std::string& testCode,
                                            const std::string& rawType) {
  std::string func = R"(
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunknown-attributes"
    /* RawType: %1% */
    void __attribute__((used, retain)) getSize_%2$016x(const OIInternal::__ROOT_TYPE__& t)
    #pragma GCC diagnostic pop
    {
      pointers.initialize();
      auto data = reinterpret_cast<uintptr_t*>(dataBase);
      data[0] = oidMagicId;
      data[1] = cookieValue;
      data[2] = 0;

      size_t dataSegOffset = 3 * sizeof(uintptr_t);

      OIInternal::getSizeType(t, dataSegOffset);
      OIInternal::StoreData((uintptr_t)123456789, dataSegOffset);
      OIInternal::StoreData((uintptr_t)123456789, dataSegOffset);
      data[2] = dataSegOffset;
      dataBase += dataSegOffset;
    }
    )";

  boost::format fmt =
      boost::format(func) % rawType % std::hash<std::string>{}(rawType);
  testCode.append(fmt.str());
}

bool FuncGen::DeclareGetSizeFuncs(std::string& testCode,
                                  const ContainerInfoRefSet& containerInfo,
                                  bool chaseRawPointers) {
  for (const ContainerInfo& cInfo : containerInfo) {
    std::string ctype = cInfo.typeName;
    ctype = ctype.substr(0, ctype.find("<", 0));

    auto& decl = cInfo.codegen.decl;
    boost::format fmt = boost::format(decl) % ctype;
    testCode.append(fmt.str());
  }

  if (chaseRawPointers) {
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
                                 bool chaseRawPointers) {
  for (const ContainerInfo& cInfo : containerInfo) {
    std::string ctype = cInfo.typeName;
    ctype = ctype.substr(0, ctype.find("<", 0));

    auto& func = cInfo.codegen.func;
    boost::format fmt = boost::format(func) % ctype;
    testCode.append(fmt.str());
  }

  if (chaseRawPointers) {
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

void FuncGen::DefineDataSegmentDataBuffer(std::string& testCode) {
  constexpr std::string_view func = R"(
    namespace ObjectIntrospection {
    namespace DataBuffer {
    class DataBuffer {
      protected:
        void write_byte(uint8_t);
    };
    class DataSegment: public DataBuffer {
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
    } // namespace DataBuffer
    } // namespace ObjectIntrospection
  )";

  testCode.append(func);
}

void FuncGen::DefineBasicTypeHandlers(std::string& testCode) {
  constexpr std::string_view handlers = R"(
    template <typename DB, typename T>
    struct TypeHandler {
      private:
        static auto choose_type() {
            if constexpr(std::is_pointer_v<T>) {
                return std::type_identity<StaticTypes::Pair<DB,
                  StaticTypes::VarInt<DB>,
                  StaticTypes::Sum<DB,
                    StaticTypes::Unit<DB>,
                    typename TypeHandler<DB, std::remove_pointer_t<T>>::type
                >>>();
            } else {
                return std::type_identity<StaticTypes::Unit<DB>>();
            }
        }
      public:
        using type = typename decltype(choose_type())::type;
        static StaticTypes::Unit<DB> getSizeType(
          const T& t,
          typename TypeHandler<DB, T>::type returnArg) {
            if constexpr(std::is_pointer_v<T>) {
              JLOG("ptr val @");
              JLOGPTR(t);
              auto r0 = returnArg.write((uintptr_t)t);
              if (t && pointers.add((uintptr_t)t)) {
                return r0.template delegate<1>([&t](auto ret) {
                  if constexpr (!std::is_void<std::remove_pointer_t<T>>::value) {
                    return TypeHandler<DB, std::remove_pointer_t<T>>::getSizeType(*t, ret);
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
    template <typename DB>
    class TypeHandler<DB, void> {
      public:
        using type = StaticTypes::Unit<DB>;
    };
  )";

  testCode.append(handlers);
}

void FuncGen::DefineStaticTypes(std::string& testCode) {
  constexpr std::string_view unitType = R"(
    template <typename DataBuffer>
    class Unit {
      public:
        Unit(DataBuffer db) : _buf(db) {}

        size_t offset() {
          return _buf.offset();
        }

        template <typename T>
        T cast() {
          return T(_buf);
        }

        template <typename F>
        Unit<DataBuffer>
        delegate(F const& cb) {
          return cb(*this);
        }

      private:
        DataBuffer _buf;
    };
  )";

  constexpr std::string_view varintType = R"(
    template <typename DataBuffer>
    class VarInt {
      public:
        VarInt(DataBuffer db) : _buf(db) {}

        Unit<DataBuffer> write(uint64_t val) {
          while (val >= 128) {
            _buf.write_byte(0x80 | (val & 0x7f));
            val >>= 7;
          }
          _buf.write_byte(uint8_t(val));
          return Unit<DataBuffer>(_buf);
        }

      private:
        DataBuffer _buf;
    };
  )";

  constexpr std::string_view pairType = R"(
    template <typename DataBuffer, typename T1, typename T2>
    class Pair {
      public:
        Pair(DataBuffer db) : _buf(db) {}
        template <class U>
        T2 write(U val) {
          Unit<DataBuffer> second = T1(_buf).write(val);
          return second.template cast<T2>();
        }
        template <typename F>
        T2 delegate(F const& cb) {
          T1 first = T1(_buf);
          Unit<DataBuffer> second = cb(first);
          return second.template cast<T2>();
        }
      private:
        DataBuffer _buf;
    };
  )";

  constexpr std::string_view sumType = R"(
    template <typename DataBuffer, typename... Types>
    class Sum {
      private:
        template <size_t I, typename... Elements>
        struct Selector;
        template <size_t I, typename Head, typename... Tail>
        struct Selector<I, Head, Tail...> {
          using type = typename std::conditional<I == 0, Head, typename Selector<I - 1, Tail...>::type>::type;
        };
        template<size_t I>
        struct Selector<I> {
          using type = int;
        };
      public:
        Sum(DataBuffer db) : _buf(db) {}
        template <size_t I>
        typename Selector<I, Types...>::type write() {
          Pair<DataBuffer, VarInt<DataBuffer>, typename Selector<I, Types...>::type> buf(_buf);
          return buf.write(I);
        }
        template <size_t I, typename F>
        Unit<DataBuffer> delegate(F const& cb) {
          auto tail = write<I>();
          return cb(tail);
        }
      private:
        DataBuffer _buf;
    };
  )";

  constexpr std::string_view listType = R"(
    template <typename DataBuffer, typename T>
    class ListContents {
      public:
        ListContents(DataBuffer db) : _buf(db) {}

        template<typename F>
        ListContents<DataBuffer, T> delegate(F const& cb) {
          T head = T(_buf);
          Unit<DataBuffer> tail = cb(head);
          return tail.template cast<ListContents<DataBuffer, T>>();
        }

        Unit<DataBuffer> finish() {
          return { _buf };
        }
      private:
        DataBuffer _buf;
    };

    template <typename DataBuffer, typename T>
    using List = Pair<DataBuffer, VarInt<DataBuffer>, ListContents<DataBuffer, T>>;
  )";

  testCode.append("namespace ObjectIntrospection {\n");
  testCode.append("namespace StaticTypes {\n");

  testCode.append(unitType);
  testCode.append(varintType);
  testCode.append(pairType);
  testCode.append(sumType);
  testCode.append(listType);

  testCode.append("} // namespace StaticTypes {\n");
  testCode.append("} // namespace ObjectIntrospection {\n");
}
