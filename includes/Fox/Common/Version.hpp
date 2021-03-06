//----------------------------------------------------------------------------//
// Part of the Fox project, licensed under the MIT license.
// See LICENSE.txt in the project root for license information.     
// File : Version.hpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//
// This file contains the version Major/Minor/Patch and other information
// About the current version of the Fox Project
//----------------------------------------------------------------------------//

// Utility macros
#define FOX_UTIL_TOSTR_HELPER(X) #X
#define FOX_UTIL_TOSTR(X) FOX_UTIL_TOSTR_HELPER(X)

// Version numbers
#define FOX_VER_MAJOR 0
#define FOX_VER_MINOR 1
#define FOX_VER_PATCH 0

#define FOX_DEV_PHASE "inDev"  

// Description of each (planned) "development" phase
// inDev: Nothing is complete/working as intended  (Make it work) -> 0.1
// Alpha: The interpreter is working and can run basic Fox code. 
//       This phase is mostly improving the existing code :
//       Writing more tests, Refactoring, DRYing, etc. 
//       At the end of this phase, the source code should be
//       clean and maintainable in the long run (Make it right)
// Beta: Optimization-oriented development phase. 
//       This include refactoring/rewriting slow code, 
//       profiling, etc (Make it fast)  
//      
// Release: The interpreter is considered good enough to be usable by the public

#define FOX_VERSION FOX_UTIL_TOSTR(FOX_VER_MAJOR) \
 "." FOX_UTIL_TOSTR(FOX_VER_MINOR) "."\
 FOX_UTIL_TOSTR(FOX_VER_PATCH)
#define FOX_VERSION_COMPLETE FOX_VERSION " (" FOX_DEV_PHASE ")"
