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

/* For details about the SVD file format and interpretation, look at the ARM CMSIS-SVD documentation. */

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
	/* Note, that 'dimElementGroup' elements are possible for peripheral, cluster, register, and field elements,
	 * but have currently been observed only for registers and fields. */
	struct SvdDimElementGroup
	{
		/* The 'dim' element is of type 'scaledNonNegativeInteger' */
		int		dim = -1;
		/* The 'dimIncrement' element is of type 'scaledNonNegativeInteger' */
		int		dimIncrement = -1;
		/* The 'dimIndex' element is of type 'dimIndexType', and is used for substitution, to define a list (sequence)
		 * of elements.
		 * The type is formally defined in the ARM CMSIS-SVD Schema File as:
		 *
			<xs:simpleType name="dimIndexType">
				<xs:restriction base="xs:string">
					<xs:pattern value="[0-9]+\-[0-9]+|[A-Z]-[A-Z]|[_0-9a-zA-Z]+(,\s*[_0-9a-zA-Z]+)+"/>
				</xs:restriction>
			</xs:simpleType>
		 *
		 * These are some useful examples from the documentation:
			Example: The examples creates definitions for registers.
			...
			<register>
			    <dim>6</dim>
			    <dimIncrement>4</dimIncrement>
			    <dimIndex>A,B,C,D,E,Z</dimIndex>
			    <name>GPIO_%s_CTRL</name>
			...
			</register>

			The code above generates the list: => GPIO_A_CTRL, GPIO_B_CTRL, GPIO_C_CTRL, GPIO_D_CTRL, GPIO_E_CTRL, GPIO_Z_CTRL
			...
			<register>
			    <dim>4</dim>
			    <dimIncrement>4</dimIncrement>
			    <dimIndex>3-6</dimIndex>
			    <name>IRQ%s</name>
			...
			</register>

			The example above generates the list: => IRQ3, IRQ4, IRQ5, IRQ6
			...
			<register>
			    <dim>4</dim>
			    <dimIncrement>4</dimIncrement>
			    <name>MyArr[%s]</name>
			...
			</register>

			The example above generates the array: => MyArr[4]
		 */
		QString		dimIndex;
		/* The 'dimName' element is of type 'identifierType' */
		/*! \todo	This is currently not used, because there are no known samples using it. Fix this if this changes in the future. */
		QString		dimName;
		/* The 'dimArrayIndex' element is of type 'dimArrayIndexType':
		  <xs:complexType name="dimArrayIndexType">
		    <xs:sequence>
		      <xs:element name="headerEnumName" type="identifierType" minOccurs="0"/>
		      <xs:element name="enumeratedValue" type="enumeratedValueType" minOccurs="1" maxOccurs="unbounded"/>
		    </xs:sequence>
		  </xs:complexType>
		 */
		/*! \todo	This is currently not used, because there are no known samples using it. Fix this if this changes in the future. */
		QString		dimArrayIndex;
	};
	struct SvdRegisterFieldNode
	{
		QString		name;
		QString		description;
		QString		access;
		unsigned	bitOffset = -1;
		unsigned	bitWidth = -1;
		/*! \todo	This is currently not used, because there are no known samples using it. Fix this if this changes in the future. */
		struct SvdDimElementGroup dim;
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
		std::list<SvdRegisterFieldNode>	fields;
		std::list<SvdRegisterOrClusterNode>	children;
		struct SvdDimElementGroup dim;
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
		std::list<SvdRegisterOrClusterNode>	registersAndClusters;
		/*! \todo	This is currently not used, because there are no known samples using it. Fix this if this changes in the future. */
		struct SvdDimElementGroup dim;
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
		std::list<SvdPeripheralNode>	peripherals;
		unsigned	addressUnitBits	= -1;
		unsigned	width		= -1;
		unsigned	size		= -1;
		unsigned	resetValue	= -1;
		unsigned	resetMask	= 0;
	};

private:

	QXmlStreamReader xml;
	const SvdPeripheralNode * findPeripheral(const QString & peripheralName);

	/* Returns true, if a 'dim' element was successfully parsed, false otherwise. */
	bool parseDimElement(SvdDimElementGroup & dim);

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
