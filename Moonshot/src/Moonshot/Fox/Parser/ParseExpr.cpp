////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : ParseExpr.cpp										
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
//			SEE HEADER FILE FOR MORE INFORMATION			
// This file implements expressions related methods (rules)	
////------------------------------------------------------////

#include "Parser.h"

using namespace Moonshot;
using namespace fv_util;


std::unique_ptr<ASTExpr> Parser::parseExpr(const char & priority)
{
	auto rtr = std::make_unique<ASTExpr>(operation::PASS);
	std::unique_ptr<ASTExpr> first = 0, last = 0;

	if (priority > 0)
		first = parseExpr(priority - 1);	// Go down in the priority chain.
	else 
		first = parseCastExpr();	// We are at the lowest point ? Parse the term then !

	if (!first)					// Check if it was found/parsed correctly. If not, return a null node.
		return nullptr;

	rtr->makeChild(dir::LEFT, first);	// Make Left the left child of the return node !
	while (true)
	{
		operation op;
		bool matchResult;
		std::tie(matchResult, op) = matchBinaryOp(priority);
		if (!matchResult) // No operator found : break.
			break;
		std::unique_ptr<ASTExpr> second;

		if (priority > 0)
			second = parseExpr(priority - 1);
		else
			second = parseCastExpr();
		// Check for validity : we need a term. if we don't have one, we have an error !
		if (!second)
		{
			errorExpected("Expected an expression after binary operator.");
			break;
		}
		// Add the node to the tree but in different ways, depending on left or right assoc.
		if (!isRightAssoc(op))		// Left associative operator
		{
			if (rtr->op_ == operation::PASS)
				rtr->op_ = op;
			else	// Already has one ? create a new node and make rtr its left child.
				rtr = oneUpNode(rtr, op);
			rtr->makeChild(dir::RIGHT, second);
		}									
		// I Have to admit, this bit might have poor performance if you start to go really deep, like 2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2^2.. and so on, but who would do this anyway, right..right ?
		// To be serious:there isn't really another way of doing this,except if I change completly my parsing algorithm to do both right to left and left to right parsing.
		// OR if i implement the shunting yard algo, and that's even harder to do with my current system.
		// Which is complicated, and not really useful for now, as <expr> is the only rule
		// that might need right assoc.
		else								// right associative nodes
		{
			// There's a member variable, last_ that will hold the last parsed "second" variable.
			
			// First "loop" check.
			if (rtr->op_ == operation::PASS)
				rtr->op_ = op;			

			if (!last) // Last is empty
				last = std::move(second); // Set last_ to second.
			else
			{
				_ASSERT(op != operation::PASS);
				auto newnode_op = std::make_unique<ASTExpr>(op); // Create a node with the op
				newnode_op->makeChild(dir::LEFT, last); // Set last_ as left child.
				last = std::move(second);// Set second as last
				// Append newnode_op to rtr
				rtr->makeChildOfDeepestNode(dir::RIGHT, newnode_op);
			}
		}
	}
	if (last) // Last isn't empty -> make it the left child of our last node.
	{
		rtr->makeChildOfDeepestNode(dir::RIGHT, last);
		last = 0;
	}
	// When we have simple node (PASS operation with only a value/expr as left child), we simplify it(only return the left child)
	auto simple = rtr->getSimple();
	if (simple)
		return simple;
	return rtr;
}

std::unique_ptr<ASTExpr> Parser::parsePrefixExpr()
{
	bool uopResult = false;
	operation uopOp;
	std::size_t casttype = invalid_index;
	std::tie(uopResult, uopOp) = matchUnaryOp(); // If an unary op is matched, uopResult will be set to true and pos_ updated.
	if (uopResult)
	{
		if (auto node = parsePrefixExpr())
			return oneUpNode(node, uopOp);
		else
		{
			errorExpected("Expected an expression after unary operator in prefix expression.");
			return nullptr;
		}
	}
	else if (auto node = parseValue())
		return node;
	return nullptr;
}

std::unique_ptr<ASTExpr> Parser::parseCastExpr()
{
	if (auto node = parsePrefixExpr())
	{
		std::size_t casttype = invalid_index;
		// Search for a (optional) cast: "as" <type>
		if (matchKeyword(keywordType::TC_AS))
		{
			if ((casttype = matchTypeKw()) != invalid_index)
			{
				// If found, apply it to current node.
				node = oneUpNode(node, operation::CAST); // Create a "cast" node
				node->totype_ = casttype;
			}
			else
			{
				// If error (invalid keyword found, etc.)
				errorExpected("Expected a type keyword after \"as\" in cast expression.");
				return nullptr;
			}
		}
		return node;
	}
	return nullptr;
}

std::unique_ptr<ASTExpr> Parser::parseValue()
{
	// if the Token is invalid, return directly a null node

	// = <const>
	auto matchValue_result = matchLiteral();
	if (matchValue_result.first) // if we have a value, return it packed in a ASTLiteral
		return std::make_unique<ASTLiteral>(matchValue_result.second);
	else if (auto node = parseCallable()) // Callable?
		return node;
	// = '(' <expr> ')'
	else if (matchSign(signType::B_ROUND_OPEN))
	{
		auto expr = parseExpr(); // Parse the expression inside
		if (!expr) // check validity of the parsed expression
		{
			errorExpected("Expected an expression after '('.");
			return nullptr;
		}
		// retrieve the closing bracket, throw an error if we don't have one. 
		if (!matchSign(signType::B_ROUND_CLOSE))
		{
			errorExpected("Expected a ')' after expression");
			return nullptr;
		}
		return expr;
	}
	// TO DO :
	// f_call
	return nullptr;
}

std::unique_ptr<ASTExpr> Parser::parseCallable()
{
	// = <id>
	auto result = matchID();
	if (result.first)
		return std::make_unique<ASTVarCall>(result.second);
	return nullptr;
}

std::unique_ptr<ASTExpr> Parser::oneUpNode(std::unique_ptr<ASTExpr>& node, const operation & op)
{
	auto newnode = std::make_unique<ASTExpr>(op);
	newnode->makeChild(dir::LEFT, node);
	return newnode;
}

std::pair<bool, operation> Moonshot::Parser::matchUnaryOp()
{
	// Avoid long & verbose lines.
	

	auto cur = getToken();
	if (!cur.isValid() || (cur.type != tokenType::TT_SIGN))
		return { false, operation::PASS };

	if (cur.sign_type == signType::P_EXCL_MARK)
	{
		pos_ += 1;
		return { true, operation::LOGICNOT };
	}
	if (cur.sign_type == signType::S_MINUS)
	{
		pos_ += 1;
		return { true, operation::NEGATE};
	}

	return { false, operation::PASS };
}

std::pair<bool, operation> Parser::matchBinaryOp(const char & priority)
{
	// Avoid long & verbose lines.
	

	auto cur = getToken();
	auto pk = getToken(pos_ + 1);
	// Check current Token validity
	if (!cur.isValid() || (cur.type != tokenType::TT_SIGN))
		return { false, operation::PASS };
	pos_ += 1; // We already increment once here in prevision of a matched operator. We'll decrease before returning the result if nothing was found, of course.

	switch (priority)
	{
		case 0:
			if (cur.sign_type == signType::S_EXP)
				return { true, operation::EXP };
			break;
		case 1: // * / %
			if (cur.sign_type == signType::S_ASTERISK)
				return { true, operation::MUL };
			if (cur.sign_type == signType::S_SLASH)
				return { true, operation::DIV };
			if (cur.sign_type == signType::S_PERCENT)
				return { true, operation::MOD };
			break;
		case 2: // + -
			if (cur.sign_type == signType::S_PLUS)
				return { true, operation::ADD };
			if (cur.sign_type == signType::S_MINUS)
				return { true, operation::MINUS };
			break;
		case 3: // > >= < <=
			if (cur.sign_type == signType::S_LESS_THAN)
			{
				if (pk.isValid() && (pk.sign_type == signType::S_EQUAL))
				{
					pos_ += 1;
					return { true, operation::LESS_OR_EQUAL };
				}
				return { true, operation::LESS_THAN };
			}
			if (cur.sign_type == signType::S_GREATER_THAN)
			{
				if (pk.isValid() && (pk.sign_type == signType::S_EQUAL))
				{
					pos_ += 1;
					return { true, operation::GREATER_OR_EQUAL };
				}
				return { true, operation::GREATER_THAN };
			}
			break;
		case 4:	// == !=
			if (pk.isValid() && (pk.sign_type == signType::S_EQUAL))
			{
				if (cur.sign_type == signType::S_EQUAL)
				{
					pos_ += 1;
					return { true,operation::EQUAL };
				}
				if (cur.sign_type == signType::P_EXCL_MARK)
				{
					pos_ += 1;
					return { true,operation::NOTEQUAL };
				}
			}
			break;
		case 5:
			if (pk.isValid() && (pk.sign_type == signType::S_AND) && (cur.sign_type == signType::S_AND))
			{
				pos_ += 1;
				return { true,operation::AND };
			}
			break;
		case 6:
			if (pk.isValid() && (pk.sign_type == signType::S_VBAR) && (cur.sign_type == signType::S_VBAR))
			{
				pos_ += 1;
				return { true,operation::OR };
			}
			break;
		case 7:
			if ((cur.sign_type == signType::S_EQUAL)
				&&
				!(pk.isValid() && (pk.sign_type == signType::S_EQUAL))) // Refuse if op is ==
				return { true,operation::ASSIGN };
			break;
		default:
			throw Exceptions::parser_critical_error("Requested to match a Binary Operator with a non-existent priority");
			break;
	}
	pos_ -= 1;	// We did not find anything, decrement & return.
	return { false, operation::PASS };
}
