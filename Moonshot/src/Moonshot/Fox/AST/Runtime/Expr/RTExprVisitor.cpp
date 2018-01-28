////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : RTExprVisitor.cpp											
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
//			SEE HEADER FILE FOR MORE INFORMATION			
////------------------------------------------------------////

#include "RTExprVisitor.h"

using namespace Moonshot;
using namespace fv_util;


RTExprVisitor::RTExprVisitor(Context& c) : context_(c)
{
}

RTExprVisitor::RTExprVisitor(Context& c, std::shared_ptr<SymbolsTable> symtab) : context_(c), symtab_(symtab)
{
}

RTExprVisitor::~RTExprVisitor()
{
}

void RTExprVisitor::visit(ASTExpr & node)
{
	if (!context_.isSafe())
	{
		value_ = FVal(); // return directly if errors, don't waste time evaluating "sick" nodes.
		return;
	}
	if (node.op_ == operation::CONCAT)
	{
		if (node.left_ && node.right_)
		{
			node.left_->accept(*this);
			auto leftval = value_;
			node.right_->accept(*this);
			auto rightval = value_;
			
			if (std::holds_alternative<std::string>(leftval) &&
				std::holds_alternative<std::string>(rightval))
			{
				auto leftstr = std::get<std::string>(leftval);
				auto rightstr = std::get<std::string>(rightval);
				value_ = FVal(std::string(leftstr + rightstr));
				return;
			}
			else
				// One of the 2 childs, or the child, does not produce strings.
				throw Exceptions::ast_malformation("A Node with a CONCAT operation did not have compatible types as left_ and/or right_ values.");
		}
		else
		{
			throw Exceptions::ast_malformation("Tried to concat a node without a left_ or right  child.");
		}
	}
	else if (node.op_ == operation::ASSIGN)
	{
		if (isSymbolsTableAvailable())
		{
			node.left_->accept(*this);
			auto left_res = value_;
			node.right_->accept(*this);
			auto right_res = value_;
			if (std::holds_alternative<var::varRef>(left_res) && isValue(right_res.index()))
			{
				symtab_->setValue(
					std::get<var::varRef>(left_res).getName(),
					right_res
				);
				value_ = right_res; // Assignement returns the value on the left !
				return;
			}
			else 
				context_.reportError("Impossible assignement between " + dumpFVal(left_res) + " and " + dumpFVal(right_res));
		}
		else
			context_.logMessage("Can't perform assignement operations when the symbols table is unavailable.");
	}
	else if (node.op_ == operation::CAST)
	{
		node.left_->accept(*this);
		value_ = castTo_withDeref(node.totype_, value_);
		return;
	}
	else if (node.op_ == operation::PASS)
	{
		if (!node.left_)
			throw Exceptions::ast_malformation("Tried to pass a value to parent node, but the node did not have a left_ child.");
		else 
			return node.left_->accept(*this);
	}
	else if (node.op_ == operation::ASSIGN)
	{
		if (isSymbolsTableAvailable())
		{
			node.left_->accept(*this);
			auto vattr = value_;
			if (std::holds_alternative<var::varRef>(vattr))
			{
				// Perform assignement
				std::string vname = std::get<var::varRef>(vattr).getName();
				node.right_->accept(*this);
				symtab_->setValue(vname, value_);
			}
			else
				// this could use context_.error instead. On the long run, if this error happens often and malformed code is the source, use context_.error instead of a throw
				throw Exceptions::ast_malformation("Can't assign a value if lhs isn't a variable !");
		}
		else
			context_.logMessage("Can't perform assignement operations when the symbols table isn't available.");
	}
	else if (isComparison(node.op_))
	{
		if (!node.left_ || !node.right_)
			throw Exceptions::ast_malformation("Attempted to run a comparison operation on a node without 2 children");

		node.left_->accept(*this);
		const FVal lfval = value_;
		node.right_->accept(*this);
		const FVal rfval = value_;
		if (std::holds_alternative<std::string>(lfval) && std::holds_alternative<std::string>(rfval)) // lhs/rhs str?
		{
			if (lfval.index() == rfval.index()) // if so, lhs/rhs must both be strings to compare them.
			{
				value_ = FVal(
					compareStr(
						node.op_,
						std::get<std::string>(lfval),
						std::get<std::string>(rfval)
					)
				);
				return;
			}
			else
			{
				context_.reportError("Attempted to compare a string with an arithmetic type.");
				value_ = FVal();
				return;
			}
		}
		else
		{
			node.left_->accept(*this);
			double dleftval = fvalToDouble_withDeref(value_);
			node.right_->accept(*this);
			double drightval = fvalToDouble_withDeref(value_);
			//std::cout << "Compare: Converted lhs :" << dleftval << " converted rhs: " << drightval << std::endl;
			value_ = FVal(compareVal(
				node.op_,
				lfval,
				rfval
			)
			);
			return;
		}
	}
	else if (node.op_ == operation::LOGICNOT || node.op_ == operation::NEGATE)
	{
		node.left_->accept(*this);
		double lval = fvalToDouble_withDeref(value_);
		if (node.op_ == operation::LOGICNOT)
		{
			value_ = FVal(
				lval == 0 // If the value differs equals zero, return true
			);
			return;
		}
		else
		{
			lval = -lval; // Negate the number
			value_ = castTo(context_,node.totype_, lval);		// Cast to result type
			return;
		}
	}
	else
	{
		if (!node.left_ || !node.right_)
			throw Exceptions::ast_malformation("Tried to perform an operation on a node without a left_ and/or right child.");

		node.left_->accept(*this);
		const double dleftval = fvalToDouble_withDeref(value_);
		node.right_->accept(*this);
		const double drightval = fvalToDouble_withDeref(value_);
		//std::cout << "Op: " << util::enumAsInt(node.op_) << ",Converted lhs :" << dleftval << " converted rhs: " << drightval << std::endl;
		const double result = performOp(node.op_, dleftval, drightval);

		if (fitsInValue(node.totype_, result) || (node.op_ == operation::CAST)) // If the results fits or we desire to cast the result
			value_ = castTo(context_,node.totype_, result);		// Cast to result type
		else
			value_ = castTo(context_,fval_float, result);	// Cast to float instead to keep information from being lost.
		return;
	}
	value_ = FVal();
}

void RTExprVisitor::visit(ASTLiteral & node)
{
	value_ = node.val_;
	return;
}

void RTExprVisitor::visit(ASTVarCall & node)
{
	if (isSymbolsTableAvailable())
	{
		value_ = symtab_->retrieveVarAttr(node.varname_).createRef(); // this returns a reference, because it's needed for assignement operations.
		return;
	}
	else
	{
		context_.logMessage("Can't retrieve values if the symbols table is not available.");
		value_ = FVal();
		return;
	}
}

void RTExprVisitor::setSymbolsTable(std::shared_ptr<SymbolsTable> symtab)
{
	symtab_ = symtab;
}

FVal RTExprVisitor::getResult() const
{
	return value_;
}

double RTExprVisitor::fvalToDouble_withDeref(FVal fval)
{
	// If fval is a reference, dereference it first
	if (std::holds_alternative<var::varRef>(fval))
	{
		if (isSymbolsTableAvailable())
		{
			fval = symtab_->retrieveValue(
				std::get<var::varRef>(fval).getName()
			);
		}
		else
			context_.logMessage("Can't dereference variable if the symbols table is not available.");
	}
    if (!isBasic(fval.index()))
		context_.reportError("Can't perform conversion to double on a non basic type.");
	else if (!isArithmetic(fval.index()))
		context_.reportError("Can't perform conversion to double on a non arithmetic type.");
	else if (std::holds_alternative<int>(fval))
		return (double)std::get<int>(fval);
	else if (std::holds_alternative<float>(fval))
		return (double)std::get<float>(fval);
	else if (std::holds_alternative<char>(fval))
		return (double)std::get<char>(fval);
	else if (std::holds_alternative<bool>(fval))
		return (double)std::get<bool>(fval);
	else
		throw std::logic_error("Reached end of function.Unimplemented type in FVal?");
	return 0.0;
}
bool RTExprVisitor::compareVal(const operation & op, const FVal & l, const FVal & r)
{
	
	const double lval = fvalToDouble_withDeref(l);
	const double rval = fvalToDouble_withDeref(r);
	switch (op)
	{
		case operation::AND:
			return (lval != 0) && (rval != 0);
		case operation::OR:
			return (lval != 0) || (rval != 0);
		case operation::LESS_OR_EQUAL:
			return lval <= rval;
		case operation::GREATER_OR_EQUAL:
			return lval >= rval;
		case operation::LESS_THAN:
			return lval < rval;
		case operation::GREATER_THAN:
			return lval > rval;
		case operation::EQUAL:
			return lval == rval;
		case operation::NOTEQUAL:
			return lval != rval;
		default:
			throw std::logic_error("Defaulted. Unimplemented condition operation?");
			return false;
	}
}
bool RTExprVisitor::compareStr(const operation & op, const std::string & lhs, const std::string & rhs)
{
	
	switch (op)
	{
		case operation::EQUAL:				return lhs == rhs;
		case operation::NOTEQUAL:			return lhs != rhs;
		case operation::LESS_THAN:			return lhs < rhs;
		case operation::GREATER_THAN:		return lhs > rhs;
		case operation::LESS_OR_EQUAL:		return lhs <= rhs;
		case operation::GREATER_OR_EQUAL:	return lhs > rhs;
		default:	throw std::logic_error("Operation was not a condition.");
			return false;
	}
}
double RTExprVisitor::performOp(const operation& op,double l,double r)
{
	switch (op)
	{
		case operation::ADD:	return l + r;
		case operation::MINUS:	return l - r;
		case operation::MUL:	return l * r;
		case operation::DIV:
			if(r == 0)
			{
				context_.reportError("Division by zero.");
				return 0.0;
			}
			else 
				return l / r;
		case operation::MOD:
			// if the divisor is greater, it goes zero times in l, so we can directly return l
			//std::cout << "l:" << l << " r:" << r << std::endl;
			if (l > r)
			{
				auto res = std::fmod(l, r);
				if (res < 0)
					return res + r;
				return res;
			}
			else
				return (l < 0) ? l + r : l;
			// About the res < 0 -> res + r 
			// and  (l < 0) ? l + r : l;
			// parts, it's a tip from https://stackoverflow.com/a/12277233/3232822
			// Thanks !
		case operation::EXP:
			// if exp < 0 perform 1/base^exp
			if (r < 0)
				return performOp(operation::DIV, 1, std::pow(l, -r));
			else if (r == 0)
				return 1; // Any number with exponent 0 equals 1, except 0
			else
				return std::pow(l, r);
		default:	throw std::logic_error("Can't evaluate op.");
			return 0.0;
	}
}

bool RTExprVisitor::fitsInValue(const std::size_t& typ, const double & d)
{
	
	switch (typ)
	{
		case fval_bool:
			return true; // When we want to cast to bool, we usually don't care to lose information, we just want a true/false result.
		case fval_int:
			if (d > INT_MAX || d < INT_MIN)
				return false;
			return true;
		case fval_float:
			return true;
		case fval_char:
			if (d < -127 || d > 127)
				return false;
			return true;
		case invalid_index:
			throw std::logic_error("Index was invalid");
			return false;
		default:
			if (!isBasic(typ))
				throw std::logic_error("Can't make a \"fitInValue\" check on a non-basic type.");
			else
				throw std::logic_error("Switch defaulted. Unimplemented type?");
			return false;
	}
}

bool RTExprVisitor::isSymbolsTableAvailable() const
{
	return (symtab_ ? true : false);
}

FVal RTExprVisitor::castTo_withDeref(const std::size_t & goal, FVal val)
{
	if (std::holds_alternative<var::varRef>(val))
	{
		auto ref = std::get<var::varRef>(val);
		val = symtab_->retrieveValue(ref.getName());
	}
	else if (!isBasic(goal))
	{
		throw std::logic_error("The Goal type was not a basic type.");
		return FVal();
	}
	return castTo(context_,goal, val);
}
