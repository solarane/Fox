////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : ParseStmt.cpp										
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
//			SEE HEADER FILE FOR MORE INFORMATION			
//	This file implements statements rules. parseStmt, parseVarDeclstmt,etc.									
////------------------------------------------------------////

#include "Parser.hpp"

using namespace fox;

Parser::StmtResult Parser::parseCompoundStatement(bool isMandatory)
{
	auto rtr = std::make_unique<CompoundStmt>(); // return value
	auto leftCurlyLoc = consumeBracket(SignType::S_CURLY_OPEN);

	if (!leftCurlyLoc)
	{
		if (isMandatory)
		{
			reportErrorExpected(DiagID::parser_expected_opening_curlybracket);
			return StmtResult::Error();
		}
		return StmtResult::NotFound();
	}

	SourceLoc rightCurlyLoc;
	while (!isDone())
	{
		if (rightCurlyLoc = consumeBracket(SignType::S_CURLY_CLOSE))
			break;

		// try to parse a statement
		if(auto stmt = parseStmt())
		{
			// Push only if we don't have a standalone NullStmt
			// this is done to avoid stacking them up, and since they're a no-op in all cases
			// it's meaningless to ignore them.
			if (!stmt.is<NullStmt>())
				rtr->addStmt(std::move(stmt.move()));
		}
		// failure
		else
		{
			/*
				// if not found, report an error
				if (stmt.wasSuccessful())
					errorExpected("Expected a Statement");
			*/
			// In both case, attempt recovery to nearest semicolon.
			if (resyncToSign(SignType::S_SEMICOLON,/*stopAtSemi -> meaningless here*/ false, /*shouldConsumeToken*/ true))
				continue;
			else
			{
				// If we couldn't recover, try to recover to our '}' to stop parsing this compound statement
				if (resyncToSign(SignType::S_CURLY_CLOSE, /*stopAtSemi*/ false, /*consume*/ false))
				{
					rightCurlyLoc = consumeBracket(SignType::S_CURLY_CLOSE);
					break;
				}
				else
					return StmtResult::Error();
			}
		}
	}

	if (!rightCurlyLoc.isValid())
	{
		reportErrorExpected(DiagID::parser_expected_closing_curlybracket);
		// We can't recover since we probably reached EOF. return an error!
		return StmtResult::Error();
	}

	// if everything's alright, return the result
	rtr->setSourceLocs(leftCurlyLoc, rightCurlyLoc);
	assert(rtr->hasLocInfo() && "No loc info? But we just set it!");
	return StmtResult(std::move(rtr));
}

Parser::StmtResult Parser::parseWhileLoop()
{
	// <while_loop>  = "while"	<parens_expr> <body>
	// "while"
	auto whKw = consumeKeyword(KeywordType::KW_WHILE);
	if (!whKw)
		return StmtResult::NotFound();

	std::unique_ptr<Expr> expr;
	std::unique_ptr<Stmt> body;
	SourceLoc parenExprEndLoc;
	SourceLoc begLoc = whKw.getBeginSourceLoc();
	SourceLoc endLoc;
	// <parens_expr>
	if (auto parens_expr_res = parseParensExpr(/* The ParensExpr is mandatory */ true, nullptr, &parenExprEndLoc))
	{
		assert(parenExprEndLoc && "parseParensExpr didn't provide the ')' loc?");
		expr = parens_expr_res.move();
	}
	else
		return StmtResult::Error();

	// <body>
	if (auto body_res = parseBody())
	{
		body = body_res.move();
		endLoc = body->getEndLoc();
		assert(endLoc && "The body parsed successfully, but doesn't have a valid endLoc?");
	}
	else
	{
		if (body_res.wasSuccessful())
			reportErrorExpected(DiagID::parser_expected_stmt);

		return StmtResult::Error();
	}

	assert(expr && body && begLoc && endLoc && parenExprEndLoc);
	return StmtResult(std::make_unique<WhileStmt>(
			std::move(expr),
			std::move(body),
			begLoc,
			parenExprEndLoc,
			endLoc
		));
}

Parser::StmtResult Parser::parseCondition()
{
	// <condition>	= "if"	<parens_expr> <body> ["else" <body>]
	std::unique_ptr<Expr> expr;
	std::unique_ptr<Stmt> ifBody, elseBody;

	// "if"
	auto ifKw = consumeKeyword(KeywordType::KW_IF);
	if (!ifKw)
	{
		// check for a else without if
		if (auto elseKw = consumeKeyword(KeywordType::KW_ELSE))
		{
			diags_.report(DiagID::parser_else_without_if, elseKw);
			return StmtResult::Error();
		}
		return StmtResult::NotFound();
	}

	SourceLoc begLoc = ifKw.getBeginSourceLoc();
	SourceLoc ifHeadEndLoc;
	// <parens_expr>
	if (auto parensexpr = parseParensExpr(/* The ParensExpr is mandatory */ true, nullptr, &ifHeadEndLoc))
		expr = parensexpr.move();
	else
		return StmtResult::Error();
		
	SourceLoc endLoc;
	// <body>
	if (auto body = parseBody())
	{
		ifBody = body.move();
		endLoc = ifBody->getEndLoc();
	}
	else
	{
		if (body.wasSuccessful())
			reportErrorExpected(DiagID::parser_expected_stmt);

		return StmtResult::Error();
	}

	// "else"
	if (consumeKeyword(KeywordType::KW_ELSE))
	{
		// <body>
		if (auto body = parseBody())
		{
			elseBody = body.move();
			endLoc = elseBody->getEndLoc();
		}
		else
		{
			if(body.wasSuccessful())
				reportErrorExpected(DiagID::parser_expected_stmt);
			return StmtResult::Error();
		}
	}

	assert(begLoc && endLoc && ifHeadEndLoc && "Incomplete loc info");

	return StmtResult(std::make_unique<ConditionStmt>(
			std::move(expr),
			std::move(ifBody),
			std::move(elseBody),
			begLoc, 
			ifHeadEndLoc,
			endLoc
		));
}

Parser::StmtResult Parser::parseReturnStmt()
{
	// <rtr_stmt> = "return" [<expr>] ';'
	// "return"
	auto rtrKw = consumeKeyword(KeywordType::KW_RETURN);
	if (!rtrKw)
		return StmtResult::NotFound();
	
	std::unique_ptr<Expr> expr;
	SourceLoc begLoc = rtrKw.getBeginSourceLoc();
	SourceLoc endLoc;

	// [<expr>]
	if (auto expr_res = parseExpr())
		expr = expr_res.move();
	else if(!expr_res.wasSuccessful())
	{
		// expr failed? try to resync if possible. 
		if (!resyncToSign(SignType::S_SEMICOLON, /* stopAtSemi */ false, /*consumeToken*/ true))
			return StmtResult::Error();
	}

	// ';'
	if (auto semi = consumeSign(SignType::S_SEMICOLON))
		endLoc = semi;
	else
	{
		reportErrorExpected(DiagID::parser_expected_semi);
		// Recover to semi, if recovery wasn't successful, return an error.
		if (!resyncToSign(SignType::S_SEMICOLON, /* stopAtSemi */ false, /*consumeToken*/ true))
			return StmtResult::Error();
	}
		
	assert(begLoc && endLoc);
	return StmtResult(std::make_unique<ReturnStmt>(
			std::move(expr),
			begLoc, 
			endLoc
		));
}

Parser::StmtResult Parser::parseStmt()
{
	// <stmt>	= <var_decl> | <expr_stmt> | <condition> | <while_loop> | <rtr_stmt> 

	// <var_decl
	if (auto vardecl = parseVarDecl())
		return StmtResult(std::make_unique<DeclStmt>(vardecl.move()));
	else if (!vardecl.wasSuccessful())
		return StmtResult::Error();

	// <expr_stmt>
	if (auto exprstmt = parseExprStmt())
		return exprstmt;
	else if (!exprstmt.wasSuccessful())
		return StmtResult::Error();

	// <condition>
	if(auto cond = parseCondition())
		return cond;
	else if (!cond.wasSuccessful())
		return StmtResult::Error();

	// <while_loop>
	if (auto wloop = parseWhileLoop())
		return wloop;
	else if(!wloop.wasSuccessful())
		return StmtResult::Error();

	// <return_stmt>
	if (auto rtrstmt = parseReturnStmt())
		return rtrstmt;
	else if(!rtrstmt.wasSuccessful())
		return StmtResult::Error();

	return StmtResult::NotFound();
}

Parser::StmtResult Parser::parseBody()
{
	// <body>	= <stmt> | <compound_statement>

	// <stmt>
	if (auto stmt = parseStmt())
		return stmt;
	else if (!stmt.wasSuccessful())
		return StmtResult::Error();

	// <compound_statement>
	if (auto compoundstmt = parseCompoundStatement())
		return compoundstmt;
	else if (!compoundstmt.wasSuccessful())
		return StmtResult::Error();

	return StmtResult::NotFound();
}

Parser::StmtResult Parser::parseExprStmt()
{
	// <expr_stmt>	= ';' | <expr> ';' 	

	// ';'
	if (auto semi = consumeSign(SignType::S_SEMICOLON))
		return StmtResult(std::make_unique<NullStmt>(semi));

	// <expr> 
	else if (auto expr = parseExpr())
	{
		// ';'
		if (!consumeSign(SignType::S_SEMICOLON))
		{
			if (expr.wasSuccessful())
				reportErrorExpected(DiagID::parser_expected_semi);

			if (!resyncToSign(SignType::S_SEMICOLON, /* stopAtSemi */ false, /*consumeToken*/ true))
				return StmtResult::Error();
			// if recovery was successful, just return like nothing has happened!
		}

		return StmtResult(expr.move());
	}
	else if(!expr.wasSuccessful())
	{
		// if the expression had an error, ignore it and try to recover to a semi.
		if (resyncToSign(SignType::S_SEMICOLON, /* stopAtSemi */ false, /*consumeToken*/ false))
			return StmtResult(std::make_unique<NullStmt>(consumeSign(SignType::S_SEMICOLON)));
		return StmtResult::Error();
	}

	return StmtResult::NotFound();
}