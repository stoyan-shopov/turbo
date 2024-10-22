/*
 * Copyright (C) 2021 Stoyan Shopov <stoyan.shopov@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QTextCharFormat>
#include <QPlainTextEdit>
#include <QMessageBox>

#include "breakpoint-cache.hxx"
#include "source-files-cache.hxx"
#include "gdb-mi-parser.hxx"

class DisassemblyCache
{
public:
	struct DisassemblyBlock
	{
		enum KIND
		{
			INVALID = 0,
			SOURCE_LINE,
			DISASSEMBLY_LINE,
		}
		kind;
		union
		{
			uint64_t	address;
			int		lineNumber;
		};
		QString		fullFileName;
		DisassemblyBlock(enum KIND kind, uint64_t addressOrLineNumber, const QString & fullFileName = "")
		{ this->kind = kind, this->address = addressOrLineNumber, this->fullFileName = fullFileName; }
	};
private:
	std::unordered_map<uint64_t /* address */, int /* textLineNumber */> disassemblyLines;
	std::unordered_map<QString /* fileName */, std::unordered_map<int /* lineNumber */, std::unordered_set<int /* textLineNumber */>>> sourceLines;

	QTextCharFormat enabledBreakpointFormat;
	QTextCharFormat disabledBreakpointFormat;
	QTextCharFormat currentPCLineFormat;
	std::vector<struct DisassemblyBlock> disassemblyBlocks;
	const struct DisassemblyBlock invalidDisassemblyBlock = DisassemblyBlock(DisassemblyBlock::INVALID, -1, "<<< invalid >>>");
public:
	DisassemblyCache()
	{
		enabledBreakpointFormat.setProperty(QTextFormat::FullWidthSelection, true);
		enabledBreakpointFormat.setBackground(QBrush(Qt::red));
		disabledBreakpointFormat.setProperty(QTextFormat::FullWidthSelection, true);
		disabledBreakpointFormat.setBackground(QBrush(Qt::darkRed));
		currentPCLineFormat.setProperty(QTextFormat::FullWidthSelection, true);
		currentPCLineFormat.setBackground(QBrush(Qt::cyan));
	}

	const struct DisassemblyBlock & disassemblyBlockForTextBlockNumber(int blockNumber)
	{ return (blockNumber < disassemblyBlocks.size()) ? disassemblyBlocks.at(blockNumber) : invalidDisassemblyBlock; }

	void generateDisassemblyDocument(const GdbMiParser::MIList * disassembly, SourceFilesCache & sourceFilesCache, QString & htmlDocument)
	{
		disassemblyLines.clear();
		sourceLines.clear();
		disassemblyBlocks.clear();

		int currentLine = 0;

		htmlDocument = ("<!DOCTYPE html>"
			 "<html>"
			 "<body>");
		std::function<void(const GdbMiParser::MITuple & asmRecord)> processAsmRecord = [&](const GdbMiParser::MITuple & asmRecord) -> void
		{
			std::string address, opcodes, mnemonics, funcName, offset;
			for (const auto & line_details : asmRecord.map)
			{
				if (line_details.first == "address")
					address = line_details.second->asConstant()->constant();
				else if (line_details.first == "func-name")
					funcName = line_details.second->asConstant()->constant();
				else if (line_details.first == "offset")
					offset = line_details.second->asConstant()->constant();
				else if (line_details.first == "opcodes")
					opcodes = line_details.second->asConstant()->constant();
				else if (line_details.first == "inst")
					mnemonics = line_details.second->asConstant()->constant();
			}
			QString s = QString::fromStdString(address + '\t' + opcodes + '\t' + mnemonics);
			if (funcName.length() && offset.length())
				s += QString::fromStdString("\t; " + funcName + "+" + offset);
			QString backgroundColor = "PowderBlue";
			bool ok;
			uint64_t t = QString::fromStdString(address).toULongLong(& ok, 0);
			htmlDocument += QString("<p style=\"background-color:%1;\"><pre>%2</pre></p>")
					.arg(backgroundColor)
					.arg(s);
			disassemblyLines.operator [](t) = currentLine;

			disassemblyBlocks.push_back(DisassemblyBlock(DisassemblyBlock::DISASSEMBLY_LINE, t));
			currentLine ++;
		};
		for (const auto & d : disassembly->results)
		{
			if (d.variable == "src_and_asm_line")
			{
				const GdbMiParser::MITuple * t = d.value->asTuple();
				bool ok = false;
				int lineNumber = -1;
				QString fullFileName;
				auto i = t->map.find("line");
				if (i != t->map.cend())
					lineNumber = QString::fromStdString(i->second->asConstant()->constant()).toInt(& ok, 0);
				i = t->map.find("fullname");
				if (i != t->map.cend())
					fullFileName = QString::fromStdString(i->second->asConstant()->constant());
				i = t->map.find("line_asm_insn");
				{
					if (ok && lineNumber > 0 && !fullFileName.isEmpty())
					{
						QString errorMessage;
						auto sourceData = sourceFilesCache.getSourceFileCacheData(fullFileName, errorMessage);
						QString backgroundColor = "Azure";

						sourceLines.operator [](fullFileName).operator [](lineNumber).insert(currentLine);
						if (sourceData && lineNumber - 1 < sourceData->sourceCodeTextlines.length())
						{
							htmlDocument += QString("<p style=\"background-color:%1;\"><pre>%2: %3</pre></p>")
									.arg(backgroundColor)
									.arg(lineNumber).arg(sourceData->sourceCodeTextlines.at(lineNumber - 1));
							currentLine ++;
						}
						else
						{
							htmlDocument += QString("<p style=\"background-color:%1;\"><pre>%2: %3</pre></p>")
									.arg(backgroundColor)
									.arg(lineNumber).arg(fullFileName);
							currentLine ++;
						}
						disassemblyBlocks.push_back(DisassemblyBlock(DisassemblyBlock::SOURCE_LINE, lineNumber, fullFileName));
					}
					for (const auto & asmRecord : i->second->asList()->values)
						processAsmRecord(* asmRecord->asTuple());
				}
			}
			else
			{
				QMessageBox::critical(0, "Internal frontend error", "Internal frontend error - failed to parse gdb response. Please, report this");
				return;
			}
		}
		/* If this is a disassembly of code, for which there is no debug information available, the reply from
		 * gdb will be a list of tuples, which will be stored as a list of values, not as a list of results, as handled above. */
		for (const auto & d : disassembly->values)
			if (d->asTuple())
				processAsmRecord(* d->asTuple());
		htmlDocument += ("</body></html>");
	}
	void highlightLines(QPlainTextEdit * textEdit, const std::vector<GdbBreakpointData> & breakpoints, uint64_t programCounterValue, bool centerViewOnCurrentPC = false)
	{
		/* Highlight lines with breakpoints, if any. */
		QTextCursor c(textEdit->textCursor());
		QList<QTextEdit::ExtraSelection> extraSelections;

		std::unordered_set<int /* line number */> enabledLines;
		std::unordered_set<int /* line number */> disabledLines;

		std::function<void(const GdbBreakpointData & /* breakpoint */)> processBreakpoint = [&] (const GdbBreakpointData & breakpoint) -> void
		{
			const auto s = sourceLines.find(breakpoint.sourceCodeLocation.fullFileName);
			if (s != sourceLines.cend())
			{
				const auto lines = s->second.find(breakpoint.sourceCodeLocation.lineNumber);
				if (lines != s->second.cend())
					for (const auto & l : lines->second)
						(breakpoint.enabled ? enabledLines : disabledLines).insert(l);
			}
			if (breakpoint.multipleLocationBreakpoints.empty())
			{
				const auto l = disassemblyLines.find(breakpoint.address);
				if (l != disassemblyLines.cend())
					(breakpoint.enabled ? enabledLines : disabledLines).insert(l->second);
			}
		};
		for (const auto & b : breakpoints)
		{
			processBreakpoint(b);
			for (const auto & t : b.multipleLocationBreakpoints)
				processBreakpoint(t);
		}
		QTextEdit::ExtraSelection selection;

		std::function<void(int /* lineNumber*/)> rewindCursor = [&] (int line) -> void
		{
			c.movePosition(QTextCursor::Start);
			c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, line);
			c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
			selection.cursor = c;
		};
		for (const auto & line : enabledLines)
		{
			rewindCursor(line);
			selection.format = enabledBreakpointFormat;
			extraSelections << selection;
		}
		for (const auto & line : disabledLines)
		{
			rewindCursor(line);
			selection.format = disabledBreakpointFormat;
			extraSelections << selection;
		}

		const auto t = disassemblyLines.find(programCounterValue);
		if (t != disassemblyLines.cend())
		{
			rewindCursor(t->second);
			selection.format = currentPCLineFormat;
			extraSelections << selection;

			if (centerViewOnCurrentPC)
				c.clearSelection(), textEdit->setTextCursor(c), textEdit->centerCursor();
		}
		textEdit->setExtraSelections(extraSelections);
	}
};
