/*
 * Copyright (C) 2021, 2020 Stoyan Shopov <stoyan.shopov@gmail.com>
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
#include <unordered_set>

struct SourceFileData {
	/* Enumeration constants used as a 'role' parameter in treeview item widgets, contained in
	 * the different treeview widgets (subprograms, data objects, breakpoints, bookmarks, etc.). */
	enum
	{
		FILE_NAME = Qt::UserRole,
		LINE_NUMBER,

		/* Item type values in the object locator view. Used for creating
		 * custom context menus depending on the item type. */
		/*! \todo	It may be simpler to add here all 'source location'-related objects,
		 * i.e., bookmarks, breakpoints, stack frames, etc., and handle their context menus depending on
		 * the item types. */
		/* The values for this role are from the SymbolData::SymbolKind enumeration. */
		ITEM_KIND,
		/* The data contents are a 'void' pointer that can be cast to a 'GdbBreakpointData' data structure. */
		BREAKPOINT_DATA_POINTER,
		/* If set, and set to true, then the context menu for this item is disabled. */
		DISABLE_CONTEXT_MENU,
		/* If set, and set to true, then navigating to source code for this item is disabled. */
		DISABLE_SOURCE_CODE_NAVIGATION,
		/* If available, specifies the gdb string to use for setting a breakpoint. */
		BREAKPOINT_TARGET_COORDINATES,
		/* If available, specifies the gdb string to use for requesting a disassembly. */
		DISASSEMBLY_TARGET_COORDINATES,
	};
	QString fileName, gdbReportedFileName, fullFileName;
	bool isSourceLinesFetched = false;
	/* This set is used to know for which source code line numbers, there is some machine code generated.
	 * This is useful, for example, for showing which source code line numbers are potential candidates
	 * for setting breakpoints. */
	std::unordered_set<int /* Source code line number. */> machineCodeLineNumbers;
	bool operator ==(const SourceFileData & other) const
	{ return other.fileName == fileName && other.gdbReportedFileName == gdbReportedFileName && other.fullFileName == fullFileName; }
	struct SymbolData
	{
		/* This enumeration specifies a symbol 'kind', useful in some cases when handling symbols. */
		enum SymbolKind
		{
			/* For data type symbols, only the 'name' field is appropriate. For data object and subprogram symbols,
			 * the description is normally a string specifying the declaration of the symbol.
			 *
			 * 'Source file name' is not really a symbol, it is here to make some parts of the user-interface code
			 *  more uniform. */
			INVALID = 0,
			DATA_OBJECT,
			DATA_TYPE,
			SUBPROGRAM,
			SOURCE_FILE_NAME,
		};
		int line = -1;
		QString	name, type, description;
		bool operator ==(const SymbolData & other) const
		{
			return name == other.name && type == other.type && description == other.description && line == other.line;
		}
	};
	struct SymbolHash
	{
		size_t operator ()(const SourceFileData::SymbolData & t) const
		{
			return std::hash<std::string>()(QString("%1:%2:%3:%4").arg(t.name).arg(t.type).arg(t.description).arg(t.line).toStdString());
		}
	};

	std::unordered_set<struct SymbolData, SymbolHash> subprograms;
	std::unordered_set<struct SymbolData, SymbolHash> variables;
	std::unordered_set<struct SymbolData, SymbolHash> dataTypes;
};
