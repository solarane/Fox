////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : DeclRecorder.hpp											
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
// DeclRecorder acts as a Declaration Recorder. While parsing, the 
// parser "registers" every declaration in the most recent declaration recorder found.
// DeclRecorder assists during name resolution to find members of a unit, or just to find if a
// identifier is linked to one or more declaration in a specific place of the AST.
////------------------------------------------------------////

#pragma once

#include <map>
#include <vector>

namespace Moonshot
{
	class ASTDecl;
	class ASTNamedDecl;
	class IdentifierInfo;
	class LookupResult;
	class DeclRecorder
	{
		private:
			using NamedDeclsMapTy = std::multimap<IdentifierInfo*, ASTNamedDecl*>;
			using NamedDeclsMapIter = NamedDeclsMapTy::iterator;
			using NamedDeclsMapConstIter = NamedDeclsMapTy::const_iterator;
		public:
			DeclRecorder(DeclRecorder * parent = nullptr);

			// "Record" a declaration within this DeclRecorder
			void recordDecl(ASTNamedDecl* decl);

			// Searches for every NamedDecl whose identifier == id in this DeclRecorder
			LookupResult restrictedLookup(IdentifierInfo *id) const;

			// Performs a restrictedLookup on this DeclRecorder and recursively searches parent
			// DeclRecorders.
			LookupResult fullLookup(IdentifierInfo *id) const;

			// Manage parent decl recorder
			bool hasParentDeclRecorder() const;
			DeclRecorder* getParentDeclRecorder();
			void setParentDeclRecorder(DeclRecorder *dr);
			void resetParentDeclRecorder();

			// Get information
			std::size_t getNumberOfRecordedDecls()  const;

			NamedDeclsMapIter recordedDecls_begin();
			NamedDeclsMapIter recordedDecls_end();

			NamedDeclsMapConstIter recordedDecls_begin() const;
			NamedDeclsMapConstIter recordedDecls_end() const;
		private:
			// Pointer to the Declaration Recorder "above" this one.
			DeclRecorder * parent_ = nullptr;
			NamedDeclsMapTy namedDecls_;
	};

	// A Class that encapsulates a lookup result.
	// All NamedDecls stored here are assumed to have the same IdentifierInfo.
	// an assertion in addResult ensures this.
	class LookupResult
	{
		private:
			using ResultVecTy = std::vector<ASTDecl*>;
			using ResultVecIter = ResultVecTy::iterator;
			using ResultVecConstIter = ResultVecTy::const_iterator;
		public:
			LookupResult();

			// Returns false if this LookupResult is empty.
			bool isEmpty() const;

			// Returns true if this LookupResult contains only one result.
			bool isUnique() const;

			// If this LookupResult contains only one result, returns it, else, returns a nullptr.
			ASTNamedDecl* getResultIfUnique() const;

			// Returns true if this LookupResult contains at least one function declaration.
			bool containsFunctionDecls() const;

			// Returns true if this LookupResult contains only function declarations.
			bool containsVarDecl() const;

			// Iterates over this LookupResult's results and checks if every single one of them
			// is a function declaration.
				// Note: Since every NamedDecl has the same name in a LookupResult, if they're all functions, that means it's an "overload set"
			bool onlyContainsFunctionDecls() const;

			operator bool() const;
		protected:
			friend class DeclRecorder;

			// Add another lookup result.
			void addResult(ASTNamedDecl* decl);
			// Clear this LookupResult
			void clear();
			// Copies all of the results from target into this lookupresult then clears the target.
			void merge(LookupResult &target);
		private:
			std::vector<ASTNamedDecl*> results_;

			// Theses flags are set to keep track of the "diversity" of named decls contained in this lookupresult.
			bool containsFuncDecl_		: 1;	// Contains at least 1 func decl
			bool containsVarDecl_		: 1;	// Contains at least 1 var decl
	};
}