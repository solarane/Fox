//----------------------------------------------------------------------------//
// This file is a part of The Moonshot Project.        
// See LICENSE.txt for license info.            
// File : Decl.cpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//

#include "Fox/AST/Expr.hpp"
#include "Fox/AST/Decl.hpp"
#include "Fox/AST/Stmt.hpp"
#include "Fox/Common/Source.hpp"
#include "Fox/AST/ASTContext.hpp"
#include <sstream>
#include <cassert>

using namespace fox;

//----------------------------------------------------------------------------//
// Decl
//----------------------------------------------------------------------------//
/*
#define DECL(ID, PARENT)\
  static_assert(std::is_trivially_destructible<ID>::value, \
  #ID " is allocated in the ASTContext: It's destructor is never called!");
#include "Fox/AST/DeclNodes.def"
*/
Decl::Decl(DeclKind kind, Parent parent, SourceRange range):
  kind_(kind), range_(range), parent_(parent), 
  checkState_(CheckState::Unchecked) {}

DeclKind Decl::getKind() const {
  return kind_;
}

DeclContext* Decl::getDeclContext() const {
  if(isParentNull()) return nullptr;
  if(DeclContext* ptr = parent_.dyn_cast<DeclContext*>())
    return ptr;
  return nullptr;
}

bool Decl::isLocal() const {
  return parent_.is<FuncDecl*>() && (!isParentNull());
}

FuncDecl* Decl::getFuncDecl() const {
  if(isParentNull()) return nullptr;
  if(FuncDecl* ptr = parent_.dyn_cast<FuncDecl*>())
    return ptr;
  return nullptr;
}

Decl::Parent Decl::getParent() const {
  return parent_;
}

bool Decl::isParentNull() const {
  return parent_.isNull();
}

DeclContext* Decl::getClosestDeclContext() const {
  if(auto* dc = dyn_cast<DeclContext>(const_cast<Decl*>(this)))
    return dc;
  if(auto* fn = getFuncDecl())
    return fn->getDeclContext();
  return getDeclContext();
}

void Decl::setRange(SourceRange range) {
  range_ = range;
}

SourceRange Decl::getRange() const {
  return range_;
}

SourceLoc Decl::getBegin() const {
  return range_.getBegin();
}

SourceLoc Decl::getEnd() const {
  return range_.getEnd();
}

bool Decl::isUnchecked() const {
  return (checkState_ == CheckState::Unchecked);
}

bool Decl::isChecked() const {
  return (checkState_ == CheckState::Checked);
}

Decl::CheckState Decl::getCheckState() const {
  return checkState_;
}

void Decl::markAsChecked() {
  checkState_ = CheckState::Checked;
}

FileID Decl::getFileID() const {
  return range_.getBegin().getFileID();
}

void* Decl::operator new(std::size_t sz, ASTContext& ctxt, std::uint8_t align) {
  return ctxt.getAllocator().allocate(sz, align);
}

//----------------------------------------------------------------------------//
// NamedDecl
//----------------------------------------------------------------------------//

NamedDecl::NamedDecl(DeclKind kind, Parent parent, Identifier id, 
  SourceRange idRange, SourceRange range): Decl(kind, parent, range),
  identifier_(id), identifierRange_(idRange), illegalRedecl_(false){}

Identifier NamedDecl::getIdentifier() const {
  return identifier_;
}

void NamedDecl::setIdentifier(Identifier id) {
  identifier_ = id;
}

void NamedDecl::setIdentifier(Identifier id, SourceRange idRange) {
  identifier_ = id;
  identifierRange_ = idRange;
}

bool NamedDecl::hasIdentifier() const {
  return !identifier_.isNull();
}

bool NamedDecl::isIllegalRedecl() const {
  return illegalRedecl_;
}

void NamedDecl::setIsIllegalRedecl(bool val) {
  illegalRedecl_ = val;
}

SourceRange NamedDecl::getIdentifierRange() const {
  return identifierRange_;
}

void NamedDecl::setIdentifierRange(SourceRange range) {
  identifierRange_ = range;
}

bool NamedDecl::hasIdentifierRange() const {
  return (bool)identifierRange_;
}

//----------------------------------------------------------------------------//
// ValueDecl
//----------------------------------------------------------------------------//

ValueDecl::ValueDecl(DeclKind kind, Parent parent, Identifier id, 
  SourceRange idRange, TypeLoc ty, bool isConst, SourceRange range): 
  NamedDecl(kind, parent, id, idRange, range), const_(isConst), type_(ty) {}

Type ValueDecl::getType() const {
  return type_.withoutLoc();
}

TypeLoc ValueDecl::getTypeLoc() const {
  return type_;
}

void ValueDecl::setTypeLoc(TypeLoc ty) {
  type_ = ty;
}

SourceRange ValueDecl::getTypeRange() const {
  return type_.getRange();
}

bool ValueDecl::isConst() const {
  return const_;
}

void ValueDecl::setIsConst(bool k) {
  const_ = k;
}

//----------------------------------------------------------------------------//
// ParamDecl
//----------------------------------------------------------------------------//

ParamDecl* ParamDecl::create(ASTContext& ctxt, FuncDecl* parent, 
  Identifier id, SourceRange idRange, TypeLoc type, bool isMutable,
  SourceRange range) {
  return new(ctxt) ParamDecl(parent, id, idRange, type, isMutable, range);
}

bool ParamDecl::isMutable() const {
  return !isConst();
}

ParamDecl::ParamDecl(FuncDecl* parent, Identifier id, SourceRange idRange, 
  TypeLoc type, bool isMutable, SourceRange range):
  ValueDecl(DeclKind::ParamDecl, parent, id, idRange, type, !isMutable, range) 
  {}

//----------------------------------------------------------------------------//
// ParamList
//----------------------------------------------------------------------------//

ParamList* ParamList::create(ASTContext& ctxt, ArrayRef<ParamDecl*> params) {
  auto& alloc = ctxt.getAllocator();
  auto totalSize = totalSizeToAlloc<ParamDecl*>(params.size());
  void* mem = alloc.allocate(totalSize, alignof(ParamDecl));
  return new(mem) ParamList(params);
}

ParamList::ParamList(ArrayRef<ParamDecl*> params) 
  : numParams_(static_cast<SizeTy>(params.size())) {
  assert((numParams_ < maxParams) && "Too many parameters for ParamList. "
    "Change the type of SizeTy to something bigger!");
  std::uninitialized_copy(params.begin(), params.end(), 
    getTrailingObjects<ParamDecl*>());
}

void* ParamList::operator new(std::size_t, void* mem) {
  assert(mem);
  return mem;
}

//----------------------------------------------------------------------------//
// FuncDecl
//----------------------------------------------------------------------------//

FuncDecl::FuncDecl(DeclContext* parent, Identifier fnId, SourceRange idRange,
  TypeLoc returnType, SourceRange range, SourceLoc headerEndLoc):
  NamedDecl(DeclKind::FuncDecl, parent, fnId, idRange, range), 
  headEndLoc_(headerEndLoc), returnType_(returnType) {}

FuncDecl* FuncDecl::create(ASTContext& ctxt, DeclContext* parent, Identifier id,
  SourceRange idRange, TypeLoc type, SourceRange range, SourceLoc headerEnd) {
  return new(ctxt) FuncDecl(parent, id, idRange, type, range, headerEnd);
}

void FuncDecl::setLocs(SourceRange range, SourceLoc headerEndLoc) {
  setRange(range);
  setHeaderEndLoc(headerEndLoc);
}

void FuncDecl::setHeaderEndLoc(SourceLoc loc) {
  headEndLoc_ = loc;
}

SourceLoc FuncDecl::getHeaderEndLoc() const {
  return headEndLoc_;
}

SourceRange FuncDecl::getHeaderRange() const {
  return SourceRange(getBegin(), headEndLoc_);
}

void FuncDecl::setReturnTypeLoc(TypeLoc ty) {
  returnType_ = ty;
}

TypeLoc FuncDecl::getReturnTypeLoc() const {
  return returnType_;
}

Type FuncDecl::getReturnType() const {
  return returnType_.withoutLoc();
}

SourceRange FuncDecl::getReturnTypeRange() const {
  return returnType_.getRange();
}

CompoundStmt* FuncDecl::getBody() const {
  return body_;
}

void FuncDecl::setBody(CompoundStmt* body) {
  body_ = body;
}

ParamDecl* FuncDecl::getParam(std::size_t ind) const {
  assert(ind < params_.size() && "out-of-range");
  return params_[ind];
}

ParamVec& FuncDecl::getParams() {
  return params_;
}

const ParamVec& FuncDecl::getParams() const {
  return params_;
}

void FuncDecl::addParam(ParamDecl* param) {
  params_.push_back(param);
}

void FuncDecl::setParam(ParamDecl* param, std::size_t idx) {
  assert(idx <= params_.size() && "Out of range");
  params_[idx] = param;
}

std::size_t FuncDecl::getNumParams() const {
  return params_.size();
}

//----------------------------------------------------------------------------//
// VarDecl
//----------------------------------------------------------------------------//

VarDecl::VarDecl(Parent parent, Identifier id, SourceRange idRange, 
  TypeLoc type, bool isConst, Expr* init, SourceRange range):
  ValueDecl(DeclKind::VarDecl, parent, id, idRange, type, isConst, range),
  init_(init) {}

VarDecl* VarDecl::create(ASTContext& ctxt, Parent parent, Identifier id,
  SourceRange idRange, TypeLoc type, bool isConst, Expr* init, 
  SourceRange range) {
  return new(ctxt) VarDecl(parent, id, idRange, type, isConst, init, range);
}

Expr* VarDecl::getInitExpr() const {
  return init_;
}

bool VarDecl::hasInitExpr() const {
  return (bool)init_;
}

bool VarDecl::isVar() const {
  return !isConst();
}

bool VarDecl::isLet() const {
  return isConst();
}

void VarDecl::setInitExpr(Expr* expr) {
  init_ = expr;
}

//----------------------------------------------------------------------------//
// UnitDecl
//----------------------------------------------------------------------------//

static SourceRange createRange(FileID file) {
  return SourceRange(SourceLoc(file));
}

UnitDecl::UnitDecl(ASTContext& ctxt, DeclContext* parent, Identifier id,
  FileID inFile): Decl(DeclKind::UnitDecl, parent, createRange(inFile)),
  identifier_(id), DeclContext(DeclContextKind::UnitDecl),
  ctxt_(ctxt) {}

UnitDecl* UnitDecl::create(ASTContext& ctxt, DeclContext* parent, 
  Identifier id, FileID file) {
  return new(ctxt) UnitDecl(ctxt, parent, id, file);
}

Identifier UnitDecl::getIdentifier() const {
  return identifier_;
}

void UnitDecl::setIdentifier(Identifier id) {
  identifier_ = id;
}

ASTContext& UnitDecl::getASTContext() {
  return ctxt_;
}
