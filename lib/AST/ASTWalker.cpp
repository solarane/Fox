////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : ASTWalker.cpp											
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
//			SEE HEADER FILE FOR MORE INFORMATION			
////------------------------------------------------------////

// To-Do:
// Add a way of replacing types through a TypeLoc object

#include "Fox/AST/ASTWalker.hpp"
#include "Fox/AST/ASTVisitor.hpp"
#include "Fox/Common/Errors.hpp"
#include "Fox/Common/LLVM.hpp"

using namespace fox;

namespace
{
	class Traverse : public ASTVisitor<Traverse, Decl*, Expr*, Stmt*, /*type*/ bool>
	{		
		public:
			Traverse(ASTWalker& walker):
				walker_(walker)
			{}

			// Exprs

			Expr* visitParensExpr(ParensExpr* expr)
			{
				if (Expr* node = doIt(expr->getExpr()))
				{
					expr->setExpr(node);
					return expr;
				}
				return nullptr;
			}

			Expr* visitBinaryExpr(BinaryExpr* expr)
			{
				Expr* lhs = doIt(expr->getLHS());
				if(!lhs) return nullptr;
				expr->setLHS(lhs);

				Expr* rhs = doIt(expr->getRHS());
				if(!rhs) return nullptr;
				expr->setRHS(rhs);

				return expr;
			}

			Expr* visitUnaryExpr(UnaryExpr* expr)
			{
				if (Expr* child = doIt(expr->getExpr()))
				{
					expr->setExpr(child);
					return expr;
				}
				return nullptr;
			}

			Expr* visitCastExpr(CastExpr* expr)
			{
				if (Expr* child = doIt(expr->getExpr()))
				{
					expr->setExpr(child);
					return expr;
				}
				return nullptr;
			}

			Expr* visitArrayAccessExpr(ArrayAccessExpr* expr)
			{
				Expr* base = doIt(expr->getExpr()); 
				if(!base) return nullptr;
				expr->setExpr(base);

				Expr* idx = doIt(expr->getIdxExpr());
				if(!idx) return nullptr;
				expr->setIdxExpr(idx);

				return expr;
			}

			#define TRIVIAL_EXPR_VISIT(NODE) Expr* visit##NODE(NODE* expr) { return expr; }
			TRIVIAL_EXPR_VISIT(CharLiteralExpr)
			TRIVIAL_EXPR_VISIT(BoolLiteralExpr)
			TRIVIAL_EXPR_VISIT(IntegerLiteralExpr)
			TRIVIAL_EXPR_VISIT(StringLiteralExpr)
			TRIVIAL_EXPR_VISIT(FloatLiteralExpr)
			TRIVIAL_EXPR_VISIT(DeclRefExpr)
			#undef TRIVIAL_EXPR_VISIT
			
			Expr* visitArrayLiteralExpr(ArrayLiteralExpr* expr)
			{
				for (auto& elem : expr->getExprs())
				{
					if (Expr* node = doIt(elem))
						elem = node;
					return nullptr;
				}

				return expr;
			}

			Expr* visitMemberOfExpr(MemberOfExpr* expr)
			{
				if (Expr* child = doIt(expr->getExpr()))
				{
					expr->setExpr(child);
					return expr;
				}
				return nullptr;
			}

			Expr* visitFunctionCallExpr(FunctionCallExpr* expr)
			{
				Expr* callee = doIt(expr->getCallee());
				if(!callee) return nullptr;
				expr->setCallee(callee);

				for (auto& elem : expr->getArgs())
				{
					if (Expr* node = doIt(elem))
						elem = node;
					return nullptr;
				}

				return expr;
			}

			// Decls

			Decl* visitParamDecl(ParamDecl* decl)
			{
				if(doIt(decl->getType()))
					return decl;
				return nullptr;
			}

			Decl* visitVarDecl(VarDecl* decl)
			{
				if (!doIt(decl->getType()))
					return nullptr;

				if (Expr* init = decl->getInitExpr())
				{
					if (init = doIt(init))
						decl->setInitExpr(init);
					return nullptr;
				}

				return decl;
			}

			Decl* visitFuncDecl(FuncDecl* decl)
			{
				for (auto& param : decl->getParams())
				{
					if (param)
					{
						if (Decl* node = doIt(param))
							param = cast<ParamDecl>(node);
						return nullptr;
					}
				}

				if (Stmt* body = decl->getBody())
				{
					if (body = doIt(body))
						decl->setBody(cast<CompoundStmt>(body));
					else return nullptr;
				}

				return decl;
			}

			Decl* visitUnitDecl(UnitDecl* decl)
			{
				for (auto& elem : decl->getDecls())
				{
					if (elem)
					{
						if (Decl* node = doIt(elem))
							elem = node;
						else return nullptr;
					}
				}

				return decl;
			}

			// Stmt

			Stmt* visitNullStmt(NullStmt* stmt)
			{
				return stmt;
			}

			Stmt* visitReturnStmt(ReturnStmt* stmt)
			{
				if (Expr* expr = stmt->getExpr())
				{
					if (expr = doIt(expr))
						stmt->setExpr(expr);
					else return nullptr;
				}

				return stmt;
			}

			Stmt* visitConditionStmt(ConditionStmt* stmt)
			{
				if (Expr* cond = stmt->getCond())
				{
					if (cond = doIt(cond))
						stmt->setCond(cond);
					else return nullptr;
				}

				if (ASTNode then = stmt->getThen())
				{
					if (then = doIt(then))
						stmt->setThen(then);
					else return nullptr;
				}

				if (ASTNode elsestmt = stmt->getElse())
				{
					if (elsestmt = doIt(elsestmt))
						stmt->setElse(elsestmt);
					else return nullptr;
				}

				return stmt;
			}

			Stmt* visitCompoundStmt(CompoundStmt* stmt)
			{
				for (auto& elem: stmt->getNodes())
				{
					if (elem)
					{
						if (ASTNode node = doIt(elem))
							elem = node;
						else return nullptr;
					}
				}

				return stmt;
			}

			Stmt* visitWhileStmt(WhileStmt* stmt)
			{
				if (Expr* cond = stmt->getCond())
				{
					if (cond = doIt(cond))
						stmt->setCond(cond);
					else return nullptr;
				}

				if (ASTNode node = stmt->getBody())
				{
					if (node = doIt(node))
						stmt->setBody(node);
					else return nullptr;
				}

				return stmt;
			}

			// doIt method for expression: handles call to the walker &
			// requests visitation of the children of a given node.
			Expr* doIt(Expr* expr)
			{
				// Let the walker handle the pre visitation stuff.
				auto rtr = walker_.handleExprPre(expr);

				// Return if we have a nullptr or if we're instructed
				// to not visit the children.
				if (!rtr.first || !rtr.second)
					return rtr.first;

				// visit the node's childre, and if the traversal wasn't aborted,
				// let the walker handle post visitation stuff.
				if (expr = visit(rtr.first))
					expr = walker_.handleExprPost(expr);

				return expr;
			}

			// doIt method for declarations: handles call to the walker &
			// requests visitation of the children of a given node.
			Decl* doIt(Decl* expr)
			{
				// Let the walker handle the pre visitation stuff.
				auto rtr = walker_.handleDeclPre(expr);

				// Return if we have a nullptr or if we're instructed
				// to not visit the children.
				if (!rtr.first || !rtr.second)
					return rtr.first;

				// visit the node's childre, and if the traversal wasn't aborted,
				// let the walker handle post visitation stuff.
				if (expr = visit(rtr.first))
					expr = walker_.handleDeclPost(expr);

				return expr;
			}

			// doIt method for statements: handles call to the walker &
			// requests visitation of the children of a given node.
			Stmt* doIt(Stmt* expr)
			{
				// Let the walker handle the pre visitation stuff.
				auto rtr = walker_.handleStmtPre(expr);

				// Return if we have a nullptr or if we're instructed
				// to not visit the children.
				if (!rtr.first || !rtr.second)
					return rtr.first;

				// visit the node's childre, and if the traversal wasn't aborted,
				// let the walker handle post visitation stuff.
				if (expr = visit(rtr.first))
					expr = walker_.handleStmtPost(expr);

				return expr;
			}

			// doIt method for types: handles call to the walker &
			// requests visitation of the children of a given node.
			bool doIt(Type* type)
			{
				// Let the walker handle the pre visitation stuff.
				if (!walker_.handleTypePre(type))
					return false;

				// Visit the node
				if (!visit(type))
					return false;

				// Let the walker handle post visitation stuff
				return walker_.handleTypePost(type);
			}

			ASTNode doIt(ASTNode node)
			{
				if (auto decl = node.getIf<Decl>())
					return doIt(decl);
				if (auto stmt = node.getIf<Stmt>())
					return doIt(stmt);
				if (auto expr = node.getIf<Expr>())
					return doIt(expr);

				fox_unreachable("Unknown node contained in ASTNode");
			}

		private:
			ASTWalker& walker_;
	};
} // End anonymous namespace

ASTNode ASTWalker::walk(ASTNode node)
{
	if (auto decl = node.getIf<Decl>())
		return walk(decl);
	if (auto stmt = node.getIf<Stmt>())
		return walk(stmt);
	if (auto expr = node.getIf<Expr>())
		return walk(expr);

	fox_unreachable("Unknown node contained in ASTNode");
}

Expr* ASTWalker::walk(Expr* expr)
{
	return Traverse(*this).doIt(expr);
}

Decl* ASTWalker::walk(Decl* decl)
{
	return Traverse(*this).doIt(decl);
}

Stmt* ASTWalker::walk(Stmt* stmt)
{
	return Traverse(*this).doIt(stmt);
}

bool ASTWalker::walk(Type* type)
{
	return Traverse(*this).doIt(type);
}

std::pair<Expr*, bool> ASTWalker::handleExprPre(Expr* expr)
{
	return { expr, true };
}

Expr* ASTWalker::handleExprPost(Expr* expr)
{
	return expr;
}

std::pair<Stmt*, bool> ASTWalker::handleStmtPre(Stmt* stmt)
{
	return { stmt, true };
}

Stmt* ASTWalker::handleStmtPost(Stmt* stmt)
{
	return stmt;
}

std::pair<Decl*, bool> ASTWalker::handleDeclPre(Decl* decl)
{
	return { decl, true };
}

Decl* ASTWalker::handleDeclPost(Decl* decl)
{
	return decl;
}

bool ASTWalker::handleTypePre(Type* type)
{
	return true;
}

bool ASTWalker::handleTypePost(Type* type)
{
	return true;
}
