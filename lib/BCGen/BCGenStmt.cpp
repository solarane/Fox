//----------------------------------------------------------------------------//
// Part of the Fox project, licensed under the MIT license.
// See LICENSE.txt in the project root for license information.     
// File : BCGenStmt.cpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//

#include "Fox/BCGen/BCGen.hpp"
#include "Registers.hpp"
#include "Fox/AST/Stmt.hpp"
#include "Fox/AST/ASTVisitor.hpp"
#include "Fox/BC/BCBuilder.hpp"
#include "Fox/Common/Errors.hpp"

using namespace fox;

//----------------------------------------------------------------------------//
// StmtGenerator 
//----------------------------------------------------------------------------//

class BCGen::StmtGenerator : public Generator,
                             StmtVisitor<StmtGenerator, void> {
  using Visitor = StmtVisitor<StmtGenerator, void>;
  friend Visitor;
  public:
    StmtGenerator(BCGen& gen, BCModuleBuilder& builder,
                  RegisterAllocator& regAlloc): 
      Generator(gen, builder), regAlloc(regAlloc) {}

    void generate(Stmt* stmt) {
      visit(stmt);
    }

    RegisterAllocator& regAlloc;

  private:
    using jump_offset_t = decltype(Instruction::Jump.arg);
    using condjump_offset_t = decltype(Instruction::CondJump.arg1);
    
    static constexpr jump_offset_t 
    max_jump_offset = std::numeric_limits<jump_offset_t>::max();

    static constexpr jump_offset_t 
    min_jump_offset = std::numeric_limits<jump_offset_t>::min();

    //------------------------------------------------------------------------//
    // "emit" and "gen" methods 
    // 
    // These methods perform some generalized tasks related to bytecode
    // emission
    //------------------------------------------------------------------------//

    void genNode(ASTNode node) {
      if(Decl* decl = node.dyn_cast<Decl*>())
        bcGen.genLocalDecl(builder, regAlloc, decl);
      else if(Expr* expr = node.dyn_cast<Expr*>()) 
        bcGen.genDiscardedExpr(builder, regAlloc, expr);
      else if(Stmt* stmt = node.dyn_cast<Stmt*>()) 
        visit(stmt);
      else 
        fox_unreachable("Unknown ASTNode kind");
    }

    // This calculates the offset needed to jump to the first
    // instruction after 'last'. It also checks that the jump
    // offset isn't too large.
    jump_offset_t calculateJumpOffset(BCModule::instr_iterator first,
                                      BCModule::instr_iterator last) {
      if(first == last) 
        return 0;

      bool isNegative = false;
      if (last < first) {
        std::swap(first, last);
        isNegative = true;
      }

      // Calculate the distance between the first and the last iterator
      auto diff = distance(first, last);

      // TODO: Replace this assertion by a proper 'fatal' diagnostic explaining
      // the problem. Maybe pass a lambda 'onOutOfRange' as parameter to
      // this function and call onOutOfRange() + return 0; when we try to
      // jump too far.
      assert((diff <= static_cast<std::size_t>(max_jump_offset))
        && "Jump is out of range!");

      // Convert it to jump_offset_t now that we know that it's safe.
      auto offset = static_cast<jump_offset_t>(diff);

      assert((offset != 0) && "offset cannot be zero");

      // Return, applying the minus if needed
      return isNegative ? (-offset) : offset;
    }

    //------------------------------------------------------------------------//
    // "visit" methods 
    // 
    // Theses methods will perfom the actual tasks required to emit
    // the bytecode for a statement.
    //------------------------------------------------------------------------//

    void visitCompoundStmt(CompoundStmt* stmt) {
      // Just visit all the nodes
      for(ASTNode node : stmt->getNodes())
        genNode(node);
    }

    void visitConditionStmt(ConditionStmt* stmt) {
      BCModule& theModule = builder.getModule();

      {
        // First, generate the condition
        RegisterValue condReg = 
          bcGen.genExpr(builder, regAlloc, stmt->getCond());

        // Then generate a condjump. This condjump will, if true, skip the jump
        // to the else, so we'll just jump 1 instruction (since jump offsets
        // are relative to the next instruction)
        builder.createCondJumpInstr(condReg.getAddress(), 1);
      }

      // Now, create a jump to the code that needs to be executed
      // if the condition isn't satisfied.
      auto jumpIfNot = builder.createJumpInstr(0);

      // Compile the 'then'
      visit(stmt->getThen());

      auto lastThenInstr = theModule.instrs_back();

      // Compile the 'else' if present
      if (CompoundStmt* elseBody = stmt->getElse()) {
        // Create a jump to the end of the condition (same as 'jumpToElse')
        // so the if's code doesn't fall-through and executes the else too.
        auto jumpEnd = builder.createJumpInstr(0);
        // Now we can complete 'jumpIfNot' so it executes the else.
        jumpIfNot->Jump.arg = calculateJumpOffset(jumpIfNot, jumpEnd);
        // Compile the else
        visit(elseBody);
        // Complete 'jumpEnd' so it jumps to the last instruction emitted.
        jumpEnd->Jump.arg = calculateJumpOffset(jumpEnd, 
                                                theModule.instrs_back());
      }
      // No 'else'
      else {
        // Just complete 'jumpToElse' to jump after the last instruction
        // emitted.
        // FIXME: Is this safe? We'll probably have instruction be generated
        // after (at least a return) but it can't be guaranteed here.
        jumpIfNot->Jump.arg = calculateJumpOffset(jumpIfNot, 
                                                  theModule.instrs_back());
      }
    }

    void visitWhileStmt(WhileStmt*) {
      fox_unimplemented_feature("WhileStmt BCGen");
    }

    void visitReturnStmt(ReturnStmt*) {
      fox_unimplemented_feature("ReturnStmt BCGen");
    }
};

//----------------------------------------------------------------------------//
// BCGen Entrypoints
//----------------------------------------------------------------------------//

void BCGen::genStmt(BCModuleBuilder& builder, 
                    RegisterAllocator& regAlloc, 
                    Stmt* stmt) {
  assert(stmt && "stmt is null");
  StmtGenerator(*this, builder, regAlloc).generate(stmt);
}