////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : Flags.hpp											
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
// This class declares enum for each flag kind, and a FlagManager that groups them all and provide nice and easy access to the flag values.
// 
// Theses flags are not to be confused with command line "flags" (or args), even if they're somewhat related to them. In the end, I expect to have 
// a wide variety of flags used to change the interpreter's behaviour, but they won't always have a command line arg counterpart. Some might just be here
// for tweaking or customizing the interpreter's behaviour.
////------------------------------------------------------////

#pragma once

#include <map>

namespace Moonshot
{
	enum class FoxFlag
	{
		#define FLAG(FLAG_NAME,FLAG_BASE_VAL) FLAG_NAME,
		#include "FoxFlags.def"	
	};

	enum class CommonFlag
	{
		#define FLAG(FLAG_NAME,FLAG_BASE_VAL) FLAG_NAME,
		#include "CommonFlags.def"
	};

	class FlagsManager
	{
		public:
			FlagsManager() = default;

			// FoxFlags
			bool isSet(const FoxFlag& ff) const;
			void set(const FoxFlag& ff);
			void unset(const FoxFlag& ff);

			// CommonFlag
			bool isSet(const CommonFlag& ff) const;
			void set(const CommonFlag& ff);
			void unset(const CommonFlag& ff);
		private:
			template<typename KEY, typename DATA>
			inline bool existsInMap(const std::map<KEY, DATA> &mmap, const KEY& key) const
			{
				return mmap.find(key) != mmap.end();
			}

			std::map<FoxFlag, bool> fox_flags_ = 
			{
				#define FLAG(FLAG_NAME,FLAG_BASE_VAL) { FoxFlag::FLAG_NAME, FLAG_BASE_VAL },
				#include "FoxFlags.def"	
			};

			std::map<CommonFlag, bool> common_flags_ =
			{
				#define FLAG(FLAG_NAME,FLAG_BASE_VAL) { CommonFlag::FLAG_NAME, FLAG_BASE_VAL },
				#include "CommonFlags.def"
			};
	};
}