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
#include "ClangTypeParser.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/Type.h>
#include <clang/Basic/DiagnosticSema.h>
#include <clang/Sema/Sema.h>
#include <glog/logging.h>

#include <stdexcept>

#include "oi/type_graph/Types.h"

namespace oi::detail::type_graph {
namespace {
bool requireCompleteType(clang::Sema& sema, const clang::Type& ty);
}

Type& ClangTypeParser::parse(clang::ASTContext& ast_,
                             clang::Sema& sema_,
                             const clang::Type& ty) {
  ast = &ast_;
  sema = &sema_;

  depth_ = 0;
  return enumerateType(ty);
}

Type& ClangTypeParser::enumerateType(const clang::Type& ty) {
  // Avoid re-enumerating an already-processsed type
  if (auto it = clang_types_.find(&ty); it != clang_types_.end())
    return it->second;

  struct DepthTracker {
    DepthTracker(ClangTypeParser& self_) : self{self_} {
      self.depth_++;
    }
    ~DepthTracker() {
      self.depth_--;
    }

   private:
    ClangTypeParser& self;
  } d{*this};

  if (VLOG_IS_ON(3)) {
    std::string fqName = clang::TypeName::getFullyQualifiedName(
        clang::QualType(&ty, 0), *ast, {ast->getLangOpts()});
    VLOG(3) << std::string(depth_ * 2, ' ') << fqName;
  }

  // TODO: This check basically doesn't work. The action has failed before it
  // returns false. Fix this.
  if (!requireCompleteType(*sema, ty)) {
    std::string fqName = clang::TypeName::getFullyQualifiedName(
        clang::QualType(&ty, 0), *ast, {ast->getLangOpts()});
    return makeType<Incomplete>(ty, std::move(fqName));
  }

  switch (ty.getTypeClass()) {
    case clang::Type::Record:
      return enumerateClass(llvm::cast<const clang::RecordType>(ty));
    case clang::Type::LValueReference:
      return enumerateReference(
          llvm::cast<const clang::LValueReferenceType>(ty));
    case clang::Type::Pointer:
      return enumeratePointer(llvm::cast<const clang::PointerType>(ty));
    case clang::Type::SubstTemplateTypeParm:
      return enumerateSubstTemplateTypeParm(
          llvm::cast<const clang::SubstTemplateTypeParmType>(ty));
    case clang::Type::Builtin:
      return enumeratePrimitive(llvm::cast<const clang::BuiltinType>(ty));
    case clang::Type::Elaborated:
      return enumerateElaboratedType(
          llvm::cast<const clang::ElaboratedType>(ty));
    case clang::Type::TemplateSpecialization:
      return enumerateTemplateSpecialization(
          llvm::cast<const clang::TemplateSpecializationType>(ty));
    case clang::Type::UnaryTransform:
      return enumerateUnaryTransformType(
          llvm::cast<const clang::UnaryTransformType>(ty));
    case clang::Type::Decltype:
      return enumerateDecltypeType(llvm::cast<const clang::DecltypeType>(ty));
    case clang::Type::Typedef:
      return enumerateTypedef(llvm::cast<const clang::TypedefType>(ty));
    case clang::Type::Using:
      return enumerateUsing(llvm::cast<const clang::UsingType>(ty));
    case clang::Type::ConstantArray:
      return enumerateArray(llvm::cast<const clang::ConstantArrayType>(ty));
    case clang::Type::Enum:
      return enumerateEnum(llvm::cast<const clang::EnumType>(ty));

    default:
      throw std::logic_error(std::string("unsupported TypeClass `") +
                             ty.getTypeClassName() + '`');
  }
}

Type& ClangTypeParser::enumerateDecltypeType(const clang::DecltypeType& ty) {
  return enumerateType(*ty.getUnderlyingType());
}

Type& ClangTypeParser::enumerateUnaryTransformType(
    const clang::UnaryTransformType& ty) {
  return enumerateType(*ty.getUnderlyingType());
}

Typedef& ClangTypeParser::enumerateUsing(const clang::UsingType& ty) {
  auto& inner = enumerateType(*ty.desugar());
  std::string name = ty.getFoundDecl()->getNameAsString();
  return makeType<Typedef>(ty, std::move(name), inner);
}

Typedef& ClangTypeParser::enumerateTypedef(const clang::TypedefType& ty) {
  auto& inner = enumerateType(*ty.desugar());

  std::string name = ty.getDecl()->getNameAsString();
  return makeType<Typedef>(ty, std::move(name), inner);
}

Enum& ClangTypeParser::enumerateEnum(const clang::EnumType& ty) {
  std::string fqName = clang::TypeName::getFullyQualifiedName(
      clang::QualType(&ty, 0), *ast, {ast->getLangOpts()});
  std::string name = ty.getDecl()->getNameAsString();
  auto size = ast->getTypeSize(clang::QualType(&ty, 0)) / 8;

  std::map<int64_t, std::string> enumeratorMap;
  if (options_.readEnumValues) {
    for (const auto* enumerator : ty.getDecl()->enumerators()) {
      enumeratorMap.emplace(enumerator->getInitVal().getExtValue(),
                            enumerator->getNameAsString());
    }
  }

  return makeType<Enum>(
      ty, std::move(name), std::move(fqName), size, std::move(enumeratorMap));
}

Array& ClangTypeParser::enumerateArray(const clang::ConstantArrayType& ty) {
  uint64_t len = ty.getSize().getLimitedValue();
  auto& t = enumerateType(*ty.getElementType());
  return makeType<Array>(ty, t, len);
}

Type& ClangTypeParser::enumerateTemplateSpecialization(
    const clang::TemplateSpecializationType& ty) {
  if (ty.isSugared())
    return enumerateType(*ty.desugar());

  LOG(WARNING) << "failed on a TemplateSpecializationType";
  ty.dump();
  return makeType<Primitive>(ty, Primitive::Kind::Int32);
}

Type& ClangTypeParser::enumerateClass(const clang::RecordType& ty) {
  std::string fqName = clang::TypeName::getFullyQualifiedName(
      clang::QualType(&ty, 0), *ast, {ast->getLangOpts()});
  auto size = ast->getTypeSize(clang::QualType(&ty, 0)) / 8;

  if (auto* info = getContainerInfo(fqName)) {
    auto& c = makeType<Container>(ty, *info, size, nullptr);
    enumerateClassTemplateParams(ty, c.templateParams);
    c.setAlign(ast->getTypeAlign(clang::QualType(&ty, 0)) / 8);
    return c;
  }

  auto* decl = ty.getDecl();

  std::string name = decl->getNameAsString();

  auto kind = Class::Kind::Struct;
  if (ty.isUnionType()) {
    kind = Class::Kind::Union;
  } else if (ty.isClassType()) {
    kind = Class::Kind::Class;
  }

  int virtuality = 0;

  auto& c = makeType<Class>(
      ty, kind, std::move(name), std::move(fqName), size, virtuality);
  c.setAlign(ast->getTypeAlign(clang::QualType(&ty, 0)) / 8);

  enumerateClassTemplateParams(ty, c.templateParams);
  enumerateClassParents(ty, c.parents);
  enumerateClassMembers(ty, c.members);
  // enumerateClassFunctions(type, c.functions);

  return c;
}

void ClangTypeParser::enumerateClassTemplateParams(
    const clang::RecordType& ty, std::vector<TemplateParam>& params) {
  assert(params.empty());

  auto* decl = dyn_cast<clang::ClassTemplateSpecializationDecl>(ty.getDecl());
  if (decl == nullptr)
    return;

  const auto& list = decl->getTemplateArgs();

  params.reserve(list.size());
  for (const auto& arg : list.asArray()) {
    if (auto p = enumerateTemplateParam(arg))
      params.emplace_back(std::move(p.value()));
  }
}

std::optional<TemplateParam> ClangTypeParser::enumerateTemplateParam(
    const clang::TemplateArgument& p) {
  switch (p.getKind()) {
    case clang::TemplateArgument::Type: {
      auto qualType = p.getAsType();
      QualifierSet qualifiers;
      qualifiers[Qualifier::Const] = qualType.isConstQualified();
      Type& ttype = enumerateType(*qualType);
      return TemplateParam{ttype, qualifiers};
    }
    case clang::TemplateArgument::Integral: {
      auto& ty = enumerateType(*p.getIntegralType());

      std::string value;
      if (const auto* e = dynamic_cast<const Enum*>(&ty)) {
        const auto& eTy =
            llvm::cast<const clang::EnumType>(*p.getIntegralType());
        for (const auto* enumerator : eTy.getDecl()->enumerators()) {
          if (enumerator->getInitVal() == p.getAsIntegral()) {
            value = e->inputName();
            value += "::";
            value += enumerator->getName();
            break;
          }
        }
        if (value.empty()) {
          throw std::runtime_error("unable to find enum name for value");
        }
      } else {
        llvm::SmallString<32> val;
        p.getAsIntegral().toString(val);
        value = std::string{val};
      }

      return TemplateParam{ty, value};
    }
    case clang::TemplateArgument::Template: {
      return enumerateTemplateTemplateParam(p.getAsTemplate());
    }

#define X(name)                       \
  case clang::TemplateArgument::name: \
    throw std::logic_error("unsupported template argument kind: " #name);

      X(Null)
      X(Declaration)
      X(NullPtr)
      X(TemplateExpansion)
      X(Expression)
      X(Pack)
#undef X
  }
}

std::optional<TemplateParam> ClangTypeParser::enumerateTemplateTemplateParam(
    const clang::TemplateName& tn) {
  switch (tn.getKind()) {
    case clang::TemplateName::Template:
      return std::nullopt;

#define X(name)                   \
  case clang::TemplateName::name: \
    throw std::logic_error("unsupported template name kind: " #name);

      X(OverloadedTemplate)
      X(AssumedTemplate)
      X(QualifiedTemplate)
      X(DependentTemplate)
      X(SubstTemplateTemplateParm)
      X(SubstTemplateTemplateParmPack)
      X(UsingTemplate)
#undef X
  }
}

void ClangTypeParser::enumerateClassParents(const clang::RecordType& ty,
                                            std::vector<Parent>& parents) {
  assert(parents.empty());

  auto* decl = ty.getDecl();
  auto* cxxDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
  if (cxxDecl == nullptr)
    return;

  const auto& layout = decl->getASTContext().getASTRecordLayout(decl);
  for (const auto& base : cxxDecl->bases()) {
    auto baseType = base.getType().getDesugaredType(decl->getASTContext());
    const auto* baseRecordType = llvm::dyn_cast<clang::RecordType>(&*baseType);
    if (baseRecordType == nullptr)
      continue;

    auto* baseDecl = baseRecordType->getDecl();
    auto* baseCxxDecl = llvm::dyn_cast<clang::CXXRecordDecl>(baseDecl);
    if (baseCxxDecl == nullptr)
      continue;

    auto offset = layout.getBaseClassOffset(baseCxxDecl).getQuantity();
    auto& ptype = enumerateType(*baseType);
    Parent p{ptype, static_cast<uint64_t>(offset * 8)};
    parents.push_back(p);
  }
}

void ClangTypeParser::enumerateClassMembers(const clang::RecordType& ty,
                                            std::vector<Member>& members) {
  assert(members.empty());

  auto* decl = ty.getDecl();

  for (const auto* field : decl->fields()) {
    clang::QualType qualType = field->getType();
    std::string member_name = field->getNameAsString();

    size_t size_in_bits = 0;
    if (field->isBitField()) {
      size_in_bits = field->getBitWidthValue(*ast);
    }

    size_t offset_in_bits = decl->getASTContext().getFieldOffset(field);

    auto& mtype = enumerateType(*qualType);
    Member m{mtype, std::move(member_name), offset_in_bits, size_in_bits};
    members.push_back(m);
  }

  std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
    return a.bitOffset < b.bitOffset;
  });
}

Type& ClangTypeParser::enumerateReference(
    const clang::LValueReferenceType& ty) {
  // TODO: function references
  Type& t = enumerateType(*ty.getPointeeType());
  if (dynamic_cast<Incomplete*>(&t))
    return makeType<Pointer>(ty, t);

  return makeType<Reference>(ty, t);
}

Type& ClangTypeParser::enumeratePointer(const clang::PointerType& ty) {
  // TODO: function pointers
  if (!chasePointer())
    return makeType<Primitive>(ty, Primitive::Kind::StubbedPointer);

  Type& t = enumerateType(*ty.getPointeeType());
  return makeType<Reference>(ty, t);
}

Type& ClangTypeParser::enumerateSubstTemplateTypeParm(
    const clang::SubstTemplateTypeParmType& ty) {
  // Clang wraps any type that was substituted from e.g. `T` in this type. It
  // should have no representation in the type graph.
  return enumerateType(*ty.getReplacementType());
}

Type& ClangTypeParser::enumerateElaboratedType(
    const clang::ElaboratedType& ty) {
  // Clang wraps any type that is name qualified in this type. It should have no
  // representation in the type graph.
  return enumerateType(*ty.getNamedType());
}

Primitive& ClangTypeParser::enumeratePrimitive(const clang::BuiltinType& ty) {
  switch (ty.getKind()) {
    case clang::BuiltinType::Void:
      return makeType<Primitive>(ty, Primitive::Kind::Void);

    case clang::BuiltinType::Bool:
      return makeType<Primitive>(ty, Primitive::Kind::Bool);

    case clang::BuiltinType::Char_U:
    case clang::BuiltinType::UChar:
      return makeType<Primitive>(ty, Primitive::Kind::UInt8);
    case clang::BuiltinType::WChar_U:
      return makeType<Primitive>(ty, Primitive::Kind::UInt32);

    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::SChar:
      return makeType<Primitive>(ty, Primitive::Kind::Int8);
    case clang::BuiltinType::WChar_S:
      return makeType<Primitive>(ty, Primitive::Kind::Int32);
    case clang::BuiltinType::Char16:
      return makeType<Primitive>(ty, Primitive::Kind::Int16);
    case clang::BuiltinType::Char32:
      return makeType<Primitive>(ty, Primitive::Kind::Int32);

    case clang::BuiltinType::UShort:
      return makeType<Primitive>(ty, Primitive::Kind::UInt16);
    case clang::BuiltinType::UInt:
      return makeType<Primitive>(ty, Primitive::Kind::UInt32);
    case clang::BuiltinType::ULong:
      return makeType<Primitive>(ty, Primitive::Kind::UInt64);
    case clang::BuiltinType::ULongLong:
      return makeType<Primitive>(ty, Primitive::Kind::Int64);

    case clang::BuiltinType::Short:
      return makeType<Primitive>(ty, Primitive::Kind::Int16);
    case clang::BuiltinType::Int:
      return makeType<Primitive>(ty, Primitive::Kind::Int32);
    case clang::BuiltinType::Long:
    case clang::BuiltinType::LongLong:
      return makeType<Primitive>(ty, Primitive::Kind::Int64);

    case clang::BuiltinType::Float:
      return makeType<Primitive>(ty, Primitive::Kind::Float32);
    case clang::BuiltinType::Double:
    case clang::BuiltinType::LongDouble:
      return makeType<Primitive>(ty, Primitive::Kind::Float64);

    case clang::BuiltinType::UInt128:
    case clang::BuiltinType::Int128:
    default:
      throw std::logic_error(std::string("unsupported BuiltinType::Kind"));
  }
}

bool ClangTypeParser::chasePointer() const {
  // Always chase top-level pointers
  if (depth_ == 1)
    return true;
  return options_.chaseRawPointers;
}

ContainerInfo* ClangTypeParser::getContainerInfo(
    const std::string& fqName) const {
  for (const auto& containerInfo : containers_) {
    if (containerInfo->matches(fqName)) {
      return containerInfo.get();
    }
  }
  return nullptr;
}

namespace {

bool requireCompleteType(clang::Sema& sema, const clang::Type& ty) {
  switch (ty.getTypeClass()) {
    case clang::Type::Builtin: {
      if (ty.isSpecificBuiltinType(clang::BuiltinType::Void))
        return true;  // treat as complete
      break;
    }
    case clang::Type::IncompleteArray:
      return false;  // would fail completion
    default:
      break;
  }

  // TODO: This is a terrible warning.
  return !sema.RequireCompleteType(
      sema.getASTContext().getTranslationUnitDecl()->getEndLoc(),
      clang::QualType{&ty, 0},
      clang::diag::warn_nsconsumed_attribute_mismatch);
}

}  // namespace
}  // namespace oi::detail::type_graph
