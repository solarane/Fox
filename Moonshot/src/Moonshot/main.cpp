#include <iostream>
#include <Windows.h>
#include "Moonshot/Tests/Manager/TestManager.hpp"
using namespace Moonshot;

/*
	This is a short term roadmap for the project.
		Implement the rest of the Parsing functions & AST Structure
			- Member access
			- Function calls
			- Resolved/Unresolved IDs
			- Import
			- DeclContexts (Symbols table and friends)

		While doing so, refactor the whole project.
			Delete /eval/ folder. The code contained in that is complete trash.
			Delete the typecheck class in prevision of upcoming rewrite.
			Delete /datamap/, it's filth.
			Take a peek at everything inside /Common/Types to rename stuff that needs it, make names more explicit. On top of my head:
				FoxValue can be deleted altogether, and just use different ASTLiteral nodes. 
				Look at everything that's not needed anymore an throw it in the trash too.
			Also, take a peek a the general Option system. Maybe let it use .def files too.

			After that, the project will be purged and can start again on healthier grounds.
			I'm also thinking about switching to a CMake build system and start coding on Linux a bit more.
			This will make the project better in the long run I think (more people open to contribute)

		Rework the visitor pattern, add ASTWalker, ASTVisitor, ASTTraversal. (use a clang/swift-like design pattern)
		Rewrite the whole test system, possibily by using .def files and a whole macro metaprogramming system.
			- Separate tests in categories : lexer, parser, semantics, ...
			- Add more control over tests individually: should it print the AST, should it just print "PASSED/FAILED", show them in condensed form.

		Recreate semantic analysis phase in a /sema/ folder
			- Name Resolver
			- Type Checker
*/

int main()
{
	SetConsoleOutputCP(CP_UTF8);
	setvbuf(stdout, nullptr, _IOFBF, 1000);

	std::ios_base::sync_with_stdio(false); // We don't use printf, so we don't need to sync with stdio (CppCoreGuidelines SL.io.10)
	Context context;
	context.optionsManager_.addAttr(OptionsList::exprtest_printAST,true);
	Test::TestManager ts(context);
	ts.addDefaultTests();
	ts.runTests(true);
	std::cout << "Finished. Press any key to continue.\n";
	std::cin.get();
	return 0;
}
