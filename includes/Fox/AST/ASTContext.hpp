//----------------------------------------------------------------------------//
// This file is a part of The Moonshot Project.        
// See the LICENSE.txt file at the root of the project for license information.            
// File : ASTContext.hpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//
// Contains the ASTContext class
//----------------------------------------------------------------------------//

// Improvements ideas:
//  > Remove the DeclContext.hpp include. It shouldn't really be there.
//    If I manage to remove it, I can also remove the <list> include and
//    DeclContext friendship

#pragma once

#include <map>
#include <set>
#include <list>
#include "Type.hpp"
#include "Identifier.hpp"
#include "ASTFwdDecl.hpp"
#include "Fox/Common/LinearAllocator.hpp"
#include "Fox/Common/string_view.hpp"
#include "Fox/AST/DeclContext.hpp"

namespace fox {
	class DiagnosticEngine;
	class SourceManager;

  // The ASTContext is pretty much the centerpiece of the AST. It owns
  // the allocators used to allocate the AST, keeps track of type singletons,
  // of the UnitDecls, etc.
  //
  // The ASTContext also contains a reference to the "attached" DiagnosticEngine
  // and SourceManagers.
  class ASTContext {
    public:
      ASTContext(SourceManager& srcMgr, DiagnosticEngine& diags);

      UnitDecl* getMainUnit();
      const UnitDecl* getMainUnit() const;
      void setUnit(UnitDecl* decl);

      // ALLOCATORS
      LinearAllocator<>& getAllocator();

      // Resets the ASTContext, freeing the AST and
      // everything allocated within it's allocators.
      void reset();

			// Returns the unique, ASTContext-owned version of the identifier "str"
			Identifier getIdentifier(const std::string& str);

      // Allocates a copy of "str" inside the main allocator.
      string_view allocateCopy(string_view str);

      // Shortcut for diagEngine.getErrorCount() != 0
      bool hadErrors() const;

      SourceManager& sourceMgr;
      DiagnosticEngine& diagEngine;

    private:
      // The ASTContext shouldn't be copyable.
      ASTContext(const ASTContext&) = delete;
      ASTContext& operator=(const ASTContext&) = delete;

      friend class ArrayType;
      friend class LValueType;
      friend class ErrorType;
      friend class PrimitiveType;
      friend class DeclContext;

      // The DeclData instance for every DeclContext in the AST.
      std::list<DeclContext::DeclData> declContextDatas_;

      // Map of Array types (Type -> Type[]) 
      // (managed by ArrayType::get)
      std::map<Type, ArrayType*> arrayTypes;

      // LValue types (Type -> @Type)
      // (managed by LValueType::get)
      std::map<Type, LValueType*> lvalueTypes;

      // Singleton/unique types. Lazily
      // created by their respective classes.
      ErrorType* theErrorType = nullptr;
      PrimitiveType* theIntType = nullptr;
      PrimitiveType* theFloatType = nullptr;
      PrimitiveType* theCharType = nullptr;
      PrimitiveType* theBoolType = nullptr;
      PrimitiveType* theStringType = nullptr;
      PrimitiveType* theVoidType = nullptr;

      UnitDecl* theUnit_ = nullptr;

			// The unique identifiers strings set
			std::set<std::string> idents_;

      // Allocators
      LinearAllocator<> allocator_; // Default allocator
  };
}
