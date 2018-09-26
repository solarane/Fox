////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : ASTVisitor.hpp											
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
// This file implements the "Visitor" class, which dispatchs Stmt,Decls and Types
// to their appropriate "visit" method.
////------------------------------------------------------////

#pragma once

#include "Decl.hpp"
#include "Stmt.hpp"
#include "Expr.hpp"
#include "Types.hpp"
#include "ASTNode.hpp"
#include "Fox/Common/Errors.hpp"
#include <utility>

namespace fox
{
	// Visitor class, which takes a few templates arguments : The derived class, The return type and the Args
	// that should be passed to the Visit method.
	template<
		typename Derived, 
		typename DeclRtrTy,
		typename ExprRtrTy,
		typename StmtRtrTy,
		typename TypeRtrTy,
		typename ... Args
	>
	class ASTVisitor
	{
		public:
			// Visit ASTNode
			void visit(ASTNode node, Args... args)
			{
				assert(node.getOpaque() && "Cannot be used on a null pointer");
				if (node.is<Decl>())
					visit(node.get<Decl>(), ::std::forward<Args>(args)...);
				else if (node.is<Expr>())
					visit(node.get<Expr>(), ::std::forward<Args>(args)...);
				else if (node.is<Stmt>())
					visit(node.get<Stmt>(), ::std::forward<Args>(args)...);
				else
					fox_unreachable("Unsupported ASTNode variant");
			}

			// Visit Decl "Dispatch" Method
			DeclRtrTy visit(Decl* decl, Args... args)
			{
				assert(decl && "Cannot be used on a null pointer");
				switch (decl->getKind())
				{
					#define DECL(ID,PARENT)\
						case DeclKind::ID:\
							return static_cast<Derived*>(this)->visit##ID(static_cast<ID*>(decl), ::std::forward<Args>(args)...);
					#include "DeclNodes.def"
					default:
						fox_unreachable("Unknown node");
				}
			}

			// Visit Stmt dispatch method
			StmtRtrTy visit(Stmt* stmt, Args... args)
			{
				assert(stmt && "Cannot be used on a null pointer");
				switch (stmt->getKind())
				{
					#define STMT(ID,PARENT)\
						case StmtKind::ID:\
							return static_cast<Derived*>(this)->visit##ID(static_cast<ID*>(stmt), ::std::forward<Args>(args)...);
					#include "StmtNodes.def"
					default:
						fox_unreachable("Unknown node");
				}
			}

			// Visit Expr dispatch method
			ExprRtrTy visit(Expr* expr, Args... args)
			{
				assert(expr && "Cannot be used on a null pointer");
				switch (expr->getKind())
				{
					#define EXPR(ID,PARENT)\
						case ExprKind::ID:\
							return static_cast<Derived*>(this)->visit##ID(static_cast<ID*>(expr), ::std::forward<Args>(args)...);
					#include "ExprNodes.def"
					default:
						fox_unreachable("Unknown node");
				}
			}

			// Visit Types dispatch method
			TypeRtrTy visit(TypeBase* type, Args... args)
			{
				assert(type && "Cannot be used on a null pointer");
				switch (type->getKind())
				{
					#define TYPE(ID,PARENT)\
						case TypeKind::ID:\
							return static_cast<Derived*>(this)->visit##ID(static_cast<ID*>(type), ::std::forward<Args>(args)...);
					#include "TypeNodes.def"
					default:
						fox_unreachable("Unknown node");
				}
			}

			// Base visitStmt, visitDecl and visitType methods.

			// Visit Stmt
			StmtRtrTy visitStmt(Stmt*, Args... args)
			{
				return StmtRtrTy();
			}

			// Visit Expr
			ExprRtrTy visitExpr(Expr*, Args... args)
			{
				return ExprRtrTy();
			}

			// Visit Decl 
			DeclRtrTy visitDecl(Decl*, Args... args)
			{
				return DeclRtrTy();
			}

			// Visit Type 
			TypeRtrTy visitTypeBase(TypeBase*, Args... args)
			{
				return TypeRtrTy();
			}

			// VisitXXX Methods
			// The base implementations just chain back to the parent class, so visitors can just
			// implement the parent class or an abstract class and still handle every derived class!
			#define VISIT_METHOD(RTRTYPE, NODE, PARENT)\
			RTRTYPE visit##NODE(NODE* node, Args... args){ \
				return static_cast<Derived*>(this)->visit##PARENT(node, ::std::forward<Args>(args)...); \
			}

			// Decls
			#define DECL(ID,PARENT) VISIT_METHOD(DeclRtrTy, ID, PARENT)
			#define ABSTRACT_DECL(ID,PARENT) VISIT_METHOD(DeclRtrTy, ID, PARENT)
			#include "DeclNodes.def"

			// Stmt & Exprs
			#define STMT(ID,PARENT) VISIT_METHOD(StmtRtrTy, ID, PARENT)
			#define ABSTRACT_STMT(ID,PARENT) VISIT_METHOD(StmtRtrTy, ID, PARENT)
			#include "StmtNodes.def"

			// Stmt & Exprs
			#define EXPR(ID,PARENT) VISIT_METHOD(ExprRtrTy, ID, PARENT)
			#define ABSTRACT_EXPR(ID,PARENT) VISIT_METHOD(ExprRtrTy, ID, PARENT)
			#include "ExprNodes.def"

			// Types
			#define TYPE(ID,PARENT) VISIT_METHOD(TypeRtrTy, ID, PARENT)
			#define ABSTRACT_TYPE(ID,PARENT) VISIT_METHOD(TypeRtrTy, ID, PARENT)
			#include "TypeNodes.def"

	};

	// Simple visitor
	template<typename Derived, typename RtrTy, typename ... Args>
	using SimpleASTVisitor = ASTVisitor<Derived, RtrTy, RtrTy, RtrTy, RtrTy, Args...>;

	// Visitor for Decls
	template<typename Derived, typename RtrTy, typename ... Args>
	using DeclVisitor = ASTVisitor<Derived, RtrTy, void, void, void, Args...>;

	// Visitor for Exprs
	template<typename Derived, typename RtrTy, typename ... Args>
	using ExprVisitor = ASTVisitor<Derived, void, RtrTy, void, void, Args...>;

	// Visitor for Stmts
	template<typename Derived, typename RtrTy, typename ... Args>
	using StmtVisitor = ASTVisitor<Derived, void, void, RtrTy, void, Args...>;

	// Visitor for Types
	template<typename Derived, typename RtrTy, typename ... Args>
	using TypeVisitor = ASTVisitor<Derived, void, void, void, RtrTy, Args...>;
}
