//----------------------------------------------------------------------------//
// Part of the Fox project, licensed under the MIT license.
// See LICENSE.txt in the project root for license information.      
// File : SemaLookup.cpp                    
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//
//  This file implements the name lookup logic
//----------------------------------------------------------------------------//

#include "Fox/Sema/Sema.hpp"
#include "Fox/AST/ASTContext.hpp"
#include "Fox/AST/BuiltinTypeMembers.hpp"
#include "Fox/AST/DeclContext.hpp"
#include "Fox/AST/Decl.hpp"
#include <functional>
#include <algorithm>

using namespace fox;

//----------------------------------------------------------------------------//
// Unqualified Lookup
//----------------------------------------------------------------------------//

namespace {
  /// Removes shadowed declarations from a set of declarations.
  /// Shadowing is only allowed in 2 situations in Fox:
  ///  1) when a parameter shadows a global declaration
  ///  2) when a local variable shadows a global variable or a parameter.
  void removeShadowedDecls(SmallVectorImpl<NamedDecl*>& decls) {
    // FIXME: I think this can be made more efficient.

    // Collect the local decls (excluding param decls) and param decls.
    SmallVector<NamedDecl*, 4> paramDecls, localDecls;
    for (NamedDecl* decl : decls) {
      if (decl->isLocal()) {
        if(isa<ParamDecl>(decl))
          paramDecls.push_back(decl);
        else 
          localDecls.push_back(decl);
      }
    }
    
    if (localDecls.size()) {
      // if we found at least one local non-param decl, use them as the result
      // since they always have priority.
      decls = localDecls;
    }
    else if (paramDecls.size()) {
      // Now, if we didn't find any local non-param decls, but we found 
      // ParamDecls, use them as the result since they now have priority.
      decls = paramDecls;
    }
    // Else, the vector only contained global decls, so we can't change
    // anything.
  }
}

//----------------------------------------------------------------------------//
// Sema
//----------------------------------------------------------------------------//

void Sema::doUnqualifiedLookup(LookupResult& results, Identifier id,
  SourceLoc loc, const LookupOptions& options) {
  assert((results.size() == 0) && "'results' must be a fresh LookupResult");
  assert(id && "can't lookup with invalid id!");

  // If we find a VarDecl that's currently being checked, it's ignored and
  // stored in "checkingVar". If we finish lookup and we still find nothing,
  // we return checkingVar.
  //
  // This is needed to allow cases such as
  //  func foo(x : int) {
  //    var x : int = x; // x binds to the Parameter, not the variable.
  //  }
  // 
  // And checkingVar is still returned when nothing is found so cases such as
  //  let x : int = x;
  // can still be diagnosed correctly.

  NamedDecl* checkingVar = nullptr;

  bool onlyLocalDCs = options.onlyLookInLocalDeclContexts;

  // Helper lambda that returns true if a lookup result should be ignored.
  auto shouldIgnore = [&](NamedDecl* decl) {
    auto fn = options.shouldIgnore;
    return fn ? fn(decl) : false;
  };

  // Check in decl context if allowed to
  DeclContext* currentDeclContext = getDeclCtxt();
  // We should ALWAYS have an active DC, else something's broken.
  assert(currentDeclContext 
    && "No DeclContext available?");

  auto handleResult = [&](NamedDecl* decl) {
    // If we should ignore this result, do so and continue looking.
    if(shouldIgnore(decl)) return;

    // If this decl is being checked, set checkingvar and ignore it.
    if (decl->isChecking()) {
      assert(!checkingVar && "2 vars are already in the 'checking' state");
      checkingVar = decl;
      return;
    }
    
    // If not, add the decl to the results and continue looking
    results.addResult(decl);
  };

  auto shouldLookIn = [&](DeclContext* dc) {
    if(onlyLocalDCs)
      return dc->isLocal();
    return true;
  };

  // We're going to iterate over each parent in the DeclContext
  // hierarchy, and stop when we're satisfied with the results.
  while (currentDeclContext) {
    if (shouldLookIn(currentDeclContext)) {
      // The SourceLoc only matters when looking inside the currently
      // active DeclContext.
      SourceLoc theLoc = 
        (currentDeclContext == getDeclCtxt()) ? loc : SourceLoc();

      // Perform the lookuo
      currentDeclContext->lookup(id, theLoc, handleResult);
    
      // Check if we're satisfied with the results. If we are,
      // we'll stop the search here.
      if (results.size()) {
        // If the current decl context is a local context,
        // and we found results inside it, stop here to only
        // keep local results.
        if(currentDeclContext->isLocal()) break;
      }
    }

    // Search in the parent of this DC
    currentDeclContext = currentDeclContext->getParentDeclCtxt();
  }

  // Add the checkingVar to the result set if it's empty.
  if(results.isEmpty() && checkingVar)
    results.addResult(checkingVar);

  // Lookup builtin functions
  {
    SmallVector<BuiltinFuncDecl*, 8> builtinResults;
    ctxt.lookupBuiltin(id, builtinResults);
    for(auto builtin : builtinResults)
      results.addResult(builtin);
  }

  // Remove shadowed decls from the results set.
  if(results.size() > 1)
    removeShadowedDecls(results.getDecls());
}

//----------------------------------------------------------------------------//
// Sema::LookupResult 
//----------------------------------------------------------------------------//

void Sema::LookupResult::addResult(NamedDecl* decl) {
  results_.push_back(decl);
}

NamedDeclVec& Sema::LookupResult::getDecls() {
  return results_;
}

const NamedDeclVec& Sema::LookupResult::getDecls() const {
  return results_;
}

std::size_t Sema::LookupResult::size() const {
  return results_.size();
}

NamedDecl* Sema::LookupResult::getIfSingleResult() const {
  if(results_.size() == 1)
    return results_[0];
  return nullptr;
}

bool Sema::LookupResult::isEmpty() const {
  return (size() == 0);
}

bool Sema::LookupResult::isAmbiguous() const {
  return (size() > 1);
}

NamedDeclVec::iterator Sema::LookupResult::begin() {
  return results_.begin();
}

NamedDeclVec::const_iterator Sema::LookupResult::begin() const {
  return results_.begin();
}

NamedDeclVec::iterator Sema::LookupResult::end() {
  return results_.end();
}

NamedDeclVec::const_iterator Sema::LookupResult::end() const {
  return results_.end();
}

//----------------------------------------------------------------------------//
// Builtin Type Members Lookup
//----------------------------------------------------------------------------//

/// Searches for string builtin members with the identifier "id"
void Sema::lookupStringMember(SmallVectorImpl<BuiltinTypeMemberKind>& results,
                              Identifier id) {
  // FIXME: A chain of ifs is suboptimal (but it shouldn't really be a
  //        problem here due to the relatively small amount of entries)
  //        lookupArrayMember below has the same problem.
  string_view str = id.getStr();
  #define STRING_MEMBER(ID, FOX)\
    if(str == #FOX) results.push_back(BuiltinTypeMemberKind::ID);
  #include "Fox/AST/BuiltinTypeMembers.def"
}

/// Searches for array builtin members with the identifier "id"
void Sema::lookupArrayMember(SmallVectorImpl<BuiltinTypeMemberKind>& results,
                             Identifier id) {
  string_view str = id.getStr();
  #define ARRAY_MEMBER(ID, FOX)\
    if(str == #FOX) results.push_back(BuiltinTypeMemberKind::ID);
  #include "Fox/AST/BuiltinTypeMembers.def"
}