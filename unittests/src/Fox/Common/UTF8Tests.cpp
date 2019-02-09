﻿//----------------------------------------------------------------------------//
// This file is part of the Fox project.        
// See the LICENSE.txt file at the root of the project for license information.            
// File : UTF8Tests.cpp                      
// Author : Pierre van Houtryve                
//----------------------------------------------------------------------------//
// (Unit) Tests for the UTF8 String manipulator.
//----------------------------------------------------------------------------//

#include "gtest/gtest.h"
#include "Support/TestUtils.hpp"
#include "Fox/Common/StringManipulator.hpp"
#include <cwctype>    // std::iswspace

using namespace fox;
using namespace fox::test;

/*
  getTextStat : return false if exception happened, and puts e.what() inside exception_details.
  returns true if success and places the results inside linecount, charcount, spacecount.
*/
bool getTextStats(StringManipulator &manip, unsigned int& linecount, 
                  unsigned int& charcount, unsigned int& spacecount, 
                  std::string exception_details = "") {
  try {
    while (!manip.eof()) {
      const auto cur = manip.getCurrentChar();
      if (cur == '\n')
        ++linecount;
      else if (cur == '\r' && manip.peekNext() == '\n') {
        manip.advance();
        ++linecount;
      }
      else {
        if (std::iswspace(static_cast<wchar_t>(cur)))
          ++spacecount;
        ++charcount;
      }
      manip.advance();
    }
  }
  catch (std::exception& e) {
    exception_details = e.what();
    return false;
  }
  return true;
}

TEST(UTF8Tests,BronzeHorseman) {
  // Open test file
  std::string file_content;
  std::string file_path("lexer/utf8/bronzehorseman.txt");
  ASSERT_TRUE(readFileToString(file_path, file_content)) 
    << "Could not open test file \"" << file_path << '"';

  // Prepare string manipulator & other variables
  StringManipulator manip(file_content);
  unsigned int linecount = 1, charcount = 0, spacecount = 0;
  std::string exception_details;

  // Get text statistics
  EXPECT_TRUE(getTextStats(manip, linecount, charcount, 
    spacecount, exception_details)) 
    << "Test failed, exception thrown while iterating "
       "through the string. Exception details:" << exception_details;
  
  // Expected text statistics
  // 11 Lines
  // 278 Characters
  // 44 Spaces
  // 511 Bytes
  // 288 Codepoints
  EXPECT_EQ(11, linecount);
  EXPECT_EQ(268, charcount);
  EXPECT_EQ(34, spacecount);
  EXPECT_EQ(511, manip.getSizeInBytes());
  EXPECT_EQ(288, manip.getSizeInCodepoints());
}

TEST(UTF8Tests, ASCIIDrawing) {
  // Open test file
  std::string file_content;
  std::string file_path("lexer/utf8/ascii.txt");
  ASSERT_TRUE(readFileToString(file_path, file_content)) << "Could not open test file \"" << file_path << '"';

  // Prepare string manipulator & other variables
  StringManipulator manip(file_content);
  unsigned int linecount = 1, charcount = 0, spacecount = 0;
  std::string exception_details;

  // Get text statistics
  EXPECT_TRUE(getTextStats(manip, linecount, charcount, 
    spacecount, exception_details)) 
    << "Test failed, exception thrown while iterating "
       "through the string. Exception details:" << exception_details;

  // Expected text statistics
  // 18 lines
  // 1190 Characters
  // 847 Spaces
  // 1207 bytes
  // 1207 codepoints
  EXPECT_EQ(18, linecount);
  EXPECT_EQ(1173, charcount);
  EXPECT_EQ(830, spacecount);
  EXPECT_EQ(1207, manip.getSizeInBytes());
  EXPECT_EQ(1207, manip.getSizeInCodepoints());
}

TEST(UTF8Tests, Substring) {
  // Open test file : bronze
  std::string bronze_content;
  std::string bronze_path("lexer/utf8/bronzehorseman.txt");
  ASSERT_TRUE(readFileToString(bronze_path, bronze_content)) 
    << "Could not open test file \"" << bronze_path << '"';

  // Open test file : substr
  std::string expected_substr;
  std::string substr_path("lexer/utf8/bronzehorseman.substr.txt");
  ASSERT_TRUE(readFileToString(substr_path, expected_substr)) 
    << "Could not open test file \"" << substr_path << '"';
  StringManipulator::removeBOM(expected_substr);

  // Prepare string manipulator
  StringManipulator manip(bronze_content);

  string_view substr = manip.substring(10, 9);

  EXPECT_EQ(expected_substr, substr) << "Substring was not correct";
}

TEST(UTF8Tests, IndexOfCurCharValidity) {
  // Open test file : bronze
  std::string bronze_content;
  std::string bronze_path("lexer/utf8/bronzehorseman.txt");
  ASSERT_TRUE(readFileToString(bronze_path, bronze_content))
    << "Could not open test file \"" << bronze_path << '"';

  
  // Prepare string manipulator
  StringManipulator manip1,manip2;
  manip1.setStr(bronze_content);
  manip2.setStr(bronze_content);

  for (auto k(0); k < 15; k++)
    manip1.advance();

  manip2.advance(15);

  // test if index is valid
  EXPECT_EQ(15, manip1.getIndexInCodepoints());
  EXPECT_EQ(manip1.getCurrentChar(), manip2.getCurrentChar());
}