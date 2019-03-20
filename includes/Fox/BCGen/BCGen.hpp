//----------------------------------------------------------------------------//
// Part of the Fox project, licensed under the MIT license.
// See LICENSE.txt in the project root for license information.     
// File : BCGen.hpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//
// This file contains the interface to code generation for Fox, which
// converts a Fox AST into bytecode.
//----------------------------------------------------------------------------//

#pragma once

namespace fox {
  class ASTContext;
  class DiagnosticEngine;
  class BCModuleBuilder;
  class Expr;
  class BCGen {
    public:
      BCGen(ASTContext& ctxt);

      // Generates (emits) the bytecode for an expression "expr" using the 
      // builder "builder".
      void emitExpr(BCModuleBuilder& builder, Expr* expr);

      ASTContext& ctxt;
      DiagnosticEngine& diagEngine;

    private:
      class Generator;
      class ExprGenerator;

  };

  // Common base class for every "generator".
  class BCGen::Generator {
    public:
      BCGen& bcGen;
      DiagnosticEngine& diagEngine;
      ASTContext& ctxt;
      BCModuleBuilder& builder;

    protected:
      Generator(BCGen& bcGen, BCModuleBuilder& builder) : 
        bcGen(bcGen), builder(builder),
        diagEngine(bcGen.diagEngine), ctxt(bcGen.ctxt) {}
  };
}