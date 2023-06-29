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
#ifndef OI_TYPES_ST_H
#define OI_TYPES_ST_H 1

namespace ObjectIntrospection {
namespace types {
namespace st {

template <typename DataBuffer>
class Unit {
 public:
  Unit(DataBuffer db) : _buf(db) {
  }

  size_t offset() {
    return _buf.offset();
  }

  template <typename T>
  T cast() {
    return T(_buf);
  }

  template <typename F>
  Unit<DataBuffer> delegate(F const& cb) {
    return cb(*this);
  }

 private:
  DataBuffer _buf;
};

template <typename DataBuffer>
class VarInt {
 public:
  VarInt(DataBuffer db) : _buf(db) {
  }

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

template <typename DataBuffer, typename T1, typename T2>
class Pair {
 public:
  Pair(DataBuffer db) : _buf(db) {
  }
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

template <typename DataBuffer, typename... Types>
class Sum {
 private:
  template <size_t I, typename... Elements>
  struct Selector;
  template <size_t I, typename Head, typename... Tail>
  struct Selector<I, Head, Tail...> {
    using type = typename std::conditional<
        I == 0,
        Head,
        typename Selector<I - 1, Tail...>::type>::type;
  };
  template <size_t I>
  struct Selector<I> {
    using type = int;
  };

 public:
  Sum(DataBuffer db) : _buf(db) {
  }
  template <size_t I>
  typename Selector<I, Types...>::type write() {
    Pair<DataBuffer, VarInt<DataBuffer>, typename Selector<I, Types...>::type>
        buf(_buf);
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

template <typename DataBuffer, typename T>
class ListContents {
 public:
  ListContents(DataBuffer db) : _buf(db) {
  }

  template <typename F>
  ListContents<DataBuffer, T> delegate(F const& cb) {
    T head = T(_buf);
    Unit<DataBuffer> tail = cb(head);
    return tail.template cast<ListContents<DataBuffer, T>>();
  }

  Unit<DataBuffer> finish() {
    return {_buf};
  }

 private:
  DataBuffer _buf;
};

template <typename DataBuffer, typename T>
using List = Pair<DataBuffer, VarInt<DataBuffer>, ListContents<DataBuffer, T>>;

}  // namespace st
}  // namespace types
}  // namespace ObjectIntrospection

#endif
