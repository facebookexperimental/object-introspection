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

/*
 * Static Types
 *
 * OI employs a data segment to transfer information about the probed object to
 * the debugger. Static Types are used with the `-ftyped-data-segment` feature
 * to provide a compile time description of the contents of this data segment.
 *
 * DataBuffer represents any type with two methods: `void write_byte(uint8_t)`,
 * which writes a given byte to the buffer; and, `size_t offset()`, which
 * returns the number of bytes written. Each Static Type holds a DataBuffer
 * which describes where to write data, and has no other fields. DataBuffers
 * should remain pointer sized enabling trivial copies.
 *
 * Writing to an object of a given static type returns a different type which
 * has had that part written. When there is no more to write, the type will
 * return a Unit. There are two ways to write data from the JIT code into a
 * static type:
 *
 * - .write(): This works if you can write an entire object from one input. For
 *              example, VarInt::write(0) returns a Unit, and
 *              Pair<VarInt, VarInt>::write(0) returns a VarInt.
 *
 * - .delegate(): This handles the remainder of the cases where you need to do
 *              something more complicated. For example:
 *              ```
 *              using ComplexType = Pair<VarInt, VarInt>;
 *              Pair<ComplexType, VarInt>::delegate([](auto ret) {
 *               return ret.write(0).write(1);
 *              }).write(2);
 *              ```
 *              In this case, `ret` is of type `ComplexType`. After the two
 *              writes, the inner function returns `Unit`. Delegate then
 *              internally converts this unit to a `VarInt`.
 *
 * DEFINE_DESCRIBE controls the additional feature of dynamic descriptions of
 * types. If defined when this header is included, static types provide a
 * dynamic description of their type as the constexpr field `describe`. Compound
 * types compose appropriately.
 */
namespace ObjectIntrospection::types::st {

#ifdef DEFINE_DESCRIBE
#include "oi/types/dy.h"
#endif

/*
 * Unit
 *
 * Represents the case of having completely written the type, or having nothing
 * of interest to write. Examples are after having written the final element of
 * the object, after having completely delegated a field, or having a field of
 * a struct that makes sense structurally but holds no interesting data.
 */
template <typename DataBuffer>
class Unit {
 public:
  Unit(DataBuffer db) : _buf(db) {
  }

  size_t offset() {
    return _buf.offset();
  }

  template <typename F>
  Unit<DataBuffer> delegate(F const& cb) {
    return cb(*this);
  }

#ifdef DEFINE_DESCRIBE
  static constexpr types::dy::Unit describe{};
#endif

 private:
  /*
   * Allows you to cast the Unit type to another Static Type. Think very
   * carefully before using it. It is private so that only friends can access
   * it. Good use cases are Pair::write and Pair::delegate to cast the result to
   * the second element. Bad use cases are within a type handler because the
   * type doesn't quite fit.
   */
  template <typename T>
  T cast() {
    return T(_buf);
  }

 private:
  DataBuffer _buf;

  template <typename DB, typename T1, typename T2>
  friend class Pair;
  template <typename DB, typename T>
  friend class ListContents;
};

/*
 * VarInt
 *
 * Represents a variable length integer. The only primitive type at present,
 * used for all data transfer.
 */
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

#ifdef DEFINE_DESCRIBE
  static constexpr types::dy::VarInt describe{};
#endif

 private:
  DataBuffer _buf;
};

/*
 * Pair<T1,T2>
 *
 * Represents a pair of types. Can be combined to hold an arbitrary number of
 * types, e.g. Pair<VarInt, Pair<VarInt, VarInt>> allows you to write three
 * integers.
 */
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

#ifdef DEFINE_DESCRIBE
  static constexpr types::dy::Pair describe{T1::describe, T2::describe};
#endif

 private:
  DataBuffer _buf;
};

/*
 * Sum<Types...>
 *
 * Represents a tagged union of types.
 */
template <typename DataBuffer, typename... Types>
class Sum {
 private:
  /*
   * Selector<I, Elements...>
   *
   * Selects the Ith type of Elements... and makes it available at ::type.
   */
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

#ifdef DEFINE_DESCRIBE
 private:
  static constexpr std::array<types::dy::Dynamic, sizeof...(Types)> members{
      Types::describe...};

 public:
  static constexpr types::dy::Sum describe{members};
#endif

 private:
  DataBuffer _buf;
};

/*
 * ListContents<T>
 *
 * Repeatedly delegate instances of type T, writing them one after the other.
 * Terminate with a call to finish().
 */
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

/*
 * List<T>
 *
 * Holds the length of a list followed by the elements. Write the length of the
 * list first then that number of elements.
 *
 * BEWARE: There is NO static or dynamic checking that you write the number of
 * elements promised.
 */
template <typename DataBuffer, typename T>
class List
    : public Pair<DataBuffer, VarInt<DataBuffer>, ListContents<DataBuffer, T>> {
 public:
  List(DataBuffer db)
      : Pair<DataBuffer, VarInt<DataBuffer>, ListContents<DataBuffer, T>>(db) {
  }

#ifdef DEFINE_DESCRIBE
 public:
  static constexpr types::dy::List describe{T::describe};
#endif
};

}  // namespace ObjectIntrospection::types::st

#endif
