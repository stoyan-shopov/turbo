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

/*
 * This is some rather ugly code for detecting connected blackmagic probes.
 * It is platform dependent, and best kept separately.
 */


#include "bmpdetect.hxx"
#include <QRegularExpression>
#include <QDir>

#ifdef Q_OS_WINDOWS
#define _WIN32_WINNT	0x0600

/* This source has been used as an example:
 * https://stackoverflow.com/questions/3438366/setupdigetdeviceproperty-usage-example
 *
 * Also, function `find_bmp_by_serial()` from the blackmagic sources has been used
 * as a basis for function `portNameForSerialNumber()`
 */

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>   // for MAX_DEVICE_ID_LEN, CM_Get_Parent and CM_Get_Device_ID
#include <tchar.h>

/* include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpropdef.h */
#ifdef DEFINE_DEVPROPKEY
#undef DEFINE_DEVPROPKEY
#endif
#define DEFINE_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) const DEVPROPKEY name = { { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }, pid }

/* include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpkey.h */
DEFINE_DEVPROPKEY(DEVPKEY_Device_BusReportedDeviceDesc,  0x540b947e, 0x8b40, 0x45bc, 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2, 4);     // DEVPROP_TYPE_STRING

static QString portNameForSerialNumber(const QString & serialNumber)
{
	QString registryPath = QString("SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_%1&PID_%2\\%3")
		.arg((QString("%1").arg(BMP_USB_VID, 4, 16, QChar('0')).toUpper()))
		.arg((QString("%1").arg(BMP_USB_PID, 4, 16, QChar('0')).toUpper()))
		.arg(serialNumber);
	HKEY h;
	wchar_t rp[258];
	rp[registryPath.toWCharArray(rp)] = 0;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, rp, 0, KEY_READ, &h) != ERROR_SUCCESS)
		return QString();
	WCHAR parentPrefix[128];
	DWORD size = sizeof parentPrefix;
	LSTATUS result;
	result = RegQueryValueEx(h, L"ParentIdPrefix", 0, 0, (BYTE *) parentPrefix, &size);
	RegCloseKey(h);
	if (result != ERROR_SUCCESS)
		return QString();

	registryPath = QString("SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_%1&PID_%2&MI_00\\%3&0000\\Device Parameters")
		.arg((QString("%1").arg(BMP_USB_VID, 4, 16, QChar('0')).toUpper()))
		.arg((QString("%1").arg(BMP_USB_PID, 4, 16, QChar('0')).toUpper()))
		.arg(QString::fromWCharArray(parentPrefix, size / sizeof(wchar_t) - 1));

	rp[registryPath.toWCharArray(rp)] = 0;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, rp, 0, KEY_READ, &h) != ERROR_SUCCESS)
		return QString();

	WCHAR portName[128];
	size = sizeof portName;
	result = RegQueryValueEx(h, L"PortName", 0, 0, (BYTE *) portName, &size);
	RegCloseKey(h);
	if (result != ERROR_SUCCESS)
		return QString();
	return QString::fromWCharArray(portName, size / sizeof(wchar_t) - 1);
}

void findConnectedProbes(std::vector<BmpProbeData> &probeData)
{
	unsigned i;
	DEVPROPTYPE ulPropertyType;
	CONFIGRET status;
	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA DeviceInfoData;
	TCHAR deviceInstanceId[MAX_DEVICE_ID_LEN];
	WCHAR busReportedDeviceDesc[4096];

	QRegularExpression rx = QRegularExpression(
				QString("USB\\\\VID_%1&PID_%2\\\\(.*)")
				.arg((QString("%1").arg(BMP_USB_VID, 4, 16, QChar('0')).toUpper()))
				.arg((QString("%1").arg(BMP_USB_PID, 4, 16, QChar('0')).toUpper()))
				);

	hDevInfo = SetupDiGetClassDevs (0, L"USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (hDevInfo == INVALID_HANDLE_VALUE)
		return;

	for (i = 0; ; i++)  {
		DeviceInfoData.cbSize = sizeof (DeviceInfoData);
		if (!SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData))
			break;

		status = CM_Get_Device_ID(DeviceInfoData.DevInst, deviceInstanceId , sizeof deviceInstanceId, 0);
		if (status != CR_SUCCESS)
			continue;

		QRegularExpressionMatch match = rx.match(QString::fromWCharArray(deviceInstanceId));
		if (!match.hasMatch())
			continue;

		QString portName;
		if (SetupDiGetDevicePropertyW (hDevInfo, &DeviceInfoData, &DEVPKEY_Device_BusReportedDeviceDesc,
		       &ulPropertyType, (BYTE *) busReportedDeviceDesc, sizeof busReportedDeviceDesc, 0, 0)
				&& !(portName = portNameForSerialNumber(match.captured(1))).isEmpty())

			probeData.push_back(BmpProbeData(QString::fromWCharArray(busReportedDeviceDesc),
							 match.captured(1),
							 portName));
	}
}

#elif defined Q_OS_LINUX

void findConnectedProbes(std::vector<BmpProbeData> &probeData)
{
	QDir d("/dev/serial/by-id");
	QStringList fileNames = d.entryList();
	/* Filenames are of this kind:
	 * usb-Black_Sphere_Technologies_Black_Magic_Probe__STLINK____Firmware_v1.6-rc0-955-ge3fd12eb__E3C89DF4-if00 */
	QRegularExpression rx("usb-Black_Sphere_Technologies_Black_Magic_Probe_+([^_]+)[^v]*([^_]+)_+([^-]+)-if00");
	for (const auto & f : fileNames)
	{
		QRegularExpressionMatch match = rx.match(f);
		if (match.hasMatch())
		{
			probeData.push_back(BmpProbeData("BMP probe, " + match.captured(2) + ", host " + match.captured(1),
							 match.captured(3),
							 d.absolutePath() + '/' + f));
		}
	}

}

#else
#error Unsupported environment.
#endif /* Q_OS_LINUX */
