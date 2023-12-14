#include "TypeGraphParser.h"

#include <string>

#include "oi/type_graph/TypeGraph.h"

namespace {

/*
 * Sets line to the contents of the current line.
 * Advances input to the beginning of the next line.
 */
bool getline(std::string_view& input, std::string_view& line) {
  auto nextline = input.find('\n');
  if (nextline == input.npos) {
    return false;
  }
  line = input.substr(0, nextline);
  input = input.substr(nextline + 1);
  return true;
}

Primitive::Kind getKind(std::string_view kindStr) {
  if (kindStr == "int8_t")
    return Primitive::Kind::Int8;
  if (kindStr == "int16_t")
    return Primitive::Kind::Int16;
  if (kindStr == "int32_t")
    return Primitive::Kind::Int32;
  if (kindStr == "int64_t")
    return Primitive::Kind::Int64;
  if (kindStr == "uint8_t")
    return Primitive::Kind::UInt8;
  if (kindStr == "uint16_t")
    return Primitive::Kind::UInt16;
  if (kindStr == "uint32_t")
    return Primitive::Kind::UInt32;
  if (kindStr == "uint64_t")
    return Primitive::Kind::UInt64;
  if (kindStr == "float")
    return Primitive::Kind::Float32;
  if (kindStr == "double")
    return Primitive::Kind::Float64;
  if (kindStr == "long double")
    return Primitive::Kind::Float128;
  if (kindStr == "bool")
    return Primitive::Kind::Bool;
  if (kindStr == "StubbedPointer")
    return Primitive::Kind::StubbedPointer;
  if (kindStr == "void")
    return Primitive::Kind::Void;
  throw TypeGraphParserError{"Invalid Primitive::Kind: " +
                             std::string{kindStr}};
}

ContainerInfo& getContainerInfo(std::string_view name) {
  if (name == "std::vector") {
    static ContainerInfo info{"std::vector", SEQ_TYPE, "vector"};
    info.stubTemplateParams = {1};
    return info;
  }
  if (name == "std::map") {
    static ContainerInfo info{"std::map", STD_MAP_TYPE, "utility"};
    info.stubTemplateParams = {2, 3};
    return info;
  }
  if (name == "std::pair") {
    static ContainerInfo info{"std::pair", SEQ_TYPE, "utility"};
    return info;
  }
  if (name == "std::allocator") {
    static ContainerInfo info{"std::allocator", DUMMY_TYPE, "memory"};
    return info;
  }
  throw TypeGraphParserError{"Unsupported container: " + std::string{name}};
}

Qualifier getQualifier(std::string_view line) {
  if (line == "const") {
    return Qualifier::Const;
  }
  throw TypeGraphParserError{"Unsupported qualifier: " + std::string{line}};
}

size_t stripIndent(std::string_view& line) {
  auto indent = line.find_first_not_of(" ");
  line.remove_prefix(indent);
  return indent;
}

bool tryRemovePrefix(std::string_view& line, std::string_view prefix) {
  if (!line.starts_with(prefix))
    return false;
  line.remove_prefix(prefix.size());
  return true;
}

void removePrefix(std::string_view& line, std::string_view prefix) {
  if (!tryRemovePrefix(line, prefix))
    throw TypeGraphParserError{"Unexpected line prefix. Expected '" +
                               std::string{prefix} + "'. Got '" +
                               std::string{line} + "'."};
}

std::optional<double> tryParseNumericAttribute(std::string_view line,
                                               std::string_view marker) {
  auto attrStartPos = line.find(marker);
  if (attrStartPos == line.npos)
    return {};

  auto valStartPos = attrStartPos + marker.size();
  auto valEndPos = line.find_first_not_of("0123456789.", valStartPos);

  auto valStr = line.substr(valStartPos, valEndPos);
  double val = std::stod(std::string{valStr});  // Makes a string copy :'(
  return val;
}

double parseNumericAttribute(std::string_view line,
                             std::string_view type,
                             std::string_view marker) {
  auto val = tryParseNumericAttribute(line, marker);
  if (!val)
    throw TypeGraphParserError{
        std::string{type} + " must have a numeric attribute: '" +
        std::string{marker} + "'. Got: '" + std::string{line} + "'"};
  return *val;
}

std::optional<std::string_view> tryParseStringValue(std::string_view& input,
                                                    std::string_view marker,
                                                    size_t rootIndent) {
  std::string_view modifiedInput = input;
  std::string_view line;
  getline(modifiedInput, line);

  size_t indent = stripIndent(line);
  if (indent != rootIndent)
    return {};

  if (!tryRemovePrefix(line, marker))
    return {};

  auto val = line;

  // Update input to point to after the value we have read
  input = modifiedInput;

  return val;
}

std::optional<std::string_view> tryParseInputName(std::string_view input) {
  auto left = input.find_first_of('[');
  auto right = input.find_last_of(']');
  if (left == std::string_view::npos || right == std::string_view::npos)
    return {};
  return input.substr(left + 1, right - left - 1);
}

NodeId getId(std::string_view str, size_t* idLen = nullptr) {
  if (idLen)
    *idLen = 0;
  if (str[0] != '[')
    return -1;

  auto closeBracket = str.find(']');
  if (closeBracket == str.npos)
    return -1;

  auto idStr = str.substr(1, closeBracket);
  NodeId id = std::stoi(std::string{idStr});  // Makes a string copy :'(

  if (idLen)
    *idLen = closeBracket + 2;  // +2 for the trailing "] "
  return id;
}

}  // namespace

void TypeGraphParser::parse(std::string_view input) {
  size_t rootIndent = input.find_first_not_of("[]0123456789 ");
  while (!input.empty()) {
    Type& type = parseType(input, rootIndent);
    typeGraph_.addRoot(type);
  }
}

Type& TypeGraphParser::parseType(std::string_view& input, size_t rootIndent) {
  std::string_view line;
  getline(input, line);

  size_t idLen = 0;
  NodeId id = getId(line, &idLen);
  line.remove_prefix(idLen);

  size_t indent = stripIndent(line) + idLen;
  if (indent != rootIndent)
    throw TypeGraphParserError{"Unexpected indent for line: " +
                               std::string{line}};

  auto nodeEndPos = line.find_first_of(": \n");
  auto nodeTypeName = line.substr(0, nodeEndPos);

  Type* type = nullptr;
  if (NodeId refId = getId(nodeTypeName); refId != -1) {
    auto it = nodesById_.find(refId);
    if (it == nodesById_.end())
      throw TypeGraphParserError{"Node ID referenced before definition: " +
                                 std::to_string(refId)};

    type = &it->second.get();
  } else if (nodeTypeName == "Incomplete") {
    if (line[nodeEndPos] == ':') {
      auto nameStartPos = line.find('[', nodeEndPos) + 1;
      auto nameEndPos = line.find(']', nameStartPos);
      auto underlyingTypeName =
          line.substr(nameStartPos, nameEndPos - nameStartPos);
      type = &typeGraph_.makeType<Incomplete>(std::string(underlyingTypeName));
    } else {
      auto& underlyingType = parseType(input, indent + 2);
      type = &typeGraph_.makeType<Incomplete>(underlyingType);
    }
  } else if (nodeTypeName == "Class" || nodeTypeName == "Struct" ||
             nodeTypeName == "Union") {
    // Format: "Class: MyClass (size: 12)"
    Class::Kind kind;
    if (nodeTypeName == "Class")
      kind = Class::Kind::Class;
    else if (nodeTypeName == "Struct")
      kind = Class::Kind::Struct;
    else if (nodeTypeName == "Union")
      kind = Class::Kind::Union;
    else
      abort();

    // Extract name
    auto nameStartPos = line.find(' ') + 1;
    auto nameEndPos = line.find('(', nameStartPos + 1);
    auto name = line.substr(nameStartPos, nameEndPos - nameStartPos - 1);

    auto size = parseNumericAttribute(line, nodeTypeName, "size: ");
    auto align = tryParseNumericAttribute(line, "align: ");

    Class& c = typeGraph_.makeType<Class>(id, kind, std::string{name}, size);
    if (align)
      c.setAlign(static_cast<uint64_t>(*align));
    nodesById_.insert({id, c});

    parseParams(c, input, indent + 2);
    parseParents(c, input, indent + 2);
    parseMembers(c, input, indent + 2);
    parseFunctions(c, input, indent + 2);
    parseChildren(c, input, indent + 2);

    type = &c;
  } else if (nodeTypeName == "Container") {
    // Format: "Container: std::vector (size: 24)

    // Extract container type name
    auto nameStartPos = line.find(' ') + 1;
    auto nameEndPos = line.find('(', nameStartPos + 1);
    auto name = line.substr(nameStartPos, nameEndPos - nameStartPos - 1);

    auto& info = getContainerInfo(name);

    auto size = parseNumericAttribute(line, nodeTypeName, "size: ");

    Container& c = typeGraph_.makeType<Container>(id, info, size, nullptr);
    nodesById_.insert({id, c});

    parseParams(c, input, indent + 2);
    parseUnderlying(c, input, indent + 2);

    type = &c;
  } else if (nodeTypeName == "Primitive") {
    // Format: "Primitive: int32_t"
    removePrefix(line, "Primitive: ");
    auto kind = getKind(line);
    type = &typeGraph_.makeType<Primitive>(kind);
  } else if (nodeTypeName == "Enum") {
    // Format: "Enum: MyEnum (size: 4)"
    removePrefix(line, "Enum: ");
    auto nameEndPos = line.find(' ');
    auto name = line.substr(0, nameEndPos);
    auto size = parseNumericAttribute(line, nodeTypeName, "size: ");
    type = &typeGraph_.makeType<Enum>(std::string{name}, size);
  } else if (nodeTypeName == "Array") {
    // Format: "Array: (length: 5)
    auto len = parseNumericAttribute(line, nodeTypeName, "length: ");
    auto& elementType = parseType(input, indent + 2);
    type = &typeGraph_.makeType<Array>(id, elementType, len);
  } else if (nodeTypeName == "Typedef") {
    // Format: "Typedef: myTypedef"
    removePrefix(line, "Typedef: ");
    auto name = line;
    auto& underlyingType = parseType(input, indent + 2);
    type = &typeGraph_.makeType<Typedef>(id, std::string{name}, underlyingType);
    nodesById_.insert({id, *type});
  } else if (nodeTypeName == "Pointer") {
    // Format: "Pointer"
    auto& pointeeType = parseType(input, indent + 2);
    type = &typeGraph_.makeType<Pointer>(id, pointeeType);
    nodesById_.insert({id, *type});
  } else if (nodeTypeName == "Dummy") {
    // Format: "Dummy (size: 4)"
    auto size = parseNumericAttribute(line, nodeTypeName, "size: ");
    std::string inputName{*tryParseInputName(line)};
    type = &typeGraph_.makeType<Dummy>(id, size, 0, inputName);
  } else if (nodeTypeName == "DummyAllocator") {
    // Format: "DummyAllocator (size: 8)"
    auto size = parseNumericAttribute(line, nodeTypeName, "size: ");
    std::string inputName{*tryParseInputName(line)};
    auto& typeToAlloc = parseType(input, indent + 2);
    type = &typeGraph_.makeType<DummyAllocator>(
        id, typeToAlloc, size, 0, inputName);
  } else {
    throw TypeGraphParserError{"Unsupported node type: " +
                               std::string{nodeTypeName}};
  }

  return *type;
}

template <typename T>
void TypeGraphParser::parseParams(T& c,
                                  std::string_view& input,
                                  size_t rootIndent) {
  std::string_view origInput = input;
  for (std::string_view line; getline(input, line); origInput = input) {
    size_t indent = stripIndent(line);
    if (indent != rootIndent)
      break;

    // Format: "Param"
    if (!tryRemovePrefix(line, "Param"))
      break;

    auto value = tryParseStringValue(input, "Value: ", rootIndent + 2);
    Type& type = parseType(input, rootIndent + 2);
    TemplateParam param{type};
    if (value)
      param.value = value;
    if (auto qualStr =
            tryParseStringValue(input, "Qualifiers: ", rootIndent + 2);
        qualStr) {
      Qualifier qual = getQualifier(*qualStr);
      param.qualifiers[qual] = true;
    }

    c.templateParams.push_back(param);
  }
  // No more params for us - put back the line we just read
  input = origInput;
}

void TypeGraphParser::parseParents(Class& c,
                                   std::string_view& input,
                                   size_t rootIndent) {
  std::string_view origInput = input;
  for (std::string_view line; getline(input, line); origInput = input) {
    size_t indent = stripIndent(line);
    if (indent != rootIndent)
      break;

    // Format: "Parent (offset: 0)"
    if (!tryRemovePrefix(line, "Parent "))
      break;

    auto offset = parseNumericAttribute(line, "Parent", "offset: ");
    Type& type = parseType(input, rootIndent + 2);

    c.parents.emplace_back(type, offset * 8);
  }
  // No more parents for us - put back the line we just read
  input = origInput;
}

void TypeGraphParser::parseMembers(Class& c,
                                   std::string_view& input,
                                   size_t rootIndent) {
  std::string_view origInput = input;
  for (std::string_view line; getline(input, line); origInput = input) {
    size_t indent = stripIndent(line);
    if (indent != rootIndent)
      break;

    // Format: "Member: memberName (offset: 0)"
    if (!tryRemovePrefix(line, "Member: "))
      break;

    auto nameEndPos = line.find(' ');
    auto name = line.substr(0, nameEndPos);
    auto offset = parseNumericAttribute(line, "Member", "offset: ");
    auto align = tryParseNumericAttribute(line, "align: ");
    auto bitsize = tryParseNumericAttribute(line, "bitsize: ");
    Type& type = parseType(input, rootIndent + 2);

    Member member{type, std::string{name}, static_cast<uint64_t>(offset * 8)};
    if (align)
      member.align = static_cast<uint64_t>(*align);
    if (bitsize)
      member.bitsize = static_cast<uint64_t>(*bitsize);

    c.members.push_back(member);
  }
  // No more members for us - put back the line we just read
  input = origInput;
}

void TypeGraphParser::parseFunctions(Class& c,
                                     std::string_view& input,
                                     size_t rootIndent) {
  std::string_view origInput = input;
  for (std::string_view line; getline(input, line); origInput = input) {
    size_t indent = stripIndent(line);
    if (indent != rootIndent)
      break;

    // Format: "Function: funcName"
    if (!tryRemovePrefix(line, "Function: "))
      break;

    auto name = line;

    Function func{std::string{name}};

    c.functions.push_back(func);
  }
  // No more functions for us - put back the line we just read
  input = origInput;
}

void TypeGraphParser::parseChildren(Class& c,
                                    std::string_view& input,
                                    size_t rootIndent) {
  std::string_view origInput = input;
  for (std::string_view line; getline(input, line); origInput = input) {
    size_t indent = stripIndent(line);
    if (indent != rootIndent)
      break;

    // Format: "Child"
    if (!tryRemovePrefix(line, "Child"))
      break;

    Type& type = parseType(input, rootIndent + 2);
    auto* childClass = dynamic_cast<Class*>(&type);
    if (!childClass)
      throw TypeGraphParserError{"Invalid type for child"};

    c.children.push_back(*childClass);
  }
  // No more children for us - put back the line we just read
  input = origInput;
}

void TypeGraphParser::parseUnderlying(Container& c,
                                      std::string_view& input,
                                      size_t rootIndent) {
  std::string_view origInput = input;
  std::string_view line;
  getline(input, line);

  size_t indent = stripIndent(line);
  if (indent != rootIndent) {
    input = origInput;
    return;
  }

  // Format: "Underlying"
  if (!tryRemovePrefix(line, "Underlying")) {
    input = origInput;
    return;
  }

  Type& type = parseType(input, rootIndent + 2);
  c.setUnderlying(&type);
}
