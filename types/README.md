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
