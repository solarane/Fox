//----------------------------------------------------------------------------//
// Part of the Fox project, licensed under the MIT license.
// See LICENSE.txt in the project root for license information.     
// File : BCGenExpr.cpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//

#include "Registers.hpp"
#include "Fox/BCGen/BCGen.hpp"
#include "Fox/BC/BCBuilder.hpp"
#include "Fox/AST/Expr.hpp"
#include "Fox/AST/Types.hpp"
#include "Fox/AST/ASTVisitor.hpp"
#include "Fox/AST/ASTWalker.hpp"
#include "Fox/AST/Types.hpp"
#include "Fox/AST/TypeVisitor.hpp"
#include "Fox/Common/FoxTypes.hpp"
#include "Fox/Common/LLVM.hpp"
#include "llvm/ADT/Optional.h"

using namespace fox;

/// Binary Operator Kinds
using BinOp = BinaryExpr::OpKind;
/// Unary Operator Kinds
using UnOp = UnaryExpr::OpKind;

/// A Generation "thunk" function, which delays generation of an expr
/// for later, when the thunk is actually called
/// The argument type is the destination register (can be null), and the
/// return type is the RegisterValue where the result is located.
using GenThunk = std::function<RegisterValue(RegisterValue)>;

/// Calls a genThunk, enforcing its invariants (if dest != null, the
/// return value of the thunk must be == dest)
RegisterValue call(GenThunk thunk, RegisterValue dest = RegisterValue()) {
  #ifndef NDEBUG
    // In debug mode, check that the destination is respected
    bool hadDest = dest.isAlive();
    regaddr_t expectedAddr = dest ? dest.getAddress() : 0;
    RegisterValue resultRV = thunk(std::move(dest));
    if (hadDest) {
      assert((expectedAddr == resultRV.getAddress())
      && "A destination register was provided but was not respected");
    }
    return resultRV;
  #else 
    return thunk(std::move(dest));
  #endif
}

//----------------------------------------------------------------------------//
// AssignementGenerator : Declaration
//----------------------------------------------------------------------------// 

class BCGen::AssignementGenerator : public Generator,
                             ExprVisitor<AssignementGenerator, RegisterValue,
                                                Expr*, RegisterValue, BinOp> {
  using Visitor = ExprVisitor<AssignementGenerator, RegisterValue, 
                                      Expr*, RegisterValue, BinOp>;
  friend Visitor;
  public:  
    AssignementGenerator(BCGen& gen, BCBuilder& builder, 
                         ExprGenerator& exprGen);

    /// The BCGen::ExprGenerator that created this AssignementGenerator 
    ExprGenerator& exprGen;
    /// The RegisterAllocator associted with the exprGenerator
    RegisterAllocator& regAlloc;
  
    /// Generates the bytecode for an assignement expression \p expr and puts
    /// the result inside \p dest
    RegisterValue generate(BinaryExpr* expr, RegisterValue dest);

    /// copies \p src in \p dest if \p dest is alive and != from \p src.
    /// \returns \p dest or \p src depending on if the copy was emitted or not.
    RegisterValue copyInDest(RegisterValue dest, RegisterValue src) {
      if ((dest) && (src != dest)) {
        builder.createCopyInstr(dest.getAddress(), src.getAddress());
        return dest;
      }
      return src;
    }

  private:
    ///----------------------------------------------------------------------///
    /// Handled scenarios
    ///----------------------------------------------------------------------///

    RegisterValue
    visitSubscriptExpr(SubscriptExpr* dst, Expr* src, RegisterValue dest, 
                       BinOp op);
    RegisterValue
    visitDeclRefExpr(DeclRefExpr* dst, Expr* src, RegisterValue dest, BinOp op);

    ///----------------------------------------------------------------------///
    /// Unhandled scenarios
    ///----------------------------------------------------------------------///

    RegisterValue
    visitUnresolvedDeclRefExpr(UnresolvedDeclRefExpr*, Expr*, 
                               RegisterValue, BinOp) {
      fox_unreachable("UnresolvedDeclRefExpr found past Semantic Analysis");
    }

    RegisterValue
    visitUnresolvedDotExpr(UnresolvedDotExpr*, Expr*, RegisterValue, BinOp) {
      fox_unreachable("UnresolvedDotExpr found past Semantic Analysis");
    }

    RegisterValue 
    visitErrorExpr(ErrorExpr*, Expr*, RegisterValue, BinOp) {
      fox_unreachable("ErrorExpr found past Semantic Analysis");
    }

    // Some nodes should never be found in the LHS of an assignement.
    #define IMPOSSIBLE_ASSIGNEMENT(KIND) RegisterValue\
      visit##KIND(KIND*, Expr*, RegisterValue, BinOp)\
      { fox_unreachable("Unhandled Assignement: Cannot assign to a " #KIND); }
    IMPOSSIBLE_ASSIGNEMENT(BinaryExpr)
    IMPOSSIBLE_ASSIGNEMENT(UnaryExpr)
    IMPOSSIBLE_ASSIGNEMENT(CastExpr)
    IMPOSSIBLE_ASSIGNEMENT(AnyLiteralExpr)
    IMPOSSIBLE_ASSIGNEMENT(CallExpr)
    IMPOSSIBLE_ASSIGNEMENT(BuiltinMemberRefExpr)
    #undef IMPOSSIBLE_ASSIGNEMENT
};

//----------------------------------------------------------------------------//
// ExprGenerator
//----------------------------------------------------------------------------// 

// The class responsible for generating the bytecode of expressions
class BCGen::ExprGenerator : public Generator,
                             ExprVisitor<ExprGenerator, RegisterValue, 
                                         /*args*/ RegisterValue> {
  using Visitor = ExprVisitor<ExprGenerator, RegisterValue, RegisterValue>;
  friend Visitor;
  public:
    ExprGenerator(BCGen& gen, BCBuilder& builder, 
                  RegisterAllocator& regAlloc) : Generator(gen, builder),
                  regAlloc(regAlloc) {}

    /// Entry point of the ExprGenerator.
    RegisterValue generate(Expr* expr) {
      return visit(expr);
    }  
    
    /// Entry point of the ExprGenerator
    /// \p dest is the destination register for the expression.
    /// The return RegisterValue shall be equal to \p dest
    RegisterValue generate(Expr* expr, RegisterValue reg) {
      return visit(expr, std::move(reg));
    }

    RegisterAllocator& regAlloc;

    friend AssignementGenerator;
  private:
    /// Binary Operator Kinds
    using BinOp = BinaryExpr::OpKind;
    /// Unary Operator Kinds
    using UnOp = UnaryExpr::OpKind;
    /// A std::initializer_list of references to objects
    template<typename Ty>
    using reference_initializer_list 
      = std::initializer_list< std::reference_wrapper<Ty>>;

    //------------------------------------------------------------------------//
    // Helper methods
    // 
    // Helper functions performing various tasks. Generalizes/shortens
    // some common patterns used in this generator.
    //------------------------------------------------------------------------//

    /// \returns a GenThunk that call visit() on an expr
    GenThunk getGTForExpr(Expr* expr) {
      return [this, expr](RegisterValue rv) {
        return this->visit(expr, std::move(rv));
      };
    }

    /// \returns true if \p expr can be generated using a "XXXInt" instruction
    /// such as AddInt, SubInt, etc.
    /// This is true if both operands are of Int, Bool or Char type.
    bool canGenToIntBinop(BinaryExpr* expr) {
      auto check = [](Expr* expr) {
        Type type = expr->getType();
        assert(type && "type is null");
        return type->isIntType() 
            || type->isBoolType()
            || type->isCharType();
      };
      if (check(expr->getLHS())) {
        assert(check(expr->getRHS()) 
          && "Inconsistent types");
        return true;
      }
      return false;
    }

    /// Tries to use \p dest, if \p dest is dead, allocates
    /// a new temporary
    RegisterValue tryUse(RegisterValue dest) {
      // NOTE: Don't recycle 'dest' because dest might be owning a 
      // variable or something. Just return it.
      if(dest) return dest;
      return regAlloc.allocateTemporary();
    }

    /// Chooses a destination register for an expression
    ///  -> Uses \p dest when it's non null
    ///  -> Else, uses the best recyclable RV in \p hints.
    ///     \p hints must be an array-like type of RegisterValues
    ///     (uses the one with the lowest address possible)
    ///  -> Else allocates a new temporary
    ///
    /// After execution, the hint that has been recycled will be dead
    /// (=evaluate to false) and other hints will be left untouched.
    template<typename RVArray = reference_initializer_list<RegisterValue>>
    RegisterValue getDestReg(RegisterValue dest, RVArray&& hints) {
      // NOTE: Don't recycle 'dest' because dest might be owning a 
      // variable or something. Just return it.
      if(dest) return dest;

      // First, allocate a temporary register which will serve as a starting
      // point.
      // Allocating it now lets this method do the right thing when allocating
      // a temporary is actually more efficient than reusing one of the
      // hints.
      RegisterValue temp = regAlloc.allocateTemporary();;
      RegisterValue* best = &temp;
      for (RegisterValue& hint : hints) {
        if (hint.canRecycle()) {
          // If the address of this hint is lower than the address of the best
          // candidate, it becomes our best candidate.
          if(hint.getAddress() < best->getAddress())
            best = &(hint);
        }
      }

      // Don't bother calling regAlloc.recycle if the best candidate is the 
      // temporary
      if(best == &temp)
        return temp;
      // Else, call recycle.
      return regAlloc.recycle(std::move(*best));
    }

    //------------------------------------------------------------------------//
    // "emit" methods 
    // 
    // These methods perform some generalized tasks related to bytecode
    // emission
    //------------------------------------------------------------------------//

    // Emits a simple call to a builtin function, using GenThunks to generate
    // the arguments as needed.
    //
    // NOTE: This doesn't check that the call is well formed, it just emits it.
    RegisterValue emitBuiltinCall(BuiltinKind bID, RegisterValue dest, 
                                  ArrayRef<GenThunk> generators, 
                                  SourceRange debugRange) {
      // Reserve registers
      SmallVector<RegisterValue, 4> callRegs;
      regAlloc.allocateCallRegisters(callRegs, generators.size()+1);

      // Put a reference to the builtin in the base register
      regaddr_t baseAddr = callRegs.front().getAddress();
      builder.createLoadBuiltinFuncInstr(baseAddr, bID);

      // Compile the args
      for (std::size_t k = 0, size = generators.size(); k < size; ++k)
        callRegs[k+1] = call(generators[k], std::move(callRegs[k+1]));


      BCBuilder::StableInstrConstIter callInstrIter;
      // Gen the call. If the builtin returns void, use
      if(hasNonVoidReturnType(bID)) {
        // Choose a destination register
        dest = getDestReg(std::move(dest), callRegs);
        // Emit.
        callInstrIter = builder.createCallInstr(baseAddr, dest.getAddress());
      }
      else {
        assert(!dest && "cannot have a destination if the builtin returns void");
        callInstrIter = builder.createCallVoidInstr(baseAddr);
      }

      assert(debugRange && "debugRange cannot be null");
      builder.addDebugRange(callInstrIter, debugRange);

      return dest;
    }

    // Emit an instruction to store the constant 'val' into the register
    // 'reg'.
    void emitStoreIntConstant(regaddr_t dest, FoxInt val) {
      auto ssi_min = bc_limits::storeSmallInt_min;
      auto ssi_max = bc_limits::storeSmallInt_max;
      // Check if the value can be emitted using StoreSmallInt
      if ((val >= ssi_min) && (val <= ssi_max)) {
        builder.createStoreSmallIntInstr(dest, val);
        return;
      }
      // Else, store the constant in the constant table and emit a LoadIntK
      auto kId = bcGen.getConstantID(val);
      builder.createLoadIntKInstr(dest, kId);
    }

    // Generates the adequate instruction(s) to perform a binary
    // operation on doubles
    void 
    emitDoubleBinOp(BinOp op, regaddr_t dst, regaddr_t lhs, regaddr_t rhs,
                    SourceRange debugRange) {
      assert((lhs != rhs) && "lhs and rhs are identical");
      // Emit
      switch (op) {
        case BinOp::Add:  // +
          builder.createAddDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::Sub:  // -
          builder.createSubDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::Mul:  // *
          builder.createMulDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::Div: {  // /
          auto instr = builder.createDivDoubleInstr(dst, lhs, rhs);
          builder.addDebugRange(instr, debugRange);
          break;
        }
        case BinOp::Mod: {  // %
          auto instr = builder.createModDoubleInstr(dst, lhs, rhs);
          builder.addDebugRange(instr, debugRange);
          break;
        }
        case BinOp::Pow:  // **
          builder.createPowDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::LE:   // <=
          builder.createLEDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::GE:   // >=
          builder.createGEDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::LT:   // <
          builder.createLTDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::GT:   // >
          builder.createGTDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::Eq:   // ==
          builder.createEqDoubleInstr(dst, lhs, rhs);
          break;
        case BinOp::NEq:  // !=
          // != isn't implemented in the vm, it's just implemented
          // as !(a == b). This requires 2 instructions.
          builder.createEqDoubleInstr(dst, lhs, rhs);
          builder.createLNotInstr(dst, dst);
          break;
        case BinOp::LAnd: // &&
        case BinOp::LOr:  // ||
          fox_unreachable("cannot apply these operators on doubles");
        default:
          fox_unreachable("Unhandled binary operation kind");
      }
    }

    // Generates the adequate instruction(s) to perform a binary
    // operation on integers.
    // This is used to generate operations involving chars, 
    // booleans and ints.
    void emitIntBinOp(BinOp op, regaddr_t dst, regaddr_t lhs, regaddr_t rhs, 
                      SourceRange debugRange) {
      assert((lhs != rhs) && "lhs and rhs are identical");
      // Emit
      switch (op) {
        case BinOp::Add:  // +
          builder.createAddIntInstr(dst, lhs, rhs);
          break;
        case BinOp::Sub:  // -
          builder.createSubIntInstr(dst, lhs, rhs);
          break;
        case BinOp::Mul:  // *
          builder.createMulIntInstr(dst, lhs, rhs);
          break;
        case BinOp::Div: {  // /
          auto instr = builder.createDivIntInstr(dst, lhs, rhs);
          builder.addDebugRange(instr, debugRange);
          break;
        }
        case BinOp::Mod: {  // %
          auto instr = builder.createModIntInstr(dst, lhs, rhs);
          builder.addDebugRange(instr, debugRange);
          break;
        }
        case BinOp::Pow:  // **
          builder.createPowIntInstr(dst, lhs, rhs);
          break;
        case BinOp::LE:   // <=
          builder.createLEIntInstr(dst, lhs, rhs);
          break;
        case BinOp::GE:   // >=
          // For >=, it's not implemented in the VM, but
          // (a >= b) is the same as (b <= a)
          builder.createLEIntInstr(dst, rhs, lhs);
          break;
        case BinOp::LT:   // <
          builder.createLTIntInstr(dst, lhs, rhs);
          break;
        case BinOp::GT:   // >
          // > isn't implemented in the VM too, but
          // (a > b) is the same as !(a <= b). This requires 2 instructions.
          // dest = lhs <= rhs
          builder.createLEIntInstr(dst, lhs, rhs);
          // dest != dest
          builder.createLNotInstr(dst, dst);
          break;
        case BinOp::Eq:   // ==
          builder.createEqIntInstr(dst, lhs, rhs);
          break;
        case BinOp::NEq:  // !=
          // != isn't implemented in the vm, it's just implemented
          // as !(a == b). This requires 2 instructions.
          builder.createEqIntInstr(dst, lhs, rhs);
          builder.createLNotInstr(dst, dst);
          break;
        case BinOp::LAnd: // &&
          builder.createLAndInstr(dst, lhs, rhs);
          break;
        case BinOp::LOr:  // ||
          builder.createLOrInstr(dst, lhs, rhs);
          break;
        default:
          fox_unreachable("Unhandled binary operation kind");
      }
    }

    // Generates the code for a BinaryExpr whose type is a Numeric or
    // Boolean Binary Expr.
    RegisterValue emitNumericOrBoolBinaryExpr(BinaryExpr* expr, 
                                             RegisterValue dest) {
      assert((expr->getType()->isNumericOrBool()));
      
      // Gen the LHS
      RegisterValue lhsReg = visit(expr->getLHS());
      regaddr_t lhsAddr = lhsReg.getAddress();
      assert(lhsReg && "Generated a dead register for the LHS");

      // Gen the RHS
      RegisterValue rhsReg = visit(expr->getRHS());
      regaddr_t rhsAddr = rhsReg.getAddress();
      assert(rhsReg && "Generated a dead register for the RHS");
      
      // Choose the destination register
      RegisterValue dstReg = getDestReg(std::move(dest), {lhsReg, rhsReg});
      regaddr_t dstAddr = dstReg.getAddress();

      // Dispatch to the appropriate generator function

      // Integer or Boolean expressions
      if (canGenToIntBinop(expr))
        emitIntBinOp(expr->getOp(), dstAddr, lhsAddr, rhsAddr,
                     expr->getSourceRange());
      // Double operands
      else if (expr->getLHS()->getType()->isDoubleType()) {
        assert(expr->getRHS()->getType()->isDoubleType()
          && "Inconsistent Operands");
        emitDoubleBinOp(expr->getOp(), dstAddr, lhsAddr, rhsAddr, 
                        expr->getSourceRange());
      }
      else 
        fox_unreachable("unhandled situation : operands are "
          "neither int, bools or doubles");
      return dstReg;
    }

    RegisterValue emitConcatBinaryExpr(BinaryExpr* expr, RegisterValue dest) {
      assert(expr->isConcat() && "not a concatenation");
      //  The return type of this expression should be a string
      assert(expr->getType()->isStringType() && "doesn't return a string");

      Expr* lhs = expr->getLHS();
      Expr* rhs = expr->getRHS();

      // char + char concatenation
      if(lhs->getType()->isCharType() && rhs->getType()->isCharType()) {
        // Generate a call to the charConcat builtin
        GenThunk lhsGT = getGTForExpr(lhs);
        GenThunk rhsGT = getGTForExpr(rhs);
        return emitBuiltinCall(
          BuiltinKind::charConcat, 
          std::move(dest), 
          {lhsGT, rhsGT}, 
          expr->getSourceRange());
      }

      // string + string
      // char + string 
      // string + char

      assert((lhs->getType()->isCharType() || lhs->getType()->isStringType())
        && "LHS is neither a string or a char");
      assert((rhs->getType()->isCharType() || rhs->getType()->isStringType())
        && "RHS is neither a string or a char");

      // FIXME: lhsGT and rhsGT have a lot in common, could code duplication
      //        be reduced here?
      GenThunk lhsGT = [this, lhs, expr](RegisterValue dest) {
        // If the lhs is a string, just gen it
        if(lhs->getType()->isStringType())
          return this->visit(lhs, std::move(dest));
        // Else it's a char, so emit a charToString
        return 
          this->emitBuiltinCall(BuiltinKind::charToString, 
                                std::move(dest),
                                this->getGTForExpr(lhs),
                                expr->getSourceRange());
      };

      GenThunk rhsGT = [this, rhs, expr](RegisterValue dest) {
        // If the rhs is a string, just gen it
        if(rhs->getType()->isStringType())
          return this->visit(rhs, std::move(dest));
        // Else it's a char, so emit a charToString
        return 
          this->emitBuiltinCall(BuiltinKind::charToString, 
                                std::move(dest),
                                this->getGTForExpr(rhs),
                                expr->getSourceRange());
      };

      return emitBuiltinCall(
        BuiltinKind::strConcat, 
        std::move(dest), 
        {lhsGT, rhsGT}, 
        expr->getSourceRange()
      );
    }

    RegisterValue emitToStringUnOp(UnaryExpr* expr, RegisterValue dest) {
      assert((expr->getOp() == UnOp::ToString) && "wrong function");
      Expr* child = expr->getChild();

      // If the child's type is already string, this is a no-op
      if(child->getType()->isStringType())
        return visit(child, std::move(dest));


      GenThunk childGT = getGTForExpr(child);

      // Emit the builtin call depending on the type of the child
      class ToStringBuiltinChooser 
      : public TypeVisitor<ToStringBuiltinChooser, BuiltinKind> {
        public:
          #define HANDLE(TYPE, ACTION)\
            BuiltinKind visit##TYPE(TYPE*) { ACTION; }
          HANDLE(IntegerType, return BuiltinKind::intToString)
          HANDLE(DoubleType,  return BuiltinKind::doubleToString)
          HANDLE(BoolType,    return BuiltinKind::boolToString)
          HANDLE(CharType,    return BuiltinKind::charToString)
          HANDLE(StringType,  
            fox_unreachable("Should have been handled above");)
          HANDLE(VoidType,    
            fox_unreachable("Cannot cast 'VoidType' to string");)
          HANDLE(FunctionType,    
            fox_unreachable("Cannot cast 'FunctionType' to string");)
          HANDLE(ArrayType,    
            fox_unreachable("Cannot cast 'ArrayType' to string");)
          // We use getRValue() to call this, so there shouldn't be 
          // any LValues here
          HANDLE(LValueType,
            fox_unreachable("Should have been handled by getRValue()");)
          HANDLE(TypeVariableType,   
            fox_unreachable("TypeVariableType shouldn't be present in BCGen");)
          HANDLE(ErrorType,   
            fox_unreachable("ErrorType shouldn't be present in BCGen");)
      };

      BuiltinKind builtin;
      {
        ToStringBuiltinChooser tsbc;
        builtin = tsbc.visit(child->getType()->getRValue());
      }

      // Emit the builtin call.
      return emitBuiltinCall(
        builtin, 
        std::move(dest), 
        {childGT}, 
        expr->getSourceRange()
      );
    }

    RegisterValue 
    emitStringSubscript(SubscriptExpr* expr, RegisterValue dest) {
      assert(expr->getBase()->getType()->isStringType() && "wrong function");
      assert(expr->getType()->isCharType() && "unexpected return type");
      auto baseGT = getGTForExpr(expr->getBase());
      auto indexGT = getGTForExpr(expr->getIndex());
      // getChar has a (string, int) -> char signature.
      return 
        emitBuiltinCall(BuiltinKind::getChar, std::move(dest), {baseGT, indexGT}, 
                        expr->getSourceRange());
    }

    /// Emits a CallExpr whose callee is a BuiltinMemberRef.
    RegisterValue emitBuiltinTypeMemberCall(CallExpr* call, RegisterValue dest) {
      auto membRef = dyn_cast<BuiltinMemberRefExpr>(call->getCallee());
      assert(membRef && "callee isn't a BuiltinMemberRefExpr");
      assert(membRef->isMethod() && "callee is not a method");
      // Collect all of the arguments in a single vector
      SmallVector<Expr*, 4> args;
      args.reserve(call->numArgs()+1);    // reserve enough space directly
      args.push_back(membRef->getBase()); // The BMR's base is always first arg
      for(Expr* expr: call->getArgs())    // Then push the call's args.
        args.push_back(expr);
      // Get the BuiltinTypeMemberKind
      auto kind = membRef->getBuiltinTypeMemberKind();
      // Dispatch to the appropriate emission function.
      switch (kind) {
        default: fox_unreachable("unknown BuiltinTypeMemberKind");
        #define ANY_MEMBER(ID) case BuiltinTypeMemberKind::ID:\
          return emit##ID(call, args, std::move(dest));
        #include "Fox/AST/BuiltinTypeMembers.def"
      }
    }

    //------------------------------------------------------------------------//
    // Builtin Type Member Emitters
    //------------------------------------------------------------------------//

    RegisterValue 
    emitArrayAppend(Expr* src, ArrayRef<Expr*> args, RegisterValue dest) {
      /// Emit a call to the arrAppend builtin.
      assert((args.size() == 2) 
        && "incorrect number of args for arrAppend");
      assert(!dest && "array.append returns void!");
      return emitBuiltinCall(
        BuiltinKind::arrAppend, 
        RegisterValue(), 
        { getGTForExpr(args[0]), getGTForExpr(args[1]) },
        src->getSourceRange()
      );
    }

    RegisterValue 
    emitArrayBack(Expr* src, ArrayRef<Expr*> args, RegisterValue dest) {
      /// Emit a call to the arrBack builtin.
      assert((args.size() == 1) 
        && "incorrect number of args for arrBack");
      return emitBuiltinCall(
        BuiltinKind::arrBack, 
        std::move(dest), 
        { getGTForExpr(args[0]) },
        src->getSourceRange()
      );
    }

    RegisterValue 
    emitArrayFront(Expr* src, ArrayRef<Expr*> args, RegisterValue dest) {
      /// Emit a call to the arrFront builtin.
      assert((args.size() == 1) 
        && "incorrect number of args for arrFront");
      return emitBuiltinCall(
        BuiltinKind::arrFront, 
        std::move(dest), 
        { getGTForExpr(args[0]) },
        src->getSourceRange()
      );
    }

    RegisterValue 
    emitArrayPop(Expr* src, ArrayRef<Expr*> args, RegisterValue dest) {
      /// Emit a call to the arrPop builtin.
      assert((args.size() == 1) 
        && "incorrect number of args for arrPop");
      assert(!dest && "array.pop returns void!");
      return emitBuiltinCall(
        BuiltinKind::arrPop, 
        std::move(dest), 
        { getGTForExpr(args[0]) },
        src->getSourceRange()
      );
    }

    RegisterValue 
    emitArraySize(Expr* src, ArrayRef<Expr*> args, RegisterValue dest) {
      /// Emit a call to the arrSize builtin.
      assert((args.size() == 1) 
        && "incorrect number of args for arrSize");
      return emitBuiltinCall(
        BuiltinKind::arrSize, 
        std::move(dest), 
        { getGTForExpr(args[0]) },
        src->getSourceRange()
      );
    }

    RegisterValue 
    emitArrayReset(Expr* src, ArrayRef<Expr*> args, RegisterValue dest) {
      /// Emit a call to the arrReset builtin.
      assert((args.size() == 1) 
        && "incorrect number of args for arrReset");
      assert(!dest && "array.reset returns void!");
      return emitBuiltinCall(
        BuiltinKind::arrReset, 
        std::move(dest), 
        { getGTForExpr(args[0]) },
        src->getSourceRange()
      );
    }

    RegisterValue 
    emitStringNumBytes(Expr* src, ArrayRef<Expr*> args, RegisterValue dest) {
      /// Emit a call to the strNumBytes builtin.
      assert((args.size() == 1) 
        && "incorrect number of args for strNumBytes");
      GenThunk gen = getGTForExpr(args[0]);
      return emitBuiltinCall(
        BuiltinKind::strNumBytes, 
        std::move(dest), 
        { getGTForExpr(args[0]) },
        src->getSourceRange()
      );
    }

    RegisterValue 
    emitStringLength(Expr* src, ArrayRef<Expr*> args, RegisterValue dest) {
      /// Emit a call to the strLength builtin.
      assert((args.size() == 1) 
        && "incorrect number of args for strLength");
      return emitBuiltinCall(
        BuiltinKind::strLength, 
        std::move(dest), 
        { getGTForExpr(args[0]) },
        src->getSourceRange()
      );
    }

    //------------------------------------------------------------------------//
    // "visit" methods 
    // 
    // These methods will perfom the actual tasks required to emit
    // the bytecode for an Expr.
    // They take a RegisterValue as argument. It's the destination register
    // but it can be omitted (pass a RegisterValue()), however when it is
    // present EVERY visitXXX method MUST store the result of the expression
    // inside it. This is enforced by an assertion in visit() in debug mode.
    // NOTE: For now it's unused, but it'll be used when generating assignements
    // to optimize them whenever possible.
    //------------------------------------------------------------------------//

    RegisterValue visit(Expr* expr, RegisterValue dest) {
      #ifndef NDEBUG
        // In debug mode, check that the destination is respected
        bool hadDest = dest.isAlive();
        regaddr_t expectedAddr = dest ? dest.getAddress() : 0;
        RegisterValue resultRV = Visitor::visit(expr, std::move(dest));
        if (hadDest) {
          assert(resultRV
            && "A destination register was provided but the expression did not "
               "produce any result");
          assert((expectedAddr == resultRV.getAddress())
           && "A destination register was provided but was not respected");
        }
        return resultRV;
      #else 
        return Visitor::visit(expr, std::move(dest));
      #endif
    }

    RegisterValue visit(Expr* expr) {
      // Directly use Visitor::visit so we bypass the useless checks
      // in visit(Expr*, RegisterValue)
      return Visitor::visit(expr, RegisterValue());
    }

    RegisterValue 
    visitBinaryExpr(BinaryExpr* expr, RegisterValue dest) { 
      assert((expr->getOp() != BinOp::Invalid)
        && "BinaryExpr with OpKind::Invalid past semantic analysis");
      if (expr->isAssignement()) {
        AssignementGenerator assignGen(bcGen, builder, *this);
        return assignGen.generate(expr, std::move(dest));
      }
      if (expr->isConcat())
        return emitConcatBinaryExpr(expr, std::move(dest));
      if (expr->getType()->isNumericOrBool())
        return emitNumericOrBoolBinaryExpr(expr, std::move(dest));
      fox_unreachable("Unknown BinaryExpr kind");
    }

    RegisterValue 
    visitCastExpr(CastExpr* expr, RegisterValue dest) {
      // Visit the child
      Expr* subExpr = expr->getChild();
      RegisterValue childReg = visit(subExpr);

      // If this is a useless cast (cast from a type to the same type)
      // just return childReg
      if(expr->isUseless()) return childReg;

      Type ty = expr->getType();
      Type subTy = subExpr->getType();
      regaddr_t childRegAddr = childReg.getAddress();

      dest = getDestReg(std::move(dest), {childReg});

      assert(dest && "no destination register selected");
      regaddr_t destAddr = dest.getAddress();

      // Casts to numeric types
      if (ty->isNumericType()) {
        // Numeric -> Numeric
        if (subTy->isNumericType()) {
          // We know it's a non-useless cast from a numeric type
          // to a different numeric type.
          if (ty->isDoubleType()) {
            assert(subTy->isIntType());
            // It's a Int -> Double cast
            builder.createIntToDoubleInstr(destAddr, childRegAddr);
          }
          else if (ty->isIntType()) {
            // It's a Double -> Int cast
            builder.createDoubleToIntInstr(destAddr, childRegAddr);
          }
          else 
            fox_unreachable("Unhandled numeric type kind");
        }
        // Numeric -> ?
        else {
          fox_unreachable("Unhandled BCGen situation "
            "(CastExpr from non-numeric to numeric");
        }
      }
      // Array casts
      else if (ty->isArrayType()) {
        // If the subExpr is simply an empty array literal, we don't have to
        // do anything except copying the sub expr's result if needed.
        if (auto arr = dyn_cast<ArrayLiteralExpr>(subExpr)) {
          if (arr->numElems() == 0) {
            if(childReg != dest)
              builder.createCopyInstr(destAddr, childReg.getAddress());
            return dest;
          }
        }
        fox_unimplemented_feature("CastExpr on Non-empty Array BCGen");
      }
      else
        fox_unreachable("unknown CastExpr kind");

      return dest;
    }

    RegisterValue 
    visitUnaryExpr(UnaryExpr* expr, RegisterValue dest) { 
      assert((expr->getOp() != UnOp::Invalid)
        && "UnaryExpr with OpKind::Invalid past semantic analysis");

      Expr* child = expr->getChild();

      // Handle the ToString operator
      if(expr->getOp() == UnOp::ToString)
        return emitToStringUnOp(expr, std::move(dest));

      // When we have an unary minus, and the child is int, bool or double literal, 
      // directly emit the literal with a negative
      // value instead of generating a NegInt or something.
      if (expr->getOp() == UnOp::Minus) {
        if (auto intLit = dyn_cast<IntegerLiteralExpr>(child))
          return visitIntegerLiteralExpr(intLit, std::move(dest), 
                                            /*asNegative*/ true);

        if (auto doubleLit = dyn_cast<DoubleLiteralExpr>(child))
          return visitDoubleLiteralExpr(doubleLit, std::move(dest), 
                                              /*asNegative*/ true);
      }

      // Else compile it normally
      RegisterValue childReg = visit(child);

      // Handle unary plus directly as it's a no-op
      if(expr->getOp() == UnOp::Plus) return childReg;

      regaddr_t childAddr = childReg.getAddress();

      RegisterValue destReg = getDestReg(std::move(dest), {childReg});
      regaddr_t destAddr = destReg.getAddress();

      // Unary LNot '!' is always applied on booleans, so we
      // compile it to a LNot in every scenario.
      if(expr->getOp() == UnOp::LNot)
        builder.createLNotInstr(destAddr, childAddr);

      // Unary Minus '-' is always applied on numeric types, and
      // the child's type should be the same numeric kind
      // as the expr's.
      else if (expr->getOp() == UnOp::Minus) {
        Type ty = expr->getType();
        assert(ty->isNumericType() && "Unary Minus on non-numeric types");
        // Decide what to emit based on the type of the UnaryExpr.
        if(ty->isIntType())
          builder.createNegIntInstr(destAddr, childAddr);
        else if(ty->isDoubleType()) 
          builder.createNegDoubleInstr(destAddr, childAddr);
        else fox_unreachable("Unknown numeric type kind");
      }
      else fox_unreachable("Unknown Unary Operator");

      return destReg;
    }

    RegisterValue visitSubscriptExpr(SubscriptExpr* expr, RegisterValue dest) {
      Expr* base = expr->getBase();
      // Subscript on strings
      if (base->getType()->isStringType())
        return emitStringSubscript(expr, std::move(dest));

      assert(base->getType()->isArrayType() && "not an array subscript");

      // arrGet takes the array as first parameter and the index second.
      return emitBuiltinCall(
        BuiltinKind::arrGet, 
        std::move(dest), 
        {getGTForExpr(base), getGTForExpr(expr->getIndex())},
        expr->getSourceRange()
      );
    }

    RegisterValue 
    visitDeclRefExpr(DeclRefExpr* expr, RegisterValue dest) { 
      ValueDecl* decl = expr->getDecl();

      // References to Functions
      if (auto func = dyn_cast<FuncDecl>(decl)) {
        auto fID = static_cast<func_id_t>(bcGen.getBCFunction(func).getID());
        dest = tryUse(std::move(dest));
        builder.createLoadFuncInstr(dest.getAddress(), fID);
        return dest;
      }

      // References to builtins
      if (auto builtin = dyn_cast<BuiltinFuncDecl>(decl)) {
        auto bID = builtin->getBuiltinKind();
        dest = tryUse(std::move(dest));
        builder.createLoadBuiltinFuncInstr(dest.getAddress(), bID);
        return dest;
      }

      assert((isa<VarDecl>(decl) || isa<ParamDecl>(decl))
        && "unknown ValueDecl kind");

      // Reference to Global variables (VarDecl)
      if (decl->isGlobal()) {
        VarDecl* var = dyn_cast<VarDecl>(decl);
        assert(var && var->isGlobal() && "not a global variable");
        // Just generate a GetGlobal
        dest = tryUse(std::move(dest));
        global_id_t id = bcGen.getGlobalVarID(var);
        builder.createGetGlobalInstr(id, dest.getAddress());
        return dest;
      }

      // Reference to local declarations (VarDecl or ParamDecl)
      RegisterValue varReg = regAlloc.useDecl(decl);
      if (dest && (dest != varReg)) {
        // If we have a destination register, emit a Copy instr so the result
        // is located in the dest reg (as requested).
        builder.createCopyInstr(dest.getAddress(), varReg.getAddress());
        return dest;
      }
      return varReg;
    }

    RegisterValue 
    visitBuiltinMemberRefExpr(BuiltinMemberRefExpr*, RegisterValue dest) {
      fox_unreachable("Shouldn't appear alone");
    }

    RegisterValue
    visitCallExpr(CallExpr* expr, RegisterValue dest) { 
      // If this CallExpr's callee is a BuiltinMemberRefExpr,
      // bail and let emitBuiltinTypeMemberCall do the work.
      if(isa<BuiltinMemberRefExpr>(expr->getCallee()))
        return emitBuiltinTypeMemberCall(expr, std::move(dest));

      // The list of expressions to compile, in order.
      SmallVector<Expr*, 8> exprs;
      exprs.reserve(1 + expr->numArgs());
      
      // First, the callee
      exprs.push_back(expr->getCallee());
      // Then the args
      {
        ArrayRef<Expr*> args = expr->getArgs();
        exprs.append(args.begin(), args.end());
      }

      // Reserve the registers
      SmallVector<RegisterValue, 8> regs;
      regs.reserve(exprs.size());
      regAlloc.allocateCallRegisters(regs, exprs.size());

      // Save the 'base' reg addr.
      regaddr_t baseAddr = regs.front().getAddress();

      // Compile the expressions
      assert(exprs.size() == regs.size());
      for (std::size_t k = 0, size = regs.size(); k < size; ++k) {
        // Move the registers out so the function can use them, but store
        // them back after.
        regs[k] = visit(exprs[k], std::move(regs[k]));
      }

      // Use CallVoid for void functions
      if (expr->getType()->isVoidType()) {
        assert(!dest 
          && "CallExpr has void type, but is expected to return a result");
        builder.createCallVoidInstr(baseAddr);
        return RegisterValue();
      }
      // Else just use 'Call'.
      // If there is no destination, any recyclable register in regs is a 
      // potential candidate for reusability.
      dest = getDestReg(std::move(dest), regs);
      builder.createCallInstr(baseAddr, dest.getAddress());
      return dest;
    }

    RegisterValue 
    visitCharLiteralExpr(CharLiteralExpr* expr, RegisterValue dest) { 
      dest = tryUse(std::move(dest));
      emitStoreIntConstant(dest.getAddress(), expr->getValue());
      return dest;
    }

    RegisterValue 
    visitIntegerLiteralExpr(IntegerLiteralExpr* expr, RegisterValue dest,
                            bool asNegative = false) {
      dest = tryUse(std::move(dest));
      FoxInt value = asNegative ? -expr->getValue() : expr->getValue();
      emitStoreIntConstant(dest.getAddress(), value);
      return dest;
    }

    RegisterValue 
    visitDoubleLiteralExpr(DoubleLiteralExpr* expr, RegisterValue dest,
                           bool asNegative = false) { 
      dest = tryUse(std::move(dest));
      FoxDouble value = asNegative ? -expr->getValue() : expr->getValue();
      auto kID = bcGen.getConstantID(value);
      builder.createLoadDoubleKInstr(dest.getAddress(), kID);
      return dest;
    }

    RegisterValue 
    visitBoolLiteralExpr(BoolLiteralExpr* expr, RegisterValue dest) { 
      dest = tryUse(std::move(dest));
      emitStoreIntConstant(dest.getAddress(), expr->getValue());
      return dest;
    }

    RegisterValue 
    visitStringLiteralExpr(StringLiteralExpr* expr, RegisterValue dest) { 
      dest = tryUse(std::move(dest));
      auto string = expr->getValue();
      if (string.size() == 0) // "" literal
        builder.createNewStringInstr(dest.getAddress());
      else                    // Other literals
        builder.createLoadStringKInstr(dest.getAddress(), 
                                       bcGen.getConstantID(string));
      return dest;
    }

    RegisterValue 
    visitArrayLiteralExpr(ArrayLiteralExpr* expr, RegisterValue dest) {
      Type arrayType = expr->getType();
      Type elemType = arrayType->castTo<ArrayType>()->getElementType();
      bool elemTypeIsRefType = elemType->isReferenceType();

      dest = tryUse(std::move(dest));
      regaddr_t arrAddr = dest.getAddress();

      {
        // Use the number of elements inside the array literal as the
        // initial size of the array, but max out at 65535 (the max of uint16,
        // the type of the second operand);
        auto initialSize = std::min<std::uint16_t>(expr->numElems(), 0xFFFFu);
        // Create the array
        if(elemTypeIsRefType)
          builder.createNewRefArrayInstr(arrAddr, initialSize);
        else
          builder.createNewValueArrayInstr(arrAddr, initialSize);
      }

      // Stop here if the array literal is empty.
      if(expr->numElems() == 0) return dest;

      // FIXME: Can this be made a bit more efficient?

      // Setup a call to arrAppend to fill the array
      SmallVector<RegisterValue, 4> callRegs;
      regAlloc.allocateCallRegisters(callRegs, 3);

      regaddr_t baseAddr = callRegs[0].getAddress();
      // Base = Reference to arrAppend.
      builder.createLoadBuiltinFuncInstr(baseAddr, BuiltinKind::arrAppend);

      // arg0 = reference to the array
      RegisterValue& arg0 = callRegs[1];
      // FIXME: Can we avoid the copy?
      builder.createCopyInstr(arg0.getAddress(), arrAddr);

      // arg1 = the expression.
      // We'll loop over each expression, generate them with arg1
      // as the destination then call the base and so on.
      RegisterValue& arg1 = callRegs[2];
      assert(arg1 && "arg1 is dead");
      for (Expr* elem : expr->getExprs()) {
        arg1 = visit(elem, std::move(arg1));    // gen the expr in arg1
        builder.createCallVoidInstr(baseAddr);  // Call arrAppend
      }

      return dest;
    }

    // ErrorExprs shouldn't be found past Sema
    RegisterValue 
    visitErrorExpr(ErrorExpr*, RegisterValue) { 
      fox_unreachable("ErrorExpr found past semantic analysis");
    }

    // UnresolvedDeclRefExprs shouldn't be found past Sema
    RegisterValue 
    visitUnresolvedDeclRefExpr(UnresolvedDeclRefExpr*, RegisterValue) { 
      fox_unreachable("UnresolvedDeclRefExpr found past semantic analysis");
    }

    // UnresolvedDotExprs shouldn't be found past Sema
    RegisterValue 
    visitUnresolvedDotExpr(UnresolvedDotExpr*, RegisterValue) { 
      fox_unreachable("UnresolvedDotExpr found past semantic analysis");
    }

};

//----------------------------------------------------------------------------//
// AssignementGenerator : Implementation
//----------------------------------------------------------------------------// 

BCGen::AssignementGenerator::
AssignementGenerator(BCGen& gen, BCBuilder& builder, ExprGenerator& exprGen)
  : Generator(gen, builder), exprGen(exprGen), regAlloc(exprGen.regAlloc) {}

RegisterValue 
BCGen::AssignementGenerator::generate(BinaryExpr* expr, RegisterValue dest) {
  Expr* lhs = expr->getLHS();
  Expr* rhs = expr->getRHS();
  BinOp op = expr->getOp();
#ifndef NDEBUG
  // In debug mode, check that the destination is respected
  bool hadDest = dest.isAlive();
  regaddr_t expectedAddr = dest ? dest.getAddress() : 0;
  RegisterValue resultRV = visit(lhs, rhs, std::move(dest), op);
  if (hadDest) {
    assert(resultRV
      && "A destination register was provided but the expression did not "
          "produce any result");
    assert((expectedAddr == resultRV.getAddress())
      && "A destination register was provided but was not respected");
  }
  assert(resultRV && "Assignements always return a value!");
  return resultRV;
#else 
  return visit(lhs, rhs, std::move(dest), op);
#endif
}

RegisterValue 
BCGen::AssignementGenerator::visitSubscriptExpr(SubscriptExpr* expr, 
                                                Expr* src, 
                                                RegisterValue dest, 
                                                BinOp op) {
  // VM doesn't support arrays yet
  assert((op == BinOp::Assign) && "Unsupported assignement type");
  op; // Avoid 'unreferenced formal parameter'

  // arrSet expects the array first, the index second and the value third.
  // It returns its third argument.
  RegisterValue rtr = exprGen.emitBuiltinCall(
    BuiltinKind::arrSet, 
    std::move(dest),
    {
      exprGen.getGTForExpr(expr->getBase()),
      exprGen.getGTForExpr(expr->getIndex()),
      exprGen.getGTForExpr(src)
    },
    expr->getSourceRange()
  );

  return rtr;
}

RegisterValue BCGen::AssignementGenerator::
visitDeclRefExpr(DeclRefExpr* dst, Expr* src, RegisterValue dest, BinOp op) {
  assert((op == BinOp::Assign) && "Unsupported assignement type");
  op;
  ValueDecl* decl = dst->getDecl();

  // Setting a local declaration
  if (decl->isLocal()) {
    RegisterValue rtr = exprGen.generate(src, regAlloc.useDecl(decl));
    return copyInDest(std::move(dest), std::move(rtr));
  }

  // Setting a global declaration (VarDecl)
  VarDecl* var = dyn_cast<VarDecl>(decl);
  assert(var && var->isGlobal() && "not a global variable");

  // Gen the expr in dest 
  dest = exprGen.generate(src, std::move(dest));
  assert(dest && "expr has no result");

  // Gen a SetGlobal
  global_id_t id = bcGen.getGlobalVarID(var);
  builder.createSetGlobalInstr(id, dest.getAddress());

  return dest;
}

//----------------------------------------------------------------------------//
// BCGen Entrypoints
//----------------------------------------------------------------------------//

RegisterValue BCGen::genExpr(BCBuilder& builder, 
                             RegisterAllocator& regAlloc, Expr* expr) {
  return ExprGenerator(*this, builder, regAlloc).generate(expr);
}

void BCGen::genDiscardedExpr(BCBuilder& builder, 
                             RegisterAllocator& regAlloc, Expr* expr) {
  ExprGenerator(*this, builder, regAlloc).generate(expr);
}
