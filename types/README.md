# Container Type Definition Format

This document describes the format of the container definition files contained in this directory.

### info
- `type_name`

  The fully-qualified name of the container type. This is used to match against
  the names of types contained in an executable's debug information.

- `ctype`

  A reference to the enum `ContainerTypeEnum` in OI's source code.

- `header`

  The name of the C++ header file in which this container is defined.

- `stub_template_params`

  The indexes of template parameters which do not represent types stored within
  this container. These parameters will not be recursed into and measured by OI.

- `underlying_container_index`

  Only used for container adapters. Points OI to the template parameter
  representing the underlying container to be measured.

### codegen
- `decl`

  C++ code for the declaration of a `getSizeType` function for this container.

- `func`

  C++ code for the definition of a `getSizeType` function for this container.

- `handler`

  C++ code for the definition of a `TypeHandler` class for this container. See
  further down for a description.


## Changes introduced with Typed Data Segment
- `decl` and `func` fields are ignored when using `-ftyped-data-segment` and the
  `handler` field is used instead.

### TypeHandler Classes
A `TypeHandler` class describes both what a type will write into the data segment
and how to write it. It consists of two major parts:
- `using type = ...;` - describe what it will write into the data segment.
- `static types::st::Unit<DB> getSizeType(...)` - a function which takes a
  const reference to a container and a `::type` by value and fills in the type.

Example:
```cpp
template <typename DB, typename T0>
struct TypeHandler<DB, std::string<T0>> {
  using type =
      types::st::Pair<DB, types::st::VarInt<DB>, types::st::VarInt<DB>>;

  static types::st::Unit<DB> getSizeType(
      const std::string<T0> & container,
      typename TypeHandler<DB, std::string<T0>>::type returnArg) {
    bool sso = ((uintptr_t)container.data() <
                (uintptr_t)(&container + sizeof(std::string<T0>))) &&
               ((uintptr_t)container.data() >= (uintptr_t)&container);

    return returnArg.write(container.capacity()).write(container.size());
  }
};
```


## Changes introduced with TypeGraph
- `typeName` and `matcher` fields have been merged into the single field `type_name`.
- `ns` namespace definition is no longer required.
- `underlyingContainerIndex` renamed to `underlying_container_index`
- The options for measuring / stubbing / removing template parameters have been reworked:
  - By default all parameters are now measured. This includes the tail parameters in a variadic template.
  - There is one option to specify template parameters which should not be measured: `stub_template_params`. The type graph code will automatically determine what type of stubbing to apply to each parameter in this list.

### Deprecated Options
- `numTemplateParams`

  The first `numTemplateParams` template parameters for this container represent
  the types for the data we want to process. If this is not set, use all a
  container's template parameters. All of a container's parameters will still be
  enumerated and output in CodeGen, regardless of this setting.

- `allocatorIndex`

  Index of a template parameter representing an allocator. It will be not be
  used when CodeGenning this container.

- `replaceTemplateParamIndex`

  Indexes of template parameters to be stubbed out, i.e. replaced with dummy
  structs of a given size. Used for custom hashers and comparers.
