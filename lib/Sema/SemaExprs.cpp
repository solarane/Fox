//----------------------------------------------------------------------------//
// This file is a part of The Moonshot Project.        
// See LICENSE.txt for license info.            
// File : SemaExprs.cpp                    
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//
//  This file implements Sema methods related to Exprs
//----------------------------------------------------------------------------//

// Short to-do list (to do in order)
/*
    Re-order methods in the ExprChecker. Document them better, write "category" 
    headers, etc.

    Write the description of Diagnose methods and Finalize methods
    
    Write LIT tests for UnaryExpr & CastExpr 
    (take advantage of the redudant cast warning)

    Check again how verifymode works with -werror (should I use expect-warning or expect-error?)
      -> Maybe let the DV allow an error with expect-warn?

    Move on
*/

#include "Fox/Sema/Sema.hpp"
#include "Fox/AST/Expr.hpp"
#include "Fox/Common/Errors.hpp"
#include "Fox/Common/LLVM.hpp"
#include "Fox/AST/ASTWalker.hpp"
#include "Fox/AST/ASTVisitor.hpp"

#include <utility>
#include <iostream>

using namespace fox;

namespace {
  // Various helper functions

  // If "type" is a CellType* with a substitution, returns the substitution,
  // else, returns "type".
  Type defer_if(Type type) {
    if (auto* ptr = type.getAs<CellType>()) {
      // if the type has a substitution, return it, else
      // just return the argument.
      if (auto* sub = ptr->getSubstitution())
        return Type(sub);
    }
    return type;
  }

  // Expression checker: Classic visitor, the visitXXX functions
  // all check a single node. They do not orchestrate visitation of
  // the children, because that is done in the ASTWalker
  //
  // Every visitation method return a pointer to an Expr*, which is the current expr
  // OR the expr that should take it's place. This can NEVER be null.
  class ExprChecker : public ExprVisitor<ExprChecker, Expr*>, public ASTWalker {
    using Inherited = ExprVisitor<ExprChecker, Expr*>;
    Sema& sema_;
    public:
      ExprChecker(Sema& sema): sema_(sema) {
        
      }

      //--Diagnose Methods--//
      // TODO: Describe "Diagnose" methods

      // (Error) Diagnoses an invalid cast 
      void diagnoseInvalidCast(CastExpr* expr) {
        SourceRange range = expr->getCastTypeLoc().getRange();
        TypeBase* childTy = expr->getExpr()->getType().getPtr();
        TypeBase* goalTy = expr->getCastTypeLoc().getPtr();
        getDiags()
          .report(DiagID::sema_invalid_cast, range)
          .addArg(childTy->toString())
          .addArg(goalTy->toString())
          .setExtraRange(expr->getExpr()->getRange());
      }

      // (Warning) Diagnoses a redudant cast (when the
      // cast goal and the child's type are equal)
      void diagnoseRedundantCast(CastExpr* expr) {
        SourceRange range = expr->getCastTypeLoc().getRange();
        TypeBase* goalTy = expr->getCastTypeLoc().getPtr();
        getDiags()
          .report(DiagID::sema_redundant_cast, range)
          .addArg(goalTy->toString())
          .setExtraRange(expr->getExpr()->getRange());
      }

      //--Finalize methods--//
      // TODO: Describe "Finalize" methods

      // Finalizes a valid CastExpr
      Expr* finalizeCastExpr(CastExpr* expr, bool isRedundant) {
        if (isRedundant) {
          // Diagnose the redundant cast
          diagnoseRedundantCast(expr);
          // Remove the CastExpr and just return the child
          Expr* child = expr->getExpr();
          // Simply replace the range of the child with the range
          // of the CastExpr, so diagnostics will correctly highlight the whole
          // cast's region.
          child->setRange(expr->getRange());
          return child;
        }

        // Else, the Expr's type is simply the castgoal.
        expr->setType(expr->getCastTypeLoc().withoutLoc());
        return expr;
      }

      // Finalizes a valid UnaryExpr
      // \param childTy The type of the child as a PrimitiveType.
      Expr* finalizeUnaryExpr(UnaryExpr* expr, PrimitiveType* childTy) {
        assert(childTy && "cannot be nullptr");
        using OP = UnaryExpr::OpKind;
        switch (expr->getOp()) {
          // Logical NOT operator : '!'
          case OP::LNot:
            // Always boolean
            expr->setType(PrimitiveType::getBool(getCtxt()));
            break;
          // Unary Plus '+' and Minus '-'
          case OP::Minus:
          case OP::Plus:
            // Always int or float, never bool, so uprank
            // if boolean.
            if(childTy->isBoolType())
              childTy = PrimitiveType::getInt(getCtxt());
            expr->setType(childTy);
            break;
          case OP::Invalid:
            fox_unreachable("Invalid Unary Operator");
          default:
            fox_unreachable("All cases handled");
        }
        return expr;
      }

      // Returns the Int type if type is a boolean, or
      // return it's argument otherwise.
      // The pointer must not be null an point to an integral type.
      TypeBase* uprankIfBoolean(PrimitiveType* type) {
        assert(type && "Pointer must not be null");
        assert(Sema::isIntegral(type) && "Type must be integral");

        if (type->getPrimitiveKind() == PrimitiveType::Kind::BoolTy)
          return PrimitiveType::getInt(getCtxt());
        return type;
      }

      // Returns the ASTContext
      ASTContext& getCtxt() {
        return sema_.getASTContext();
      }

      // Returns the DiagnosticEngine
      DiagnosticEngine& getDiags() {
        return sema_.getDiagnosticEngine();
      }

      Sema& getSema() {
        return sema_;
      }

      virtual std::pair<Expr*, bool> handleExprPre(Expr* expr) {
        // Not needed since we won't do preorder visitation
        return { expr, true }; // Important for postorder visitation to be done
      }

      virtual Expr* handleExprPost(Expr* expr) {
        expr = visit(expr);
        assert(expr && "Expr cannot be null!");
        // Check if the expr is typed. If it isn't, that
        // means typechecking failed for this node, so set
        // it's type to ErrorType.
        if (!expr->getType()) {
          expr->setType(ErrorType::get(getCtxt()));
        }
        return expr;
      }

      virtual std::pair<Stmt*, bool> handleStmtPre(Stmt*) {
        fox_unreachable("Illegal node kind");
      }

      virtual Stmt* handleStmtPost(Stmt*) {
        fox_unreachable("Illegal node kind");
      }

      virtual std::pair<Decl*, bool> handleDeclPre(Decl*) {
        fox_unreachable("Illegal node kind");
      }

      virtual Decl* handleDeclPost(Decl*) {
        fox_unreachable("Illegal node kind");
      }

      // Check methods

      Expr* visitBinaryExpr(BinaryExpr*) {
        // Note:
          // Handle arithmetic & text addition
          // Disallow array operation unless *
        fox_unimplemented_feature("BinaryExpr TypeChecking");
      }

      Expr* visitCastExpr(CastExpr* expr) {
        // Get the types & unwrap them
        TypeBase* childTy = expr->getExpr()->getType().getPtr();
        TypeBase* goalTy = expr->getCastTypeLoc().getPtr();
        std::tie(childTy, goalTy) = Sema::unwrapAll({childTy, goalTy });

        // Sanity Check:
          // It is impossible for unbound types to exist
          // as cast goals, as cast goals are type written
          // down by the user.
        assert(getSema().isBound(goalTy) &&
          "Unbound types cannot be present as cast goals!");

        // Check for Error Types. If one of the types is an ErrorType
        // just abort.
        if (isa<ErrorType>(childTy) && isa<ErrorType>(goalTy))
          return expr;

        // Casting to a String  
          // Check that the child's type is a primitive type.
        if (goalTy->isStringType()) {
          // If the expr's type isn't a primitive type, diagnose
          // the invalid cast.
          if (!isa<PrimitiveType>(childTy)) 
            diagnoseInvalidCast(expr);
          
          return finalizeCastExpr(expr, childTy->isStringType());
        }
        
        // Casting to anything else
          // For other type of casts, unification is enough to determine
          // if the cast is valid. If unification fails, diagnose + errorType
        if (getSema().unify(childTy, goalTy))
          return finalizeCastExpr(expr, (childTy == goalTy));

        diagnoseInvalidCast(expr);
        return expr;
      }

      Expr* visitUnaryExpr(UnaryExpr* expr) {
        Expr* child = expr->getExpr();
        TypeBase* childTy = child->getType().getPtr();
        // ignore LValue + deref
        childTy = Sema::deref(childTy->ignoreLValue());

        // For any unary operators, we only allow integral types,
        // so check that first.
        if (!Sema::isIntegral(childTy)) {
          // Not an integral type -> error.
          // Emit diag if childTy isn't a ErrorType too
          if (!isa<ErrorType>(childTy)) {
            getDiags()
              .report(DiagID::sema_unaryop_bad_child_type, expr->getOpRange())
              // Use the child's range as the extra range.
              .setExtraRange(child->getRange()) 
              .addArg(expr->getOpSign()) // %0 is the operator's sign as text
              .addArg(childTy->toString()); // %1 is the type of the child
          }
          return expr;
        }
        
        PrimitiveType* primChildTy = dyn_cast<PrimitiveType>(childTy);
        assert(primChildTy 
          && "isIntegral returned true but the type isn't a PrimitiveType?");
        return finalizeUnaryExpr(expr, primChildTy);
      }

      Expr* visitArrayAccessExpr(ArrayAccessExpr*) {
        // Note:
          // Check that base is of ArrayType and idx expr
          // is arithmetic and not float
        fox_unimplemented_feature("ArrayAccessExpr TypeChecking");
      }

      Expr* visitMemberOfExpr(MemberOfExpr*) {
        fox_unimplemented_feature("MemberOfExpr TypeChecking");
      }

      Expr* visitDeclRefExpr(DeclRefExpr*) {
        fox_unimplemented_feature("DeclRefExpr TypeChecking");
      }

      Expr* visitFunctionCallExpr(FunctionCallExpr*) {
        fox_unimplemented_feature("FunctionCallExpr TypeChecking");
      }
      

      // Trivial literals: the expr's type is simply the corresponding
      // type. Int for a Int literal, etc.
      Expr* visitCharLiteralExpr(CharLiteralExpr* expr) {
        expr->setType(PrimitiveType::getChar(getCtxt()));
        return expr;
      }

      Expr* visitIntegerLiteralExpr(IntegerLiteralExpr* expr) {
        expr->setType(PrimitiveType::getInt(getCtxt()));
        return expr;
      }

      Expr* visitFloatLiteralExpr(FloatLiteralExpr* expr) {
        expr->setType(PrimitiveType::getFloat(getCtxt()));
        return expr;
      }

      Expr* visitBoolLiteralExpr(BoolLiteralExpr* expr) {
        expr->setType(PrimitiveType::getBool(getCtxt()));
        return expr;
      }

      Expr* visitStringLiteralExpr(StringLiteralExpr* expr) {
        expr->setType(PrimitiveType::getString(getCtxt()));
        return expr;
      }

      // Array literals
      // To deduce the type of an Array literal:
      // if size > 0
      //    see deduceTypeOfArrayLiteral
      // else
      //    Type needs inference
      Expr* visitArrayLiteralExpr(ArrayLiteralExpr* expr) {
        if (expr->getSize() > 0) {
          TypeBase* deduced = deduceTypeOfArrayLiteral(expr);
          if(deduced)
            expr->setType(deduced);
          return expr;
        }
        // if it's empty, just set it's type to an empty CellType
        else
          expr->setType(CellType::create(getCtxt()));
        return expr;
      }

      // Helper for the above function that deduces the type of a non empty Array literal
      // Returns the type of the literal or nullptr if it can't be calculated.
      TypeBase* deduceTypeOfArrayLiteral(ArrayLiteralExpr* expr) {
        assert(expr->getSize() && "Size must be >0");

        // Diagnoses a heterogenous array literal.
        // Emits the diagnostics and returns the errorType.
        static auto diagnose_hetero = [&](Expr* faultyElem = nullptr) {
          if (faultyElem) {
            getDiags()
              // Precise error loc is the first element that failed the inferrence,
              // extended range is the whole arrayliteral's.
              .report(DiagID::sema_arraylit_hetero, faultyElem->getRange())
              .setRange(expr->getRange());
          }
          else {
            getDiags()
              // If we have no element to pinpoint, just use the whole expr's
              // range
              .report(DiagID::sema_arraylit_hetero, expr->getRange());
          }
          return nullptr;
        };

        // The bound type proposed by unifying the other concrete/bound
        // types inside the array.
        TypeBase* boundTy = nullptr;

        // The type used by unbounds elemTy
        TypeBase* unboundTy = nullptr;

        // Loop over each expression in the literal
        for (auto& elem : expr->getExprs()) {
          // Get the elemTy
          TypeBase* elemTy = elem->getType().getPtr();
          assert(elemTy && "Type cannot be null!");

          // Handle error elem type: we stop here if we have one.
          if (isa<ErrorType>(elemTy))
            return nullptr;

          // Special logic for unbound types
          if (!Sema::isBound(elemTy)) {
            // Set unboundTy & continue for first loop
            if (!unboundTy)
              unboundTy = elemTy;
            // Attempt unification
            else if (!getSema().unify(unboundTy, elemTy))
              return diagnose_hetero(elem);
            continue;
          }

          // From this point, ElemTy is guaranteed to be a bound/concrete type

          // First loop, set boundTy & continue.
          if (!boundTy) {
            boundTy = elemTy;
            continue;
          }

          // Unify elemTy with the bound proposed type.
          if (!getSema().unify(boundTy, elemTy))
            return diagnose_hetero(elem); // Failed to unify, incompatible types

          // Get the highest ranking type of elemTy and boundTy
          boundTy = Sema::getHighestRankedTy(elemTy, boundTy);
          assert(boundTy &&
           "Couldn't determine the highest ranked type but unification succeeded?");
        }
        Type proper;
        // Both unboundTy & boundTy
        if (unboundTy && boundTy) {
          // Unify them
          if (!getSema().unify(unboundTy, boundTy))
            return diagnose_hetero(); // FIXME: Do proper diagnosis
          proper = boundTy; // FIXME: That or getHighestRanking?
        }
        // Only boundTy OR unboundTy
        else if (boundTy)
          proper = boundTy;
        else if (unboundTy)
          proper = unboundTy;
        else
          fox_unreachable("Should have at least a boundTy or unboundTy set.");
        assert(proper);
        // The type of the expr is an array of the proposed type.
        return ArrayType::get(getCtxt(), proper.getPtr());
      }
  };

  // ExprFinalizer, which rebuilds types to remove
  // SemaTypes.
  // Visit methods return pointers to TypeBase. They return nullptr
  // if the finalization failed for this expr.
  // It's still a primitive, test version for now.
  class ExprFinalizer : public TypeVisitor<ExprFinalizer, TypeBase*>,
    public ASTWalker {
    ASTContext& ctxt_;
    DiagnosticEngine& diags_;

    public:
      ExprFinalizer(ASTContext& ctxt, DiagnosticEngine& diags) :
        ctxt_(ctxt), diags_(diags) {

      }

      Expr* handleExprPost(Expr* expr) {
        TypeBase* type = expr->getType().getPtr();
        assert(type && "Untyped expr");

        // Visit the type
        type = visit(type);
        
        // If the type is nullptr, this inference failed
        // because of a lack of substitution somewhere.
        // Set the type to ErrorType, diagnose it and move on.
        if (!type) {
          diags_.report(DiagID::sema_failed_infer, expr->getRange());
          type = ErrorType::get(ctxt_);
        }

        expr->setType(type);
        return expr;
      }

      TypeBase* visitPrimitiveType(PrimitiveType* type) {
        return type;
      }

      TypeBase* visitArrayType(ArrayType* type) {
        if (TypeBase* elem = visit(type->getElementType())) {
          // Rebuild if needed
          if (elem != type->getElementType())
            return ArrayType::get(ctxt_, elem);
          return type;
        }
        return nullptr;
      }

      TypeBase* visitLValueType(LValueType* type) {
        if (TypeBase* elem = visit(type->getType())) {
          if (elem != type->getType())
            return LValueType::get(ctxt_, elem);
          return type;
        }
        return nullptr;
      }

      TypeBase* visitCellType(CellType* type) {
        if (TypeBase* sub = type->getSubstitution())
          return visit(sub);
        return nullptr;
      }

      TypeBase* visitErrorType(ErrorType* type) {
        // Error should have been handled already, we won't emit
        // more.
        return type;
      }
  };
} // End anonymous namespace

Expr* Sema::typecheckExpr(Expr* expr) {
  expr = ExprChecker(*this).walk(expr);
  expr = ExprFinalizer(ctxt_, diags_).walk(expr);
  return expr;
}
