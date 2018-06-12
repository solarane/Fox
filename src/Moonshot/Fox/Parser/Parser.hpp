////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : Parser.hpp											
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
// This file implements the recursive descent parser.		
// The parser is implemented as a set of functions, each	
// "parseXXX" method represents a rule in the grammar.				
//															
// The grammar can be found in	/doc/																		
//
// Note :	I've dug a lot into CLang's parser to try and see how to handle complex cases with elegance,
//			so some stuff (algorithms, names) you'll see here looks a lot like clang's parser, simplified of course.
//			Again, I don't think that's an issue, but here's CLang's license anyways : 
// 
// Status: Up to date with latest grammar changes, except import/using rules that aren't implemented yet.
//
//		Potential Areas of improvement
//			Recovery Efficiency
//				> Tweak it by running different test situations and adding special recovery cases wherever needed.
//				> Find flaws in the current system and fix them!
//			Speed
//				> Not for now. I'm going to make it work, make it right, then (maybe) make it fast, but in the future I think I can do some token peekings in some functions
//					to avoid calling a parse function.
// 
//		Parser "to-do" list. Important stuff is marked with (*)
//
//			Add better error recovey with common cases support in if/while parsing & function declaration
//
//			When SourceLoc system is added, match functions should return a SourceLoc instead, and a Invalid sourceloc if it doesn't match anything.
//			SourceLoc will need to overload operator bool(), which will check it's validity.
//				Also, Migrate every diag to the new diag system for the lexer, token & parser classes when SourceLoc is added, and delete the old context diag system.
////------------------------------------------------------////

#pragma once

#include "Moonshot/Fox/Lexer/Token.hpp"					
#include "Moonshot/Fox/AST/ASTContext.hpp"
#include "Moonshot/Fox/AST/Type.hpp"
#include "Moonshot/Fox/AST/Decl.hpp"
#include "Moonshot/Fox/AST/Expr.hpp"
#include "Moonshot/Fox/AST/Stmt.hpp"

#include <cassert>

namespace Moonshot
{
	class Context;
	class DeclRecorder;
	class Parser
	{
		public:
			/*-------------- Forward Declarations --------------*/
			template<typename DataTy>
			class Result;

			template<typename DataTy>
			class UniqueResult;

			/*-------------- Usings --------------*/

			// Bunch of usings & helper functions for parsing functions. Theses are public
			// so external classes can use them.
			using ExprResult = UniqueResult<Expr>;
			using ExprListResult = UniqueResult<ExprList>;
			using DeclResult = UniqueResult<Decl>;
			using StmtResult = UniqueResult<Stmt>;
			using UnitResult = UniqueResult<UnitDecl>;
		private:
			using TokenIteratorTy = TokenVector::iterator;
		public:
			// Note : the parser now takes an optional DeclRecorder* argument,
			// This will be used as the base DeclRecorder.
			Parser(Context& c,ASTContext& astctxt,TokenVector& l,DeclRecorder* dr = nullptr);

			/*-------------- Parsing Methods --------------*/
			// UNIT
			// Generally, this will be the entry point of the parsing process.
			// fid = The FileID of the file where the unit is contained.
			// unitName = the name of the unit that we're parsing. Usually, the name of the file.
			UnitResult parseUnit(const FileID& fid,IdentifierInfo* unitName);

			// EXPRESSIONS
			ExprListResult parseExprList();
			ExprListResult parseParensExprList();
			ExprResult parseParensExpr(const bool& isMandatory = false);
			ExprResult parseSuffix(std::unique_ptr<Expr> &base);
			ExprResult parseDeclRef();
			ExprResult parsePrimitiveLiteral();
			ExprResult parseArrayLiteral();
			ExprResult parseLiteral();
			ExprResult parsePrimary();
			ExprResult parseSuffixExpr();
			ExprResult parseExponentExpr();
			ExprResult parsePrefixExpr();
			ExprResult parseCastExpr();
			ExprResult parseBinaryExpr(const char &priority = 5);
			ExprResult parseExpr(); 

			// STATEMENTS
			StmtResult parseReturnStmt();
			StmtResult parseExprStmt();
			StmtResult parseCompoundStatement(const bool& isMandatory=false);
			StmtResult parseStmt();
			StmtResult parseBody();
			StmtResult parseCondition();
			StmtResult parseWhileLoop();

			// DECLS
			DeclResult parseArgDecl();
			DeclResult parseVarDecl();
			DeclResult parseFunctionDecl();
			DeclResult parseDecl();

			// OTHERS
			void enableTestMode();
			void disableTestMode();

			// Getters
			ASTContext& getASTContext();
			Context& getContext();
		private:
			/*-------------- Parser Setup --------------*/
			void setupParser();

			/*-------------- "Basic" Parse Methods --------------*/
			// Returns a nullptr if no type keyword is found
			Result<Type*> parseBuiltinTypename();	

			// first -> The Type* (nullptr if not found), second -> False if error
			Result<Type*> parseType();

			// Parses a QualType (Full Type Spec)
			Result<QualType> parseQualType();

			Result<binaryOperator> parseAssignOp();						// = 
			Result<unaryOperator>  parseUnaryOp();						// ! - +
			Result<binaryOperator> parseBinaryOp(const char &priority);	// + - * / % 
			SourceRange parseExponentOp();											//  **

			/*-------------- Token Consuming --------------*/
			/*	
				Consume methods all return a result that evaluates to true if the "consume" operation finished successfully 
				(found the requested token), false otherwise
			*/

			// Consumes an Identifier
			// The Result packs in the SourceRange of the identifier if one was found.
			Result<IdentifierInfo*> consumeIdentifier();

			// Consumes any sign but brackets.
			// Returns an invalid SourceLoc if not found, and returns a valid one if found.
			SourceLoc consumeSign(const SignType& s);

			// Consumes a bracket and keeps the bracket count up to date. Returns an invalid SourceLoc if the bracket was not found.
			// Note : In the US, a Bracket is a [], however, here the bracket noun is used in the strict sense, where Round B. = (), Square B. = [] and Curly B. = {}
			SourceLoc consumeBracket(const SignType& s);

			// Consumes a keyword. Returns false if the keyword was not found.
			SourceRange consumeKeyword(const KeywordType& k);

			// Dispatch to the appriate consume method. Won't return any loc information.
			void consumeAny();		

			// Skips one token
			void skipToken();

			// Revert the last consume operation
			void revertConsume();	

			// Helper for consumeSign & consumeBracket
			// Brackets are one of the following : '(' ')' '[' ']' '{' '}'
			bool isBracket(const SignType& s) const;

			Token getCurtok() const;
			Token getPreviousToken() const;
			
			/*-------------- Error Recovery --------------*/
				// Last Parameter is an optional pointer to a SourceRange. If the recovery was successful, the SourceRange of the token found
				// will be saved there.
			bool resyncToSign(const SignType& sign, const bool& stopAtSemi, const bool& shouldConsumeToken);
			bool resyncToSign(const std::vector<SignType>& signs, const bool& stopAtSemi, const bool& shouldConsumeToken);
			bool resyncToNextDecl();

			/*-------------- Error Reporting --------------*/
			void errorUnexpected();
			void errorExpected(const std::string &s);
			void genericError(const std::string &s); 

			// Returns (state_.tokenIterator == state_.lastUnexpectedTokenIt)
			bool isCurrentTokenLastUnexpectedToken() const;
			// Sets state_.lastUnexpectedTokenIt
			void markAsLastUnexpectedToken(TokenIteratorTy it);

			/*-------------- Parser State --------------*/
			struct ParserState
			{
				ParserState();

				// The current token
				TokenIteratorTy tokenIterator;

				// The last token that was the target of the "unexpected token" error.
				// This is saved to avoid printing the "unexpected token x" multiple time for the same token.
				TokenIteratorTy lastUnexpectedTokenIt;

				bool isAlive : 1;				// This is set to false when the parser dies (gives up)
				bool isRecoveryAllowed : 1;
			
				// Brackets counters
				uint8_t curlyBracketsCount  = 0;
				uint8_t roundBracketsCount  = 0;
				uint8_t squareBracketsCount	= 0;

				// Current Decl Recorder
				DeclRecorder *declRecorder = nullptr;
			} state_;

			// Interrogate state_
				// isDone returns false if( (state_.tokenIterator == tokens_.end) or !isAlive())
			bool isDone() const;
				// Returns state_.isAlive
			bool isAlive() const;
				// Kills Parsing (stops it)
			void die();

			// Register a declaration in state_.declRecorder, asserting that it's not null.
			void recordDecl(NamedDecl *nameddecl);

			// Creates a state_ backup
			ParserState createParserStateBackup() const;
			// Restores state_ from a backup.
			void restoreParserStateFromBackup(const ParserState& st);

			/*-------------- RAIIRecoveryManager --------------*/
				// This class manages the recovery of the parser
				// The constructor makes a backup of the parser instance's state_.isRecoveryAllowed variable, and replaces state_.isRecoveryAllowed with the value desired.
				// The constructor restores the state_.isRecoveryAllowed variable to it's original value using the backup.
				// Note: It's currently unused, but it's a class that could one day prove useful, so i'm keeping it for now.
			class RAIIRecoveryManager
			{
				public:
					explicit RAIIRecoveryManager(Parser &parser,const bool& allowsRecovery);
					~RAIIRecoveryManager();
				private:
					Parser &parser_;
					bool recoveryAllowedBackup_ : 1;
			};

			RAIIRecoveryManager createRecoveryEnabler();
			RAIIRecoveryManager createRecoveryDisabler();
			/*-------------- RAIIDeclRecorder --------------*/
			// This class sets the current DeclRecorder at construction, and restores the last
			// one at destruction.
			// If the DeclRecorder that was here before isn't null, it's marked as being the parent of the DeclRecorder passed as argument to the constructor.
			// It assists in registering Decl in the appropriate DeclRecorder.
			class RAIIDeclRecorder
			{
				public:
					RAIIDeclRecorder(Parser &p,DeclRecorder *dr);
					~RAIIDeclRecorder();
				private:
					Parser & parser;
					DeclRecorder * old_dc = nullptr;
			};

			/*-------------- Member Variables --------------*/
			ASTContext& astcontext_;
			Context& context_;
			TokenVector& tokens_;
			bool isTestMode_ : 1;
			
			/*-------------- Constants --------------*/
			static constexpr uint8_t kMaxBraceDepth = (std::numeric_limits<uint8_t>::max)();

		public:
			/*-------------- Result Classes --------------*/
			// Class for encapsulating a parsing function's result.
			// It also stores a SourceRange to store a Position/Range if needed.
			template<typename DataTy>
			class Result
			{
				public:
					Result(DataTy res, const SourceRange& range = SourceRange()) : result_(res), hasData_(true), successFlag_(true), range_(range)
					{

					}

					Result(const bool& wasSuccessful = true) : hasData_(false), successFlag_(wasSuccessful)
					{

					}

					operator bool() const
					{
						return isUsable();
					}

					bool wasSuccessful() const
					{
						return successFlag_;
					}

					bool isUsable() const
					{
						return successFlag_ && hasData_;
					}

					DataTy get() const
					{
						return result_;
					}

					// Helper methods 
					static Result<DataTy> Error()
					{
						return Result<DataTy>(false);
					}

					static Result<DataTy> NotFound()
					{
						return Result<DataTy>(true);
					}

					SourceRange getSourceRange() const
					{
						return range_;
					}

				private:
					SourceRange range_;
					bool hasData_ : 1;
					bool successFlag_ : 1;
					DataTy result_;
			};

			// Result class that holds it's information as a unique_ptr.
			template<typename DataTy>
			class UniqueResult
			{
				public:
					UniqueResult(std::unique_ptr<DataTy> res) : result_(std::move(res)), successFlag_(true)
					{

					}

					UniqueResult(const bool& wasSuccessful = true) : result_(nullptr), successFlag_(wasSuccessful)
					{

					}

					operator bool() const
					{
						return isUsable();
					}

					bool wasSuccessful() const
					{
						return successFlag_;
					}

					bool isUsable() const
					{
						return successFlag_ && result_;
					}

					// Helper methods 
					static UniqueResult<DataTy> Error()
					{
						return UniqueResult<DataTy>(false);
					}

					static UniqueResult<DataTy> NotFound()
					{
						return UniqueResult<DataTy>(true);
					}

					// Moves the content of the ParsingResult as a derived class.
					// Asserts that result_ isn't null, check with is() or isUsable() before using if you're unsure.
					// Note that the type is asserted to be castable to the desired type.
					template<typename Derived>
					std::unique_ptr<Derived> moveAs()
					{
						assert(result_ && "Result was null, or has already been moved!");
						Derived *ptr = dynamic_cast<Derived*>(result_.get());
						assert(ptr && "Can't cast to desired type");
						result_.release();
						return std::unique_ptr<Derived>(ptr);
					}

					// Classic move. Asserts that result_ isn't null, check with is() or isUsable() before using if you're unsure.
					std::unique_ptr<DataTy> move()
					{
						assert(result_ && "Result was null, or has already been moved!");
						return std::move(result_);
					}

					// Checks that Derived can be cast to the correct type.
					template<typename Derived>
					bool is() const
					{
						return (bool)(dynamic_cast<Derived*>(result_.get()));
					}
				private:
					bool successFlag_ : 1;
					std::unique_ptr<DataTy> result_;
			};
	};
}