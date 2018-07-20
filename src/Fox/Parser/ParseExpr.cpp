////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : ParseExpr.cpp										
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
//			SEE HEADER FILE FOR MORE INFORMATION			
// This file implements expressions related methods (rules)	
////------------------------------------------------------////

#include "Parser.hpp"

using namespace fox;

// note, here the unique_ptr is passed by reference because it will be consumed (moved -> nulled) only if a '[' is found, else, it's left untouched.
Parser::ExprResult Parser::parseSuffix(std::unique_ptr<Expr>& base)
{
	// <suffix> = '.' <id> | '[' <expr> ']' | <parens_expr_list>
	SourceLoc begLoc = base->getBegLoc();
	SourceLoc endLoc;
	// "." <id> 
	// '.'
	if (auto dotLoc = consumeSign(SignType::S_DOT))
	{
		// <id>
		if (auto id = consumeIdentifier())
		{
			// found, return
			endLoc = id.getSourceRange().makeEndSourceLoc();
			return ExprResult(
				std::make_unique<MemberOfExpr>(std::move(base),id.get(),begLoc,dotLoc,endLoc)
			);
		}
		else 
		{
			reportErrorExpected(DiagID::parser_expected_iden);
			return ExprResult::Error();
		}
	}
	// '[' <expr> ']
	// '['
	else if (consumeBracket(SignType::S_SQ_OPEN))
	{
		// <expr>
		if (auto expr = parseExpr())
		{
			// ']'
			endLoc = consumeBracket(SignType::S_SQ_CLOSE);
			if (!endLoc)
			{
				reportErrorExpected(DiagID::parser_expected_closing_squarebracket);

				if (resyncToSign(SignType::S_SQ_CLOSE, /* stopAtSemi */ true, /*consumeToken*/ false))
					endLoc = consumeBracket(SignType::S_SQ_CLOSE);
				else
					return ExprResult::Error();

			}

			return ExprResult(
				std::make_unique<ArrayAccessExpr>(
						std::move(base),
						expr.move(),
						begLoc,
						endLoc
					)
			);
		}
		else
		{
			if (expr.wasSuccessful())
				reportErrorExpected(DiagID::parser_expected_expr);

			// Resync. if Resync is successful, return the base as the result (don't alter it) to fake a success
			// , if it's not, return an Error.
			if (resyncToSign(SignType::S_SQ_CLOSE, /* stopAtSemi */ true, /*consumeToken*/ true))
				return ExprResult(std::move(base));
			else
				return ExprResult::Error();
		}
	}
	// <parens_expr_list>
	else if (auto exprlist = parseParensExprList(nullptr,&endLoc))
	{
		assert(endLoc && "parseParensExprList didn't complete the endLoc?");
		return ExprResult(std::make_unique<FunctionCallExpr>(
				std::move(base),
				exprlist.move(),
				begLoc,
				endLoc
			));
	}
	else if (!exprlist.wasSuccessful())
		return ExprResult::Error();
	return ExprResult::NotFound();
}

Parser::ExprResult Parser::parseDeclRef()
{
	// Note: this rule is quite simple and is essentially just a "wrapper"
	// however I'm keeping it for clarity, and future usages where DeclRef might get more complex, if it ever 
	// does

	// <decl_call> = <id> 
	if (auto id = consumeIdentifier())
		return ExprResult(std::make_unique<DeclRefExpr>(
				id.get(),
				id.getSourceRange().getBeginSourceLoc(),
				id.getSourceRange().makeEndSourceLoc()
			));
	return ExprResult::NotFound();
}

Parser::ExprResult Parser::parsePrimitiveLiteral()
{
	// <primitive_literal>	= One literal of the following type : Integer, Floating-point, Boolean, String, Char
	auto tok = getCurtok();
	if (!tok.isLiteral())
		return ExprResult::NotFound();
	
	increaseTokenIter();

	auto litinfo = tok.getLiteralInfo();
	std::unique_ptr<Expr> expr = nullptr;

	SourceLoc begLoc = tok.getRange().getBeginSourceLoc();
	SourceLoc endLoc = tok.getRange().makeEndSourceLoc();

	if (litinfo.isBool())
		expr = std::make_unique<BoolLiteralExpr>(litinfo.get<bool>(), begLoc, endLoc);
	else if (litinfo.isString())
		expr = std::make_unique<StringLiteralExpr>(litinfo.get<std::string>(), begLoc, endLoc);
	else if (litinfo.isChar())
		expr = std::make_unique<CharLiteralExpr>(litinfo.get<CharType>(), begLoc, endLoc);
	else if (litinfo.isInt())
		expr = std::make_unique<IntegerLiteralExpr>(litinfo.get<IntType>(), begLoc, endLoc);
	else if (litinfo.isFloat())
		expr = std::make_unique<FloatLiteralExpr>(litinfo.get<FloatType>(), begLoc, endLoc);
	else
		fox_unreachable("Unknown literal kind"); // Unknown literal

	return ExprResult(std::move(expr));
}

Parser::ExprResult Parser::parseArrayLiteral()
{
	// <array_literal>	= '[' [<expr_list>] ']'
	auto begLoc = consumeBracket(SignType::S_SQ_OPEN);
	if (!begLoc)
		return ExprResult::NotFound();
	
	std::unique_ptr<ExprList> expr;
	SourceLoc endLoc;
	// [<expr_list>]
	if (auto elist = parseExprList())
		expr = elist.move();
	// ']'
	if (!(endLoc = consumeBracket(SignType::S_SQ_CLOSE)))
	{
		// Resync. If resync wasn't successful, report the error.
		if (resyncToSign(SignType::S_SQ_CLOSE, /* stopAtSemi */ true, /*consumeToken*/ false))
			endLoc = consumeBracket(SignType::S_SQ_CLOSE);
		else
			return ExprResult::Error();
	}
	return ExprResult(
		std::make_unique<ArrayLiteralExpr>(std::move(expr),begLoc,endLoc)
	);
}

Parser::ExprResult Parser::parseLiteral()
{
	// <literal>	= <primitive_literal> | <array_literal>

	// <primitive_literal>
	if (auto prim = parsePrimitiveLiteral())
		return prim;
	else if (!prim.wasSuccessful())
		return ExprResult::Error();

	// <array_literal>
	if (auto arr = parseArrayLiteral())
		return arr;
	else if (!arr.wasSuccessful())
		return ExprResult::Error();

	return ExprResult::NotFound();
}

Parser::ExprResult Parser::parsePrimary()
{
	// = <literal>
	if (auto lit = parseLiteral())
		return lit;
	else if(!lit.wasSuccessful())
		return ExprResult::Error();

	// = <decl_call>
	if (auto declcall = parseDeclRef())
		return declcall;
	else if(!declcall.wasSuccessful())
		return ExprResult::Error();

	// = '(' <expr> ')'
	if (auto parens_expr = parseParensExpr(false))
		return parens_expr;
	else if (!parens_expr.wasSuccessful())
		return ExprResult::Error();

	return ExprResult::NotFound();
}

Parser::ExprResult Parser::parseSuffixExpr()
{
	// <suffix_expr>	= <primary> { <suffix> }
	if (auto prim = parsePrimary())
	{
		std::unique_ptr<Expr> base = prim.move();
		ExprResult suffix;
		while (suffix = parseSuffix(base))
		{
			// if suffix is usable, assert that lhs is now null
			assert((!base) && "base should have been moved by parseSuffix!");
			base = std::move(suffix.move());
		}
		if (suffix.wasSuccessful())
			return ExprResult(std::move(base));
		else
			return ExprResult::Error();
	}
	else
	{
		if (!prim.wasSuccessful())
			return ExprResult::Error();
		return ExprResult::NotFound();
	}
}

Parser::ExprResult Parser::parseExponentExpr()
{
	// <exp_expr>	= <suffix_expr> [ <exponent_operator> <prefix_expr> ]

	// <suffix_expr>
	auto lhs = parseSuffixExpr();
	if (!lhs)
		return lhs; 

	// <exponent_operator> 
	if (auto expOp = parseExponentOp())
	{
		// <prefix_expr>
		auto rhs = parsePrefixExpr();
		if (!rhs)
		{
			if(rhs.wasSuccessful())
				reportErrorExpected(DiagID::parser_expected_expr);
				
			return ExprResult::Error();
		}

		SourceLoc begLoc = lhs.getObserverPtr()->getBegLoc();
		SourceLoc endLoc = lhs.getObserverPtr()->getEndLoc();

		return ExprResult(
			std::make_unique<BinaryExpr>(
					BinaryOperator::EXP,
					lhs.move(),
					rhs.move(),
					begLoc,
					expOp, 
					endLoc
				)
		);
	}

	return lhs;
}

Parser::ExprResult Parser::parsePrefixExpr()
{
	// <prefix_expr>  = <unary_operator> <prefix_expr> | <exp_expr>

	// <unary_operator> <prefix_expr> 
	if (auto uop = parseUnaryOp()) // <unary_operator>
	{
		if (auto prefixexpr = parsePrefixExpr())
		{
			SourceLoc endLoc = prefixexpr.getObserverPtr()->getEndLoc();
			return ExprResult(
				std::make_unique<UnaryExpr>(
					uop.get(),
					prefixexpr.move(),
					uop.getSourceRange().getBeginSourceLoc(),
					uop.getSourceRange(),
					endLoc
				)
			);
		}
		else
		{
			if(prefixexpr.wasSuccessful())
				reportErrorExpected(DiagID::parser_expected_expr);

			return ExprResult::Error();
		}
	}

	// <exp_expr>
	if (auto expExpr = parseExponentExpr())
		return expExpr;
	else if (!expExpr.wasSuccessful())
		return ExprResult::Error();

	return ExprResult::NotFound();
}

Parser::ExprResult Parser::parseCastExpr()
{
	// <cast_expr>  = <prefix_expr> ["as" <type>]
	// <cast_expr>
	auto prefixexpr = parsePrefixExpr();
	if (!prefixexpr)
	{
		if (!prefixexpr.wasSuccessful())
			return ExprResult::Error();
		return ExprResult::NotFound();
	}

	// ["as" <type>]
	if (consumeKeyword(KeywordType::KW_AS))
	{
		// <type>
		if (auto castType = parseBuiltinTypename())
		{
			SourceLoc begLoc = prefixexpr.getObserverPtr()->getBegLoc();
			SourceLoc endLoc = castType.getSourceRange().makeEndSourceLoc();
			return ExprResult(
					std::make_unique<CastExpr>(
						castType.get(),
						prefixexpr.move(),
						begLoc,
						castType.getSourceRange(),
						endLoc
					)
				);
		}
		else
		{
			reportErrorExpected(DiagID::parser_expected_type);
			return ExprResult::Error();
		}
	}
	return ExprResult(
		prefixexpr.move()
	);
}

Parser::ExprResult Parser::parseBinaryExpr(std::uint8_t precedence)
{
	// <binary_expr>  = <cast_expr> { <binary_operator> <cast_expr> }	

	// <cast_expr> OR a binaryExpr of inferior priority.
	ExprResult lhsResult;
	if (precedence > 0)
		lhsResult = parseBinaryExpr(precedence - 1);
	else
		lhsResult = parseCastExpr();

	if (!lhsResult)
	{
		if (!lhsResult.wasSuccessful())
			return ExprResult::Error();
		return ExprResult::NotFound();
	}

	std::unique_ptr<Expr> lhs = lhsResult.move();
	std::unique_ptr<BinaryExpr> rtr;

	// { <binary_operator> <cast_expr> }	
	while (true)
	{
		// <binary_operator>
		auto binop_res = parseBinaryOp(precedence);
		if (!binop_res) // No operator found : break.
			break;

		// <cast_expr> OR a binaryExpr of inferior priority.
		ExprResult rhsResult;
		if (precedence > 0)
			rhsResult = parseBinaryExpr(precedence - 1);
		else
			rhsResult = parseCastExpr();


		// Handle results appropriately
		if (!rhsResult) // Check for validity : we need a rhs. if we don't have one, we have an error !
		{
			if(rhsResult.wasSuccessful())
				reportErrorExpected(DiagID::parser_expected_expr);
			return ExprResult::Error();
		}

		std::unique_ptr<Expr> rhs = rhsResult.move();
		SourceLoc begLoc = lhs ? lhs->getBegLoc() : rtr->getEndLoc();
		SourceLoc endLoc = rhs->getEndLoc();
		SourceRange opRange = binop_res.getSourceRange();
		// No return node
		if (!rtr)
			rtr = std::make_unique<BinaryExpr>(
					binop_res.get(),
					std::move(lhs),
					std::move(rhs),
					begLoc, 
					opRange,
					endLoc
				);
		else 
			rtr = std::make_unique<BinaryExpr>(
					binop_res.get(),
					std::move(rtr),
					std::move(rhs),
					begLoc, 
					opRange,
					endLoc
				);

	}

	if (!rtr)
	{
		assert(lhs && "no rtr node + no lhs node?");
		return ExprResult(std::move(lhs));
	}
	return ExprResult(std::move(rtr));
}

Parser::ExprResult Parser::parseExpr()
{
	//  <expr> = <binary_expr> [<assign_operator> <expr>] 
	auto lhs = parseBinaryExpr();
	if (!lhs)
		return lhs;

	if (auto op = parseAssignOp())
	{
		auto rhs = parseExpr();
		if (!rhs)
		{
			if(rhs.wasSuccessful())
				reportErrorExpected(DiagID::parser_expected_expr);
			return ExprResult::Error();
		}

		SourceLoc begLoc = lhs.getObserverPtr()->getBegLoc();
		SourceLoc endLoc = rhs.getObserverPtr()->getEndLoc();
		assert(begLoc && endLoc && "invalid locs");
		SourceRange opRange = op.getSourceRange();
		return ExprResult(
				std::make_unique<BinaryExpr>(
						op.get(),
						lhs.move(),
						rhs.move(),
						begLoc,
						opRange,
						endLoc
					)
			);
	}
	return ExprResult(lhs.move());
}

Parser::ExprResult Parser::parseParensExpr(bool isMandatory, SourceLoc* leftPLoc, SourceLoc* rightPLoc)
{
	// <parens_expr> = '(' <expr> ')'
	// '('
	auto leftParens = consumeBracket(SignType::S_ROUND_OPEN);
	if (!leftParens)
	{
		if (isMandatory)
		{
			reportErrorExpected(DiagID::parser_expected_opening_roundbracket);
			return ExprResult::Error();
		}
		return ExprResult::NotFound();
	}

	std::unique_ptr<Expr> rtr = nullptr;
		
	// <expr>
	if (auto expr = parseExpr())
		rtr = expr.move();
	else 
	{
		// no expr, handle error & attempt to recover if it's allowed. If recovery is successful, return "not found"
		if(expr.wasSuccessful())
			reportErrorExpected(DiagID::parser_expected_expr);

		if (resyncToSign(SignType::S_ROUND_CLOSE, /* stopAtSemi */ true, /*consumeToken*/ true))
			return ExprResult::NotFound();
		else
			return ExprResult::Error();
	}

	assert(rtr && "The return value shouldn't be null!");

	// ')'
	auto rightParens = consumeBracket(SignType::S_ROUND_CLOSE);
	if (!rightParens)
	{
		// no ), handle error & attempt to recover 
		reportErrorExpected(DiagID::parser_expected_closing_roundbracket);

		if (!resyncToSign(SignType::S_ROUND_CLOSE, /* stopAtSemi */ true, /*consumeToken*/ false))
			return ExprResult::Error();
			
		// If we recovered successfuly, place the Sloc into rightParens
		rightParens = consumeBracket(SignType::S_ROUND_CLOSE);
	}

	if (leftPLoc)
		*leftPLoc = leftParens;

	if (rightPLoc)
		*rightPLoc = rightParens;

	return ExprResult(
		std::make_unique<ParensExpr>(std::move(rtr), leftParens, rightParens)
	);
}

Parser::ExprListResult Parser::parseExprList()
{
	// <expr_list> = <expr> {',' <expr> }
	auto firstexpr = parseExpr();
	if (!firstexpr)
		return ExprListResult::NotFound();

	auto exprlist = std::make_unique<ExprList>();
	exprlist->addExpr(firstexpr.move());
	while (auto comma = consumeSign(SignType::S_COMMA))
	{
		if (auto expr = parseExpr())
			exprlist->addExpr(expr.move());
		else
		{
			if (expr.wasSuccessful())
			{
				// if the expression was just not found, revert the comma consuming and
				// let the caller deal with the extra comma after the expression list.
				revertConsume();
				break;
			}

			return ExprListResult::Error();
		}
	}
	return ExprListResult(std::move(exprlist));
}

Parser::ExprListResult Parser::parseParensExprList(SourceLoc* LParenLoc, SourceLoc *RParenLoc)
{
	// <parens_expr_list>	= '(' [ <expr_list> ] ')'
	// '('
	auto leftParens = consumeBracket(SignType::S_ROUND_OPEN);
	if (!leftParens)
		return ExprListResult::NotFound();

	if (LParenLoc)
		*LParenLoc = leftParens;

	std::unique_ptr<ExprList> list = std::make_unique<ExprList>();

	//  [ <expr_list> ]
	if (auto parse_res = parseExprList())
		list = parse_res.move();
	else if (!parse_res.wasSuccessful())
	{
		// error? Try to recover from it, if success, just discard the expr list,
		// if no success return error.
		if (resyncToSign(SignType::S_ROUND_CLOSE, /* stopAtSemi */ true, /*consumeToken*/ false))
		{
			SourceLoc loc = consumeBracket(SignType::S_ROUND_CLOSE);

			if (RParenLoc)
				*RParenLoc = loc;

			return ExprListResult(std::make_unique<ExprList>()); // if recovery is successful, return an empty expression list.
		}
		return ExprListResult::Error();
	}

	SourceLoc rightParens = consumeBracket(SignType::S_ROUND_CLOSE);
	// ')'
	if (!rightParens)
	{
		reportErrorExpected(DiagID::parser_expected_closing_roundbracket);

		if (resyncToSign(SignType::S_ROUND_CLOSE, /* stopAtSemi */ true, /*consumeToken*/ false))
			rightParens = consumeBracket(SignType::S_ROUND_CLOSE);
		else 
			return ExprListResult::Error();
	}

	if (RParenLoc)
		*RParenLoc = rightParens;

	return ExprListResult(std::move(list));
}

SourceRange Parser::parseExponentOp()
{
	if (auto t1 = consumeSign(SignType::S_ASTERISK))
	{
		if (auto t2 = consumeSign(SignType::S_ASTERISK))
			return SourceRange(t1,t2);
		revertConsume();
	}
	return SourceRange();
}

Parser::Result<BinaryOperator> Parser::parseAssignOp()
{
	auto backup = createParserStateBackup();
	if (auto equal = consumeSign(SignType::S_EQUAL))
	{
		// Try to match a S_EQUAL. If failed, that means that the next token isn't a =
		// If it succeeds, we founda '==', this is the comparison operator and we must backtrack to prevent errors.
		if (!consumeSign(SignType::S_EQUAL))
			return Result<BinaryOperator>(BinaryOperator::ASSIGN_BASIC,SourceRange(equal));
		restoreParserStateFromBackup(backup);
	}
	return Result<BinaryOperator>::NotFound();
}

Parser::Result<UnaryOperator> Parser::parseUnaryOp()
{
	if (auto excl = consumeSign(SignType::S_EXCL_MARK))
		return Result<UnaryOperator>(UnaryOperator::LOGICNOT, SourceRange(excl));
	else if (auto minus = consumeSign(SignType::S_MINUS))
		return Result<UnaryOperator>(UnaryOperator::NEGATIVE, SourceRange(minus));
	else if (auto plus = consumeSign(SignType::S_PLUS))
		return Result<UnaryOperator>(UnaryOperator::POSITIVE, SourceRange(plus));
	return Result<UnaryOperator>::NotFound();
}

Parser::Result<BinaryOperator> Parser::parseBinaryOp(std::uint8_t priority)
{
	// Check current Token validity, also check if it's a sign because if it isn't we can return directly!
	if (!getCurtok().isValid() || !getCurtok().isSign())
		return Result<BinaryOperator>::NotFound();

	auto backup = createParserStateBackup();

	switch (priority)
	{
		case 0: // * / %
			if (auto asterisk = consumeSign(SignType::S_ASTERISK))
			{
				if (!consumeSign(SignType::S_ASTERISK)) // Disambiguation between '**' and '*'
					return Result<BinaryOperator>(BinaryOperator::MUL, SourceRange(asterisk));
			}
			else if (auto slash = consumeSign(SignType::S_SLASH))
				return Result<BinaryOperator>(BinaryOperator::DIV, SourceRange(slash));
			else if (auto percent = consumeSign(SignType::S_PERCENT))
				return Result<BinaryOperator>(BinaryOperator::MOD, SourceRange(percent));
			break;
		case 1: // + -
			if (auto plus = consumeSign(SignType::S_PLUS))
				return Result<BinaryOperator>(BinaryOperator::ADD, SourceRange(plus));
			else if (auto minus = consumeSign(SignType::S_MINUS))
				return Result<BinaryOperator>(BinaryOperator::MINUS, SourceRange(minus));
			break;
		case 2: // > >= < <=
			if (auto lessthan = consumeSign(SignType::S_LESS_THAN))
			{
				if (auto equal = consumeSign(SignType::S_EQUAL))
					return Result<BinaryOperator>(BinaryOperator::LESS_OR_EQUAL, SourceRange(lessthan,equal));
				return Result<BinaryOperator>(BinaryOperator::LESS_THAN, SourceRange(lessthan));
			}
			else if (auto grthan = consumeSign(SignType::S_GREATER_THAN))
			{
				if (auto equal = consumeSign(SignType::S_EQUAL))
					return Result<BinaryOperator>(BinaryOperator::GREATER_OR_EQUAL, SourceRange(grthan,equal));
				return Result<BinaryOperator>(BinaryOperator::GREATER_THAN, SourceRange(grthan));
			}
			break;
		case 3:	// == !=
			// try to match '=' twice.
			if (auto equal1 = consumeSign(SignType::S_EQUAL))
			{
				if (auto equal2 = consumeSign(SignType::S_EQUAL))
					return Result<BinaryOperator>(BinaryOperator::EQUAL, SourceRange(equal1,equal2));
			}
			else if (auto excl = consumeSign(SignType::S_EXCL_MARK))
			{
				if (auto equal =consumeSign(SignType::S_EQUAL))
					return Result<BinaryOperator>(BinaryOperator::NOTEQUAL, SourceRange(excl,equal));
			}
			break;
		case 4: // &&
			if (auto amp1 = consumeSign(SignType::S_AMPERSAND))
			{
				if (auto amp2 = consumeSign(SignType::S_AMPERSAND))
					return Result<BinaryOperator>(BinaryOperator::LOGIC_AND, SourceRange(amp1,amp2));
			}
			break;
		case 5: // ||
			if (auto vbar1 = consumeSign(SignType::S_VBAR))
			{
				if (auto vbar2 = consumeSign(SignType::S_VBAR))
					return Result<BinaryOperator>(BinaryOperator::LOGIC_OR, SourceRange(vbar1,vbar2));
			}
			break;
		default:
			fox_unreachable("Unknown priority");
			break;
	}
	restoreParserStateFromBackup(backup);; // Backtrack if we didn't return.
	return Result<BinaryOperator>::NotFound();
}