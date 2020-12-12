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

#ifndef GDBMIRECEIVER_HXX
#define GDBMIRECEIVER_HXX

#include <QObject>
#include <QProcess>
#include <memory>

/*! \todo	This class is redundant. I got it wrong, and messed it up - remove it altogether. */
class GdbMiReceiver : public QObject
{
	Q_OBJECT

public:
	GdbMiReceiver(void) {}
signals:
	void gdbMiOutputLineAvailable(QString line);
public slots:
	void gdbInputAvailable(const QByteArray data)
	{
		data_received += data;
		/* Remove any carriage return characters. */
		data_received = data_received.replace('\r', "");
		int position;
		while ((position = data_received.indexOf('\n')) != -1)
		{
			emit gdbMiOutputLineAvailable(QString::fromLocal8Bit(data_received.left(position)));
			data_received.remove(0, position + 1);
		}
	}
private:
	void panic(...) { *(int*)0=0; }
	QByteArray data_received;
};

#endif // GDBMIRECEIVER_HXX
