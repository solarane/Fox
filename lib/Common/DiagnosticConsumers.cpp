////------------------------------------------------------////
// This file is a part of The Moonshot Project.				
// See LICENSE.txt for license info.						
// File : DiagnosticConsumers.cpp											
// Author : Pierre van Houtryve								
////------------------------------------------------------//// 
//			SEE HEADER FILE FOR MORE INFORMATION			
////------------------------------------------------------////

#include "Fox/Common/DiagnosticConsumers.hpp"
#include "Fox/Common/Diagnostic.hpp"
#include "utfcpp/utf8.hpp"
#include <cassert>
#include <string>
#include <sstream>

using namespace fox;

std::string DiagnosticConsumer::getLocInfo(SourceManager& sm, const SourceRange& range, bool isFileWide) const
{
	if (!range)
		return "<unknown>";

	CompleteLoc beg = sm.getCompleteLocForSourceLoc(range.getBegin());

	std::stringstream ss;
	ss << "<" << beg.fileName << '>';

	// Don't display the column/line for file wide diags
	if (!isFileWide)
		ss << ':' << beg.line << ':' << beg.column;

	// A better approach (read: a faster approach) 
	// would be to have a special method in the SourceManager calculating the preciseLoc
	// for a SourceRange (so we avoid calling "getCompleteLocForSourceLoc" twice)
	if (range.getOffset() != 0)
	{
		CompleteLoc end = sm.getCompleteLocForSourceLoc(range.getEnd());
		ss << "-" << end.column;
	}
	return ss.str();
}

std::size_t DiagnosticConsumer::removeIndent(std::string& str) const
{
	std::size_t indent = 0;
	// Get number of char that are spaces/tabs at the beginning of the line
	for (auto it = str.begin(); it != str.end(); str.erase(0, 1))
	{
		if ((*it == ' ') || (*it == '\t'))
			indent++;
		else
			break;
	}

	// Erase at the end
	for (auto it = str.rbegin(); it != str.rend(); it++)
	{
		if ((*it == ' ') || (*it == '\t'))
			str.pop_back();
		else
			break;
	}

	return indent;
}

std::string DiagnosticConsumer::diagSevToString(DiagSeverity ds) const
{
	switch (ds)
	{
		case DiagSeverity::IGNORE:
			return "Ignored";
		case DiagSeverity::NOTE:
			return "Note";
		case DiagSeverity::WARNING:
			return "Warning";
		case DiagSeverity::ERROR:
			return "Error";
		case DiagSeverity::FATAL:
			return "Fatal";
	}
	return "<Unknown Severity>";
}


StreamDiagConsumer::StreamDiagConsumer(SourceManager &sm, std::ostream & stream) : os_(stream), sm_(sm)
{

}

void StreamDiagConsumer::consume(const Diagnostic& diag)
{
	os_ << getLocInfo(sm_, diag.getRange(), diag.isFileWide())
		<< " - " 
		<< diagSevToString(diag.getDiagSeverity()) 
		<< " - " 
		<< diag.getDiagStr() 
		<< "\n";

	if (!diag.isFileWide() && diag.hasRange())
		displayRelevantExtract(diag);
}

// Helper method for "displayRelevantExtract" which creates the "underline" string
std::string createUnderline(char underlineChar, const std::string& str, std::string::const_iterator beg, std::string::const_iterator end)
{
	std::string line = "";

	std::string::const_iterator strBeg = str.begin();
	std::size_t spacesBeforeCaret = utf8::distance(strBeg, beg);

	for (std::size_t k = 0; k < spacesBeforeCaret; k++)
		line += ' ';

	std::size_t numCarets = 1 + utf8::distance(beg, end);
	for (std::size_t k = 0; k < numCarets; k++)
	{
		// Stop generating carets if the caretLine is longer
		// than the line's size + 1 (to allow a caret at the end of the line)
		if (line.size() > (line.size() + 1))
			break;

		line += underlineChar;
	}

	return line;
}

// Embeds "b" into "a", meaning that every space in a will be replaced with
// the character at the same position in b, and returns the string
// Example: embed("  ^  ", " ~~~ ") returns " ~^~ "
std::string embedString(const std::string& a, const std::string& b)
{
	std::string out;
	for (std::size_t k = 0, sz = a.size(); k < sz; k++)
	{
		if ((a[k] == ' ') && (k < b.size()))
		{
			out += b[k];
			continue;
		}
		out += a[k];
	}

	if (b.size() > a.size())
	{
		for (std::size_t k = a.size(); k < b.size(); k++)
			out += b[k];
	}

	return out;
}

void StreamDiagConsumer::displayRelevantExtract(const Diagnostic& diag)
{
	assert(diag.hasRange() && "Cannot use this if the diag does not have a valid range");

	auto range = diag.getRange();
	auto eRange = diag.getExtraRange();
	SourceLoc::idx_type lineBeg = 0;

	// Get the line, remove it's indent and display it.
	std::string line = sm_.getLineAtLoc(diag.getRange().getBegin(), &lineBeg);

	// Remove any indent, and offset the lineBeg accordingly
	lineBeg += removeIndent(line);

	std::string underline;

	// Create the carets underline (^) 
	{	
		auto beg = line.begin() + (range.getBegin().getIndex() - lineBeg);
		auto end = line.begin() + (range.getEnd().getIndex() - lineBeg);
		underline = createUnderline('^', line, beg, end);
	}

	// If needed, create the extra range underline (~)
	if(diag.hasExtraRange())
	{
		assert((diag.getExtraRange().getFileID() == diag.getRange().getFileID())
			&& "Ranges don't belong to the same file");

		auto beg = line.begin() + (eRange.getBegin().getIndex() - lineBeg);
		auto end = line.begin() + (eRange.getEnd().getIndex() - lineBeg);
		underline = embedString(underline, createUnderline('~', line, beg, end));
	}

	// Display the line
	os_ << '\t' << line << '\n';
	// Display the carets
	os_ << '\t' << underline << '\n';
}