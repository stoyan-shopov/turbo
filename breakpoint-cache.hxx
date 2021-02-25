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

#include <vector>
#include <QString>
#include <QMap>
#include <QSet>

#include "source-code-location.hxx"

struct GdbBreakpointData
{
	QString		gdbReportedNumberString = "???";
	QString		type = "<<< unknown >>>", disposition = "<<< unknown >>>";
	bool		enabled = false;
	uint64_t	address = -1;
	QString		subprogramName = "<<< unknown >>>";
	QString		fileName = "<<< unknown >>>";
	SourceCodeLocation	sourceCodeLocation;
	QString		locationSpecifierString = "<<< unknown >>>";
	std::vector<GdbBreakpointData>	multipleLocationBreakpoints;
	static void breakpointsForSourceCodeLineNumber(
			const SourceCodeLocation & sourceCodeLocation,
			const std::vector<GdbBreakpointData> & breakpoints,
			std::vector<const GdbBreakpointData *> & foundBreakpoints)
	{
		for (const auto & b : breakpoints)
		{
			if (b.sourceCodeLocation == sourceCodeLocation)
				foundBreakpoints.push_back(& b);
			/* Special case for breakpoints with multiple locations. Otherwise breakpoint deletion gets broken,
				 * because sub-breakpoints of a multiple location breakpoint cannot be deleted - only enabled or disabled. */
			if (b.multipleLocationBreakpoints.size() && b.multipleLocationBreakpoints.at(0).sourceCodeLocation == sourceCodeLocation)
				foundBreakpoints.push_back(& b);
			for (const auto & t : b.multipleLocationBreakpoints)
				if (t.sourceCodeLocation == sourceCodeLocation)
					foundBreakpoints.push_back(& t);
		}
	}
};

class BreakpointCache
{
private:
	QMap<QString /* filename */, QSet<int /* line numbers */>> enabledSourceCodeBreakpoints;
	QMap<QString /* filename */, QSet<int /* line numbers */>> disabledSourceCodeBreakpoints;
	QSet<uint64_t /* address */> enabledBreakpointAddresses;
	QSet<uint64_t /* address */> disabledBreakpointAddresses;
	QSet<int /* line number */> emptySet;
public:
	void rebuildCache(const std::vector<struct GdbBreakpointData> & breakpoints)
	{
		enabledSourceCodeBreakpoints.clear();
		disabledSourceCodeBreakpoints.clear();
		enabledBreakpointAddresses.clear();
		disabledBreakpointAddresses.clear();
		for (const auto & b : breakpoints)
		{
			if (!b.multipleLocationBreakpoints.size())
			{
				(b.enabled ? enabledSourceCodeBreakpoints : disabledSourceCodeBreakpoints)
					    .operator [](b.sourceCodeLocation.fullFileName).insert(b.sourceCodeLocation.lineNumber);
				(b.enabled ? enabledBreakpointAddresses : disabledBreakpointAddresses).insert(b.address);
			}
			else
				for (const auto & t : b.multipleLocationBreakpoints)
				{
					(t.enabled ? enabledSourceCodeBreakpoints : disabledSourceCodeBreakpoints)
							.operator [](t.sourceCodeLocation.fullFileName).insert(t.sourceCodeLocation.lineNumber);
					(t.enabled ? enabledBreakpointAddresses : disabledBreakpointAddresses).insert(t.address);
				}
		}
	}
	bool hasEnabledBreakpointAtAddress(uint64_t address) { return enabledBreakpointAddresses.contains(address); }
	bool hasDisabledBreakpointAtAddress(uint64_t address) { return disabledBreakpointAddresses.contains(address); }
	bool hasEnabledBreakpointAtLineNumber(const QString & fullFileName, int lineNumber)
	{ return enabledSourceCodeBreakpoints.contains(fullFileName) && enabledSourceCodeBreakpoints.operator [](fullFileName).contains(lineNumber); }
	bool hasDisabledBreakpointAtLineNumber(const QString & fullFileName, int lineNumber)
	{ return disabledSourceCodeBreakpoints.contains(fullFileName) && disabledSourceCodeBreakpoints.operator [](fullFileName).contains(lineNumber); }
	const QSet<int /* line numbers */> & enabledBreakpointLinesForFile(const QString & fullFileName)
	{ return enabledSourceCodeBreakpoints.contains(fullFileName) ? enabledSourceCodeBreakpoints.operator [](fullFileName) : emptySet; }
	const QSet<int /* line numbers */> & disabledBreakpointLinesForFile(const QString & fullFileName)
	{ return disabledSourceCodeBreakpoints.contains(fullFileName) ? disabledSourceCodeBreakpoints.operator [](fullFileName) : emptySet; }
};
