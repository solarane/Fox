////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : LocParserTests.cpp											
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
//	Tests for the accuracy of Locations/Ranges of nodes generated by the Parser.
////------------------------------------------------------////

#include "gtest/gtest.h"
#include "Fox/Lexer/Lexer.hpp"
#include "Fox/Parser/Parser.hpp"
#include "Fox/Common/Source.hpp"
#include "Fox/Common/DiagnosticEngine.hpp"
#include "Fox/AST/ASTContext.hpp"
#include "Fox/AST/Identifiers.hpp"
#include "Support/TestUtils.hpp"

using namespace fox;

// Parser Preparator for LocTests
class LocTests : public ::testing::Test
{
	public:
		LocTests() : dg(&srcMgr)
		{

		}
	protected:
		virtual void SetUp(const std::string& filepath) 
		{
			fullFilePath = test::convertRelativeTestResPathToAbsolute(filepath);
			file = srcMgr.loadFromFile(fullFilePath);

			// If file couldn't be loaded, give us the reason
			if (!file)
			{
				FAIL() << "Couldn't load file \""<< filepath << "\" in memory.";
			}

			lexer = std::make_unique<Lexer>(dg, srcMgr, astContext);
			lexer->lexFile(file);

			if (dg.getErrorsCount())
			{
				FAIL() << "Lexing Error";
			}

			parser = std::make_unique<Parser>(dg, srcMgr , astContext, lexer->getTokenVector(), &declContext);
		}

		std::string fullFilePath;

		DiagnosticEngine dg;
		FileID file;
		SourceManager srcMgr;
		ASTContext astContext;
		DeclContext declContext;
		std::unique_ptr<Lexer> lexer;
		std::unique_ptr<Parser> parser;
};

TEST_F(LocTests, FuncAndArgDecl)
{
	SetUp("parser/loc/functions.fox");
	auto presult = parser->parseFunctionDecl();

	ASSERT_TRUE(presult) << "parsing error";
	auto func = presult.moveAs<FunctionDecl>();

	// First, test the function itself
	CompleteLoc func_beg = srcMgr.getCompleteLocForSourceLoc(func->getBegLoc());
	CompleteLoc func_head_end = srcMgr.getCompleteLocForSourceLoc(func->getHeaderEndLoc());
	CompleteLoc func_end = srcMgr.getCompleteLocForSourceLoc(func->getEndLoc());
	
	EXPECT_EQ(func_beg, CompleteLoc(fullFilePath, 1, 1));
	EXPECT_EQ(func_head_end, CompleteLoc(fullFilePath, 1, 56));
	EXPECT_EQ(func_end, CompleteLoc(fullFilePath, 4, 2));

	// Now, test the args
	// Arg count should be correct
	ASSERT_EQ(func->argsSize(), 2);

	// Extract each arg individually
	ArgDecl* arg1 = func->getArg(0);
	ArgDecl* arg2 = func->getArg(1);

	// Check if the names are right
	EXPECT_EQ(arg1->getIdentifier()->getStr(), "_bar1");
	EXPECT_EQ(arg2->getIdentifier()->getStr(), "_bar2");

	// Extract Arg locs
	#define BEG_LOC(x) srcMgr.getCompleteLocForSourceLoc(x->getBegLoc())
	#define END_LOC(x) srcMgr.getCompleteLocForSourceLoc(x->getEndLoc())
	
	auto arg1_beg = BEG_LOC(arg1);
	auto arg1_end = END_LOC(arg1);

	auto arg2_beg = BEG_LOC(arg2);
	auto arg2_end = END_LOC(arg2);

	#undef BEG_LOC
	#undef END_LOC

	EXPECT_EQ(arg1_beg, CompleteLoc(fullFilePath,1,10));
	EXPECT_EQ(arg1_end, CompleteLoc(fullFilePath,1,31));

	EXPECT_EQ(arg2_beg, CompleteLoc(fullFilePath, 1, 34));
	EXPECT_EQ(arg2_end, CompleteLoc(fullFilePath, 1, 45));

	// Extract arg type ranges
	auto arg1_typeRange = arg1->getTypeRange();
	auto arg2_typeRange = arg2->getTypeRange();

	// Extract locs
	auto arg1_tr_beg = srcMgr.getCompleteLocForSourceLoc(arg1_typeRange.getBeginSourceLoc());
	auto arg2_tr_beg = srcMgr.getCompleteLocForSourceLoc(arg2_typeRange.getBeginSourceLoc());

	// Check
	EXPECT_EQ(arg1_typeRange.makeEndSourceLoc(), arg1->getEndLoc());
	EXPECT_EQ(arg2_typeRange.makeEndSourceLoc(), arg2->getEndLoc());

	EXPECT_EQ(arg1_tr_beg, CompleteLoc(fullFilePath, 1, 18));
	EXPECT_EQ(arg2_tr_beg, CompleteLoc(fullFilePath, 1, 40));
}

// VarDecl test
TEST_F(LocTests, VarDecls)
{
	SetUp("parser/loc/vardecl.fox");
	auto presult = parser->parseVarDecl();

	ASSERT_TRUE(presult) << "parsing error";
	auto var = presult.moveAs<VarDecl>();

	CompleteLoc var_beg = srcMgr.getCompleteLocForSourceLoc(var->getBegLoc());
	CompleteLoc var_end = srcMgr.getCompleteLocForSourceLoc(var->getEndLoc());

	EXPECT_EQ(var_beg, CompleteLoc(fullFilePath,1,2));
	EXPECT_EQ(var_end, CompleteLoc(fullFilePath,1,25));

	CompleteLoc var_ty_beg = srcMgr.getCompleteLocForSourceLoc(var->getTypeRange().getBeginSourceLoc());
	CompleteLoc var_ty_end = srcMgr.getCompleteLocForSourceLoc(var->getTypeRange().makeEndSourceLoc());

	EXPECT_EQ(var_ty_beg, CompleteLoc(fullFilePath, 1, 10));
	EXPECT_EQ(var_ty_end, CompleteLoc(fullFilePath, 1, 20));

	CompleteLoc expr_beg = srcMgr.getCompleteLocForSourceLoc(var->getInitExpr()->getBegLoc());
	CompleteLoc expr_end = srcMgr.getCompleteLocForSourceLoc(var->getInitExpr()->getEndLoc());

	EXPECT_EQ(expr_beg, expr_end); // Since the expr is only a '3', it's only one char, thus beg = end.
	EXPECT_EQ(expr_beg, CompleteLoc(fullFilePath, 1, 24));
}

// ToDo : Same tests for Exprs and Stmts