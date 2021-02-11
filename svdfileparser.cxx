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

#include <QRegularExpression>
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
			parseDevice(device);
		else
		{
			qDebug() << "unhandled top level element: " << xml.name();
			xml.skipCurrentElement();
		}
	/* Process and expand any 'dim' elements. */
	/*! \todo	This is incomplete, as no examples for testing all cases were available when writing this code. */
	std::function<void(std::list<SvdRegisterOrClusterNode> & rc)> expand = [&](std::list<SvdRegisterOrClusterNode> & rc) -> void
	{
		std::list<SvdRegisterOrClusterNode> l{std::move(rc)};
		rc.clear();
		for (const auto & t : l)
			if (t.name.contains("%s"))
			{
				if (t.dim.dim == -1 || t.dim.dimIncrement == -1)
				{
					qDebug() << "Could not expand array/list element" << t.name << ", skipping";
					rc.push_back(t);
					continue;
				}
				if (t.dim.dimName.length() || t.dim.dimArrayIndex.length())
				{
					qDebug() << "Could not expand array/list element, case not supported, '" << t.name << "', please report this case!";
					rc.push_back(t);
					continue;
				}
				QStringList indices;
				do
				{
					/* If this is an array, ignore the 'dimIndex' field, even though
					 * many SVD files do make use of it. The CMSIS-SVD documentation explicitly
					 * states that 'dimIndex' should not be used in this case. */
					if (t.name.endsWith("[%s]") || t.dim.dimIndex.isEmpty())
					{
						for (int i = 0; i < t.dim.dim; i ++)
							indices.push_back(QString("%1").arg(i));
						break;
					}
					/* Try to parse a comma delimited list of indices. */
					QStringList l = t.dim.dimIndex.split(",");
					if (l.length() == t.dim.dim)
					{
						indices = l;
						break;
					}
					/* Try to parse a numeric range of the form '[0-9]+\-[0-9]+'. */
					QRegularExpression rx("(\\d+)-(\\d+)");
					QRegularExpressionMatch match = rx.match(t.dim.dimIndex);
					if (match.hasMatch())
					{
						unsigned l = match.captured(1).toUInt(), h = match.captured(2).toUInt();
						if (l > h || h - l + 1 != t.dim.dim)
						{
							qDebug() << "Bad numeric range for svd 'dim' element, skipping.";
							goto there;
						}
						do indices.push_back(QString("%1").arg(l)); while (++l <= h);
						break;
					}
					/* Try to parse an alphabetical range of the form '[A-Z]-[A-Z]'. */
					/*! \todo	This case has not been tested, as there are no samples using it. */
					rx.setPattern("([A-Z])-([A-Z])");
					match = rx.match(t.dim.dimIndex);
					if (match.hasMatch())
					{
						char l = match.captured(1).at(0).toLatin1(), h = match.captured(2).at(0).toLatin1();
						if (l > h || h - l + 1 != t.dim.dim)
						{
							qDebug() << "Bad numeric range for svd 'dim' element, skipping.";
							goto there;
						}
						do indices.push_back(QString("%1").arg(l)); while (++l <= h);
						break;
					}
there:
					qDebug() << "Failed to expand array/list element '" << t.name << "'. Please, report this case!";
					rc.push_back(t);
				}
				while (0);

				for (int i = 0; i < indices.size(); i ++)
				{
					rc.push_back(t);
					rc.back().name.replace("%s", indices.at(i));
					rc.back().addressOffset += i * t.dim.dimIncrement;
				}
			}
			else
				rc.push_back(t);
		for (auto & t : rc)
			expand(t.children);
	};
	for (auto & p : device.peripherals)
		expand(p.registersAndClusters);
	/* Process and resolve any 'derivedFrom' elements. */
	/*! \todo	This is incomplete. For more complicated samples, see, e.g., file ATSAMD21E15L.svd. */
	for (auto & p : device.peripherals)
	{
		const SvdPeripheralNode * origin;
		if (!p.derivedFrom.isEmpty() && (origin = findPeripheral(p.derivedFrom)) != 0)
		{
			/* Merge the peripheral, from which this peripheral has been derived from,
			 * into this peripheral. */
			if (p.addressBlocks.empty())
				p.addressBlocks = origin->addressBlocks;
			if (p.description.isEmpty())
				p.description = origin->description;
			if (p.groupName.isEmpty())
				p.groupName = origin->groupName;
			if (p.name.isEmpty())
				p.name = origin->name;
			if (p.registersAndClusters.empty())
				p.registersAndClusters = origin->registersAndClusters;

			qDebug() << "Successfully resolved peripheral " << p.name;
		}

		qDebug() << "descending in peripheral:" << p.name;
		std::function<void(SvdRegisterOrClusterNode & registerOrCluster, const std::list<SvdRegisterOrClusterNode> & siblings)>
			resolveRegistersAndClusters = [&](SvdRegisterOrClusterNode & registerOrCluster, const std::list<SvdRegisterOrClusterNode> & siblings) -> void
		{
			QString originName;
			if (!(originName = registerOrCluster.derivedFrom).isEmpty())
			{
				if (originName.contains(QChar('.')))
				{
					/* Most probably, this is a qualified 'derivedFrom' target. These are
					 * not handled at this time, because I have not seen such samples
					 * in the cmsis-svd database here:
					 * https://github.com/posborne/cmsis-svd.git */
					qDebug() << "WARNING: qualified svd elements not yet handled, please report this case, so that it may be handled properly!";
				}
				else
				{
					/* Try to resolve the derived element amonsg its sibling elements. */
					SvdRegisterOrClusterNode const * origin = 0;
					for (const auto & s : siblings)
						if (s.name == originName)
						{
							origin = & s;
							break;
						}
					if (!origin)
						qDebug() << "WARNING: could not resolve svd element, please report this case, so that it may be handled properly!";
					else
					{
						/* Merge the origin into this node. */
						if (registerOrCluster.name.isEmpty())
							registerOrCluster.name = origin->name;
						if (registerOrCluster.displayName.isEmpty())
							registerOrCluster.displayName = origin->displayName;
						if (registerOrCluster.description.isEmpty())
							registerOrCluster.description = origin->description;
						if (registerOrCluster.alternateRegister.isEmpty())
							registerOrCluster.alternateRegister = origin->alternateRegister;
						if (registerOrCluster.access.isEmpty())
							registerOrCluster.access = origin->access;
						if (registerOrCluster.addressOffset == -1)
							registerOrCluster.addressOffset = origin->addressOffset;
						if (registerOrCluster.size == -1)
							registerOrCluster.size = origin->size;
						if (registerOrCluster.resetValue == -1)
							registerOrCluster.resetValue = origin->resetValue;
						if (!registerOrCluster.fields.size())
							registerOrCluster.fields = origin->fields;
						if (!registerOrCluster.children.size())
							registerOrCluster.children = origin->children;
					}
				}
			}
			for (auto & rc : registerOrCluster.children)
				resolveRegistersAndClusters(rc, registerOrCluster.children);
		};
		for (auto & t : p.registersAndClusters)
			resolveRegistersAndClusters(t, p.registersAndClusters);
	}
}

const SvdFileParser::SvdPeripheralNode *SvdFileParser::findPeripheral(const QString & peripheralName)
{
	for (const auto & p : device.peripherals)
		if (p.name == peripheralName)
			return & p;
	return 0;
}

bool SvdFileParser::parseDimElement(SvdFileParser::SvdDimElementGroup &dim)
{
	Q_ASSERT(xml.isStartElement() && xml.name().startsWith("dim"));
	bool result = false;

	if (xml.name() == "dim")
		dim.dim = xml.readElementText().toUInt(), result = true;
	else if (xml.name() == "dimIncrement")
		dim.dimIncrement = xml.readElementText().toUInt(0, 0), result = true;
	else if (xml.name() == "dimIndex")
		dim.dimIndex = xml.readElementText(), result = true;
	else if (xml.name() == "dimName" || xml.name() == "dimArrayIndex")
		qDebug() << xml.name() << "element not handled! Please, report this, so that it is properly handled!";
	return result;
}

void SvdFileParser::parseRegisterField(SvdRegisterFieldNode & field)
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "field");
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
}

void SvdFileParser::parseRegisterOrCluster(SvdRegisterOrClusterNode & registerOrCluster)
{
	Q_ASSERT(xml.isStartElement() && (xml.name() == "register" || xml.name() == "cluster"));
	SvdRegisterOrClusterNode & r = registerOrCluster;
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
		{
			r.children.push_back(SvdRegisterOrClusterNode());
			parseRegisterOrCluster(r.children.back());
		}
		else if (xml.name() == "fields")
		{
			while (xml.readNextStartElement())
			{
				r.fields.push_back(SvdRegisterFieldNode());
				parseRegisterField(r.fields.back());
			}
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
		else if (xml.name().startsWith("dim"))
		{
			if (!parseDimElement(r.dim))
			{
				qDebug() << "Failed to parse 'dim' element" << xml.name();
				xml.skipCurrentElement();
			}
		}
		else
		{
			qDebug() << "unhandled register/cluster element: " << xml.name();
			xml.skipCurrentElement();
		}
	}
}

void SvdFileParser::parseAddressBlock(SvdAddressBlockNode & addressBlock)
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "addressBlock");
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
}

void SvdFileParser::parseInterrupt(SvdInterruptNode & interrupt)
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "interrupt");
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
}

void SvdFileParser::parsePeripheral(SvdPeripheralNode & peripheral)
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "peripheral");
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
		{
			peripheral.addressBlocks.push_back(SvdAddressBlockNode());
			parseAddressBlock(peripheral.addressBlocks.back());
		}
		else if (xml.name() == "interrupt")
		{
			peripheral.interrupts.push_back(SvdInterruptNode());
			parseInterrupt(peripheral.interrupts.back());
		}
		else if (xml.name() == "registers")
		{
			while (xml.readNextStartElement())
			{
				peripheral.registersAndClusters.push_back(SvdRegisterOrClusterNode());
				parseRegisterOrCluster(peripheral.registersAndClusters.back());
			}
		}
		else
		{
			qDebug() << "unhandled peripheral element: " << xml.name();
			xml.skipCurrentElement();
		}
	qDebug() << "processed peripheral " << peripheral.name;
}

void SvdFileParser::parseCpu(SvdCpuNode & cpu)
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "cpu");
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
}

void SvdFileParser::parseDevice(SvdDeviceNode & device)
{
	Q_ASSERT(xml.isStartElement() && xml.name() == "device");
	bool ok;

	while (xml.readNextStartElement())
		if (xml.name() == "name")
			device.name = xml.readElementText();
		else if (xml.name() == "version")
			device.version = xml.readElementText();
		else if (xml.name() == "description")
			device.description = xml.readElementText();
		else if (xml.name() == "cpu")
			parseCpu(device.cpu);
		else if (xml.name() == "peripherals")
		{
			while (xml.readNextStartElement())
			{
				device.peripherals.push_back(SvdPeripheralNode());
				parsePeripheral(device.peripherals.back());
			}
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
}
