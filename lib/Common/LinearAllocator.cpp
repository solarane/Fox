//----------------------------------------------------------------------------//
// This file is a part of The Moonshot Project.        
// See LICENSE.txt for license info.            
// File : LinearAllocator.cpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//

#include "Fox/Common/LinearAllocator.hpp"
#include <iostream>

using namespace fox;

void detail::doLinearAllocatorDump(std::size_t numPools, std::size_t poolSize,
  std::size_t bytesInCurrentPool, std::size_t totalBytesUsed) {
  std::cerr << "(Pools Size: " << poolSize << ")\n"
   << "Pools: " << numPools << "\n"
   << "Bytes in current pool: " << bytesInCurrentPool << "\n"
   << "Total bytes: " << totalBytesUsed << "\n";
}