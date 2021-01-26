/*
 * Copyright (C) 2020 Stoyan Shopov <stoyan.shopov@gmail.com>
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

#ifndef SVDFILEPARSER_HXX
#define SVDFILEPARSER_HXX

#include <vector>
#include <map>

#include <QDebug>
#include <QFile>
#include <QXmlStreamReader>

class SvdFileParser
{
public:
	struct SvdRegisterFieldNode
	{
		QString		name;
		QString		description;
		QString		access;
		unsigned	bitOffset = -1;
		unsigned	bitWidth = -1;
	};
	struct SvdRegisterOrClusterNode
	{
		bool		isRegisterNode = true;
		QString		derivedFrom;
		QString		name;
		QString		displayName;
		QString		description;
		QString		alternateRegister;
		QString		access;
		unsigned	addressOffset = -1;
		unsigned	size = -1;
		unsigned	resetValue = -1;
		std::vector<SvdRegisterFieldNode>	fields;
		std::vector<SvdRegisterOrClusterNode>	children;
	};
	struct SvdAddressBlockNode
	{
		QString		usage;
		unsigned	offset = -1;
		unsigned	size = -1;
	};
	struct SvdInterruptNode
	{
		QString		name;
		QString		description;
		unsigned	value = -1;
	};
	struct SvdPeripheralNode
	{
		QString		name;
		QString		derivedFrom;
		QString		description;
		QString		groupName;
		unsigned	baseAddress = -1;
		std::vector<SvdInterruptNode>		interrupts;
		std::vector<SvdAddressBlockNode>	addressBlocks;
		std::vector<SvdRegisterOrClusterNode>	registersAndClusters;
	};
	struct SvdCpuNode
	{
		QString		name = "Unknown";
		QString		revision;
		QString		endian;
		bool		mpuPresent;
		bool		fpuPresent;
		unsigned	nvicPrioBits	= -1;
	};
	struct SvdDeviceNode
	{
		QString		name = "Unknown device";
		QString		version;
		QString		description = "Unknown";
		SvdCpuNode	cpu;
		std::vector<SvdPeripheralNode>	peripherals;
		unsigned	addressUnitBits	= -1;
		unsigned	width		= -1;
		unsigned	size		= -1;
		unsigned	resetValue	= -1;
		unsigned	resetMask	= 0;
	};

private:

	QXmlStreamReader xml;
	const SvdPeripheralNode * findPeripheral(const QString & peripheralName);

	void parseRegisterField(SvdRegisterFieldNode & field);
	void parseRegisterOrCluster(SvdRegisterOrClusterNode & registerOrCluster);
	void parseAddressBlock(SvdAddressBlockNode & addressBlock);
	void parseInterrupt(SvdInterruptNode & interrupt);
	void parsePeripheral(SvdPeripheralNode & peripheral);
	void parseCpu(SvdCpuNode & cpu);
	void parseDevice(SvdDeviceNode & device);
public:
	SvdDeviceNode	device;
	SvdFileParser(void) {}
	void parse(const QString & svdFileName);
};

#endif // SVDFILEPARSER_HXX
