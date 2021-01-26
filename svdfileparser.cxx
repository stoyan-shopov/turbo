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

/*! \todo	This is getting unnecessarily complicated. Rework the parser. */

#include "svdfileparser.hxx"

void SvdFileParser::parse(const QString & svdFileName)
{
	/*! \todo	This is incomplete. For more complicated samples, see, e.g., file ATSAMD21E15L.svd. */
	QFile xmlFile(svdFileName);
	device = SvdDeviceNode();
	if (xmlFile.open(QFile::ReadOnly))
		xml.setDevice(& xmlFile);
	else
	{
		device.name = "Failed to load and parse device svd file";
		return;
	}
	while (xml.readNextStartElement())
		if (xml.name() == "device")
			device = parseDevice();
		else
		{
			qDebug() << "unhandled top level element: " << xml.name();
			xml.skipCurrentElement();
		}
	/* Process and resolve any 'derivedFrom' fields. */
	/*! \todo	This is incomplete. For more complicated samples, see, e.g., file ATSAMD21E15L.svd. */
	for (auto & p : device.peripherals)
	{
		const SvdPeripheralNode * target;
		if (!p.derivedFrom.isEmpty() && (target = findPeripheral(p.derivedFrom)) != 0)
		{
			/* Merge the peripheral, from which this peripheral has been derived from,
			 * into this peripheral. */
			if (p.addressBlocks.empty())
				p.addressBlocks = target->addressBlocks;
			if (p.description.isEmpty())
				p.description = target->description;
			if (p.groupName.isEmpty())
				p.groupName = target->groupName;
			if (p.name.isEmpty())
				p.name = target->name;
			if (p.registersAndClusters.empty())
				p.registersAndClusters = target->registersAndClusters;

			qDebug() << "Successfully resolved peripheral " << p.name;
		}
	}
}

const SvdFileParser::SvdPeripheralNode *SvdFileParser::findPeripheral(const QString & peripheralName)
{
	for (const auto & p : device.peripherals)
		if (p.name == peripheralName)
			return & p;
	return 0;
}

SvdFileParser::SvdRegisterFieldNode SvdFileParser::parseRegisterField()
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "field");
	SvdRegisterFieldNode field;
	bool ok;

	while (xml.readNextStartElement())
		if (xml.name() == "name")
			field.name = xml.readElementText();
		else if (xml.name() == "description")
			field.description = xml.readElementText();
		else if (xml.name() == "access")
			field.access = xml.readElementText();
		else if (xml.name() == "bitOffset")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				field.bitOffset = t;
		}
		else if (xml.name() == "bitWidth")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				field.bitWidth = t;
		}
		else
		{
			qDebug() << "unhandled register field element: " << xml.name();
			xml.skipCurrentElement();
		}

	return field;
}

SvdFileParser::SvdRegisterOrClusterNode SvdFileParser::parseRegisterOrCluster()
{
	Q_ASSERT(xml.isStartElement() && (xml.name() == "register" || xml.name() == "cluster"));
	SvdRegisterOrClusterNode r;
	if (xml.name() == "cluster")
		r.isRegisterNode = false;
	bool ok;

	r.derivedFrom = xml.attributes().value("derivedFrom").toString();

	while (xml.readNextStartElement())
	{
		if (xml.name() == "name")
			r.name = xml.readElementText();
		else if (xml.name() == "displayName")
			r.displayName = xml.readElementText();
		else if (xml.name() == "description")
			r.description = xml.readElementText();
		else if (xml.name() == "alternateRegister")
			r.alternateRegister = xml.readElementText();
		else if (xml.name() == "access")
			r.access = xml.readElementText();
		else if (xml.name() == "register")
			r.children.push_back(parseRegisterOrCluster());
		else if (xml.name() == "fields")
		{
			while (xml.readNextStartElement())
				r.fields.push_back(parseRegisterField());
		}
		else if (xml.name() == "addressOffset")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				r.addressOffset = t;
		}
		else if (xml.name() == "size")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				r.size = t;
		}
		else if (xml.name() == "resetValue")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				r.resetValue = t;
		}
		else
		{
			qDebug() << "unhandled register/cluster element: " << xml.name();
			xml.skipCurrentElement();
		}
	}

	return r;
}

SvdFileParser::SvdAddressBlockNode SvdFileParser::parseAddressBlock()
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "addressBlock");
	SvdAddressBlockNode addressBlock;
	bool ok;

	while (xml.readNextStartElement())
		if (xml.name() == "usage")
			addressBlock.usage = xml.readElementText();
		else if (xml.name() == "offset")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				addressBlock.offset = t;
		}
		else if (xml.name() == "size")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				addressBlock.size = t;
		}
		else
		{
			qDebug() << "unhandled address block element: " << xml.name();
			xml.skipCurrentElement();
		}
	return addressBlock;
}

SvdFileParser::SvdInterruptNode SvdFileParser::parseInterrupt()
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "interrupt");
	SvdInterruptNode interrupt;
	bool ok;

	while (xml.readNextStartElement())
		if (xml.name() == "name")
			interrupt.name = xml.readElementText();
		else if (xml.name() == "description")
			interrupt.description = xml.readElementText();
		else if (xml.name() == "value")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				interrupt.value = t;
		}
		else
		{
			qDebug() << "unhandled interrupt element: " << xml.name();
			xml.skipCurrentElement();
		}
	return interrupt;
}

SvdFileParser::SvdPeripheralNode SvdFileParser::parsePeripheral()
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "peripheral");
	SvdPeripheralNode peripheral;
	bool ok;

	peripheral.derivedFrom = xml.attributes().value("derivedFrom").toString();

	while (xml.readNextStartElement())
		if (xml.name() == "name")
			peripheral.name = xml.readElementText();
		else if (xml.name() == "description")
			peripheral.description = xml.readElementText();
		else if (xml.name() == "groupName")
			peripheral.groupName = xml.readElementText();
		else if (xml.name() == "baseAddress")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				peripheral.baseAddress = t;
		}
		else if (xml.name() == "addressBlock")
			peripheral.addressBlocks.push_back(parseAddressBlock());
		else if (xml.name() == "interrupt")
			peripheral.interrupts.push_back(parseInterrupt());
		else if (xml.name() == "registers")
		{
			while (xml.readNextStartElement())
				peripheral.registersAndClusters.push_back(parseRegisterOrCluster());
		}
		else
		{
			qDebug() << "unhandled peripheral element: " << xml.name();
			xml.skipCurrentElement();
		}
	qDebug() << "processed peripheral " << peripheral.name;
	return peripheral;
}

SvdFileParser::SvdCpuNode SvdFileParser::parseCpu()
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "cpu");
	SvdCpuNode cpu;
	bool ok;

	while (xml.readNextStartElement())
		if (xml.name() == "name")
			cpu.name = xml.readElementText();
		else if (xml.name() == "revision")
			cpu.revision = xml.readElementText();
		else if (xml.name() == "endian")
			cpu.endian = xml.readElementText();
		else if (xml.name() == "mpuPresent")
			cpu.mpuPresent = (xml.readElementText() == "true") ? true : false;
		else if (xml.name() == "fpuPresent")
			cpu.fpuPresent = (xml.readElementText() == "true") ? true : false;
		else if (xml.name() == "nvicPrioBits")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				cpu.nvicPrioBits = t;
		}
		else
		{
			qDebug() << "unhandled cpu element: " << xml.name();
			xml.skipCurrentElement();
		}
	return cpu;
}

SvdFileParser::SvdDeviceNode SvdFileParser::parseDevice()
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "device");
	SvdDeviceNode device;
	bool ok;

	while (xml.readNextStartElement())
		if (xml.name() == "name")
			device.name = xml.readElementText();
		else if (xml.name() == "version")
			device.version = xml.readElementText();
		else if (xml.name() == "description")
			device.description = xml.readElementText();
		else if (xml.name() == "cpu")
			device.cpu = parseCpu();
		else if (xml.name() == "peripherals")
		{
			while (xml.readNextStartElement())
				device.peripherals.push_back(parsePeripheral());
		}
		else if (xml.name() == "addressUnitBits")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				device.addressUnitBits = t;
		}
		else if (xml.name() == "width")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				device.width = t;
		}
		else if (xml.name() == "size")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				device.size = t;
		}
		else if (xml.name() == "resetValue")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				device.resetValue = t;
		}
		else if (xml.name() == "resetMask")
		{
			unsigned t = xml.readElementText().toULong(& ok, 0);
			if (ok)
				device.resetMask = t;
		}
		else
		{
			qDebug() << "unhandled device element: " << xml.name();
			xml.skipCurrentElement();
		}
	return device;
}
