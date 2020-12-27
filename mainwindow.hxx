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

#ifndef MAINWINDOW_HXX
#define MAINWINDOW_HXX

#include <QMainWindow>
#include <QProcess>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QTreeWidgetItem>
#include <QFileInfo>
#include <QHash>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QSerialPort>
#include <QSerialPortInfo>

#include <QMessageBox>
#include <QFileSystemWatcher>

#include <QDebug>
#include <QTimer>
#include <QTime>

#include <QSpinBox>
#include <QGroupBox>

#include <memory>
#include <unordered_set>
#include <set>

#include "gdbmireceiver.hxx"
#include "gdb-mi-parser.hxx"
#include "target-corefile.hxx"
#include "gdbserver.hxx"
#include "gdb-remote.hxx"

#include "svdfileparser.hxx"

#include <functional>

namespace std
{
template<> struct hash<QString>
{
	std::size_t operator()(const QString& s) const noexcept
	{
		return (size_t) qHash(s);
	}
};
}

namespace Ui {
class MainWindow;
}

/*! \todo	Merge this, and the `Util` class, this one got misnamed. */
class Utils
{
public:
static QString filenameToWindowsFilename(const QString & filename)
{
/* A regular expression used for detecting msys paths, that need to be adjusted on windows systems.
 * This is not really exact... */
QRegularExpression rx("^/(\\w)/");

	QRegularExpressionMatch match = rx.match(filename);
	if (match.hasMatch())
		return QString(filename).replace(rx, match.captured(1) + ":/");
	return filename;
}

};


class BlackMagicProbe : public QObject
{
	Q_OBJECT
private:
	QSerialPort port;
	QByteArray incomingData;
	enum
	{
		INVALID,
		DETECTING_PORT,
		CONNECTED,
		SYNC,
		ASYNC,
		PASSTHROUGH,
	}
	portState = INVALID;
	bool blackmMagicTargetConnected = false;
	void dumpIncomingPackets(void)
	{
		QByteArray packet;
		qDebug() << "dumping packets";
		while ((packet = GdbRemote::extractPacket(incomingData)).length())
		{
			port.write("+");
			qDebug() << "confirmed";
			auto packetContents = GdbRemote::packetData(packet);
			if (packetContents.length() && packetContents.at(0) == 'O' && !GdbRemote::isOkResponse(packetContents))
				qDebug() << QByteArray::fromHex(packetContents.right(packetContents.length() - 1));
			qDebug() << packet;
		}
	}
	bool receivePacketConfirmation(void)
	{
		int len = incomingData.length();
		qDebug() << "expecting confirmation...";
		if (!port.waitForReadyRead(3000))
		{
			if (len != incomingData.length())
				*(int*)0=0;
			/*! \todo	This is an error - handle it better at this place. */
			*(int *)0=0;
		}
		incomingData += port.readAll();
		if (!incomingData.length())
			/*! \todo	This is an error - handle it better at this place. */
			*(int *)0=0;
		if (incomingData.at(0) != '+')
			*(int *)0=0;
		incomingData.remove(0, 1);
		qDebug() << "confirmation received";
		return true;
	}
signals:
	void dataAvailable(const QByteArray & data);
	void BlackMagicProbeConnected(void);
	void BlackMagicProbeDisconnected(void);
	void BlackMagicTargetConnected(void);
	void BlackMagicTargetDisconnected(void);
private slots:
	void errorOccurred(QSerialPort::SerialPortError error)
	{
		if (error != QSerialPort::NoError)
		{
			qDebug() << "blackmagic probe error occurred, error code:" << error;
			if (port.isOpen())
			{
				port.blockSignals(true);
				qDebug() << "cached data available when closing the port:" << incomingData;
				qDebug() << "data available when closing the port:" << port.readAll();
				port.setDataTerminalReady(false);
				port.close();
				emit BlackMagicProbeDisconnected();
				connect(& port, SIGNAL(errorOccurred(QSerialPort::SerialPortError)), this, SLOT(errorOccurred(QSerialPort::SerialPortError)));
				port.blockSignals(false);
			}
		}
	}
	void portReadyRead(void)
	{
		qDebug() << "blackmagic data available; state is:" << portState;
		if (portState == ASYNC)
		{
			incomingData += port.readAll();
			dumpIncomingPackets();
		}
		else if (portState == PASSTHROUGH)
			emit dataAvailable(port.readAll());
		//incomingData += port.readAll();
	}
public:
	BlackMagicProbe(void)
	{
		connect(& port, SIGNAL(errorOccurred(QSerialPort::SerialPortError)), this, SLOT(errorOccurred(QSerialPort::SerialPortError)));
		connect(& port, SIGNAL(readyRead()), this, SLOT(portReadyRead()));
	}
	void connectToProbe(void)
	{
		auto serialPorts = QSerialPortInfo::availablePorts();
		if (port.isOpen())
		{
			port.setDataTerminalReady(false);
			port.close();
			emit BlackMagicProbeDisconnected();
		}
		portState = DETECTING_PORT;
		for (const auto & p : serialPorts)
		{
			qDebug() << p.portName() << p.description() << p.manufacturer() << p.vendorIdentifier() << p.productIdentifier() << p.systemLocation();
			port.setPort(p);
			if (port.open(QSerialPort::ReadWrite))
			{
				incomingData.clear();
				port.setDataTerminalReady(false);
				port.setDataTerminalReady(true);
				while (true)
				{
					int len = incomingData.length();
					if (!port.waitForReadyRead(500))
					{
						if (len != incomingData.length())
							*(int*)0=0;
						break;
					}
					incomingData += port.readAll();
					QByteArray packet = GdbRemote::extractPacket(incomingData);
					if (GdbRemote::packetData(packet) == "OK")
					{
						port.write("+");
						qDebug() << "confirmed";
						qDebug() << "probe detected, returning";
						//////!!!!!!!!!!!!portState = ASYNC;
						portState = PASSTHROUGH;
						emit BlackMagicProbeConnected();
						return;
					}

				}
				port.setDataTerminalReady(false);
				port.close();
			}
		}
		return;
	}
	void connectToFirstTarget(void)
	{
		if (!port.isOpen())
			return;
		qDebug() << "START";
		QString response;
		portState = SYNC;
		//incomingData.clear();
		sendRawGdbPacket(GdbRemote::monitorRequest("swdp_scan"), true);
		while (true)
		{
			incomingData += port.readAll();
			while (1)
			{
				auto packet = GdbRemote::extractPacket(incomingData);
				if (!packet.length())
					break;
				port.write("+");
				if (GdbRemote::isOkResponse(packet))
				{
					qDebug() << "target detection complete, response:" << response;
					goto out;
				}
				if (GdbRemote::isErrorResponse(packet))
				{
					qDebug() << "target detection complete with errors, response:" << response;
					goto out;
				}
				auto packetContents = GdbRemote::packetData(packet);
				if (packetContents.length() && packetContents.at(0) == 'O')
					response += QByteArray::fromHex(packetContents.right(packetContents.length() - 1));
				else
				{
					qDebug() << "unexpected packet received, ignoring:" << packetContents;
				}
			}
			if (!port.waitForReadyRead(1000))
			{
				qDebug() << "error receiving packet from the blackmagic probe";
				response.clear();
				break;
			}
		}
out:
		//dumpIncomingPackets();
		portState = ASYNC;
		qDebug() << "END";
	}
	/*! \todo This is broken when sending packets from the user interface, and I have no idea why (yet). */
	void sendRawGdbPacket(const QByteArray & packet, bool waitForConfirmation = false, bool confirmPacketsAlreadyReceived = false)
	{
		if (port.isOpen())
		{
			auto savedState = portState;
			portState = SYNC;
			port.write(packet);
			if (waitForConfirmation)
				if (!receivePacketConfirmation())
					*(int*)0=0;
			/*! \todo This is for handling any received monitor packets when issuing monitor requests from
			 * the user interface. This is really messy, and buggy at this time, fix this... */
			if (confirmPacketsAlreadyReceived)
			{
				qDebug() << "xxx";
				dumpIncomingPackets();
				qDebug() << "yyy";
			}
			portState = savedState;
		}
		else
			*(int*)0=0;
	}
};

class BlackmagicTargetAttacher : public QObject
{
	Q_OBJECT
private:
	bool sendPacketAndGetResponse(const QByteArray packet, QList<QByteArray> & responsePackets)
	{
		responsePackets.clear();
		QTcpSocket s;
		auto returnError = [&] (const QString& errorMessage) -> bool
		{
			if (s.isOpen())
				s.close();
			qDebug() << errorMessage;
			emit targetCommunicationError(errorMessage);
			return false;
		};
		s.connectToHost("localhost", 1122);
		if (!s.waitForConnected(3000))
			return returnError("Failed to connect to gdbserver socket");
		QByteArray incomingData;
		s.write(packet);

		if (!s.waitForBytesWritten())
			return returnError("Error writing to the gdbserver socket");
		if (!s.waitForReadyRead())
			return returnError("Error reading from the gdbserver socket");
		incomingData += s.readAll();
		if (incomingData.at(0) != '+')
			return returnError("Error receiving packet confirmation from the gdbserver");
		incomingData.remove(0 ,1);
		while (1)
		{
			QByteArray packet;
			while (!(packet = GdbRemote::extractPacket(incomingData)).isEmpty())
			{
				s.write("+");
				responsePackets << packet;
				if (!s.waitForBytesWritten())
					return returnError("Error writing to the gdbserver socket");

				qDebug() << __func__ << ": received packet: " << GdbRemote::packetData(packet);
				if (GdbRemote::isOkResponse(packet) || GdbRemote::isErrorResponse(packet)
						|| GdbRemote::isTargetStopReplyPacket(packet))
					return true;
			}

			if (!s.waitForReadyRead())
				return returnError("Error reading from the gdbserver socket");
			incomingData += s.readAll();
		}

	}
public slots:

	void attachToFirstTarget(void)
	{
		QList<QByteArray> packets;
		if (!sendPacketAndGetResponse(GdbRemote::monitorRequest("swdp_scan"), packets))
			return;
		if (packets.size() && GdbRemote::isErrorResponse(packets.last()))
		{
			emit targetCommunicationError("Failed to scan targets with 'swdp_scan'");
			return;
		}

		if (!sendPacketAndGetResponse(GdbRemote::attachRequest(), packets))
		{
			emit targetCommunicationError("Error attaching to first target");
			return;
		}
		if (packets.size() == 1 && GdbRemote::isTargetStopReplyPacket(packets.at(0)))
			emit targetAttached();
		else
			emit targetCommunicationError("Failed to attach to target");
	}
signals:
	void targetAttached(void);
	void targetCommunicationError(const QString errorMessage);
};

class PassThroughGdbserver : public QObject
{
	Q_OBJECT
public:
	PassThroughGdbserver(BlackMagicProbe * probe)
	{
		blackmagicProbe = probe;
		gdb_tcpserver.setMaxPendingConnections(1);
		connect(& gdb_tcpserver, SIGNAL(newConnection()), this, SLOT(newConneciton()));
		if (!gdb_tcpserver.listen(QHostAddress::Any, 1122))
		{
			QMessageBox::critical(0, "Cannot listen on gdbserver port", "Error opening a gdbserver port for listening for incoming gdb connections");
			return;
		}
		connect(probe, SIGNAL(dataAvailable(QByteArray)), this, SLOT(portDataAvailable(QByteArray)));
	}
	void closeConnection(void) { gdb_client_socket->close(); }
private:
	BlackMagicProbe	* blackmagicProbe;
	QTcpServer	gdb_tcpserver;
	/*! \todo	I think there is a race with the 'gdb_client_socket' variable. */
	QTcpSocket	* gdb_client_socket = 0;
private slots:
	void newConneciton(void)
	{
		qDebug() << "gdb client connected";
		/*! \todo	I think there is a race with the 'gdb_client_socket' variable - if a signal arrives between the variable assignment,
		 * 		and the slot connections - fix this. */
		gdb_client_socket = gdb_tcpserver.nextPendingConnection();
		connect(gdb_client_socket, &QTcpSocket::disconnected, [&] { gdb_client_socket->disconnect(); gdb_client_socket = 0; qDebug() << "gdb client disconnected"; });
		connect(gdb_client_socket, SIGNAL(readyRead()), this, SLOT(gdbClientSocketReadyRead()));
	}
	void gdbClientSocketReadyRead(void)
	{
		blackmagicProbe->sendRawGdbPacket(gdb_client_socket->readAll());
	}
	void portDataAvailable(const QByteArray & data) { gdb_client_socket->write(data); }
};

class StringFinder : public QObject
{
	Q_OBJECT
public:
	enum
	{
		MAX_RETURNED_SEARCH_RESULTS	= 1000,
	};
	struct SearchResult
	{
		const QString fullFileName;
		const QString sourceCodeLineText;
		int lineNumber = -1;
		SearchResult(){}
		SearchResult(const QString & fullFileName, const QString & sourceCodeLineText, int lineNumber) :
			fullFileName(fullFileName), sourceCodeLineText(sourceCodeLineText), lineNumber(lineNumber) {}
	};
	StringFinder() {}

	enum SEARCH_FLAGS_ENUM
	{
		SEARCH_FOR_WHOLE_WORDS_ONLY = 1 << 0,
	};

public slots:
	void findString(const QString & str, unsigned flags = 0)
	{
		int fileCount = 0;
		QSharedPointer<QVector<SearchResult>> results(QSharedPointer<QVector<SearchResult>>::create());
		/*! \todo	Properly escape all regular expression special characters. I am not sure all of them are
		 *		escaped at this moment. */
		QString pattern = str;
		const QString specialCharacters = "\\^$.[]|()?*+";
		for (const QChar & c : specialCharacters)
			pattern.replace(c, QString("\\") + c);
		if (flags & SEARCH_FOR_WHOLE_WORDS_ONLY)
			pattern = QString("\\b") + pattern + "\\b";
		QRegularExpression rx;
		rx.setPattern(pattern);
		for (const auto & fileName : sourceCodeFiles)
		{
			QFile f(fileName);
			if (!QFileInfo(fileName).exists())
			{
				/* Attempt to adjust the filename path on windows systems. */
				f.setFileName(Utils::filenameToWindowsFilename(fileName));
			}
			if (!f.open(QFile::ReadOnly))
				continue;
			fileCount ++;
			int i = 1;
			QList<QByteArray> lines = f.readAll().split('\n');
			for (const auto & line : lines)
			{
				if (rx.match(line).lastCapturedIndex() != -1)
					* results.get() << SearchResult(fileName, line, i);
				i ++;
				if (results.get()->size() >= MAX_RETURNED_SEARCH_RESULTS)
				{
					emit searchReady(str, results, true);
					return;
				}
			}
		}
		qDebug() << "files searched:" << fileCount;
		emit searchReady(str, results, false);
	}
	void addFilesToSearchSet(const QStringList & sourceCodeFiles)
	{
		for (const auto & f : sourceCodeFiles)
			this->sourceCodeFiles.insert(f);
	}

signals:
	void searchReady(const QString pattern, const QSharedPointer<QVector<StringFinder::SearchResult>> results, bool resultsTruncated);
private:
	std::set<QString> sourceCodeFiles;
};

Q_DECLARE_METATYPE(QSharedPointer<QVector<StringFinder::SearchResult>>)

struct GdbVarObjectTreeItem
{
private:
	QList<GdbVarObjectTreeItem *> children;
	GdbVarObjectTreeItem	* parent = 0;
	/* This is the 'numchild' value as reported by the gdb mi varobject report. */
	int reportedChildCount = 0;
public:
	QString miName;
	QString name;
	QString type;
	QString value;
	bool isChildrenFetchingInProgress = false;
	bool isInScope = true;
	int getReportedChildCount(void) const { return isInScope ? reportedChildCount : 0; }
	int setReportedChildCount(int reportedChildCount) { this->reportedChildCount = reportedChildCount; }
	void deleteChildren(void) { qDeleteAll(children); children.clear(); }

	~GdbVarObjectTreeItem() { deleteChildren(); }

	GdbVarObjectTreeItem * parentItem(void) { return parent; }
	int childCount(void) const { return children.count(); }
	int columnCount(void) const { return 4; }

	void appendChild(GdbVarObjectTreeItem * child) { children.push_back(child); child->parent = this; }
	GdbVarObjectTreeItem * child(int row) const { return children.at(row); }
	QVariant data(int column) const
	{
		switch (column)
		{
		case 0: return name;
		case 1: return value;
		case 2: return type;
		case 3: /* Special case - if the 'value' string is recognized to be a decimal number,
			 * return the hexadecimal representation of this number. */
		{
			bool ok;
			unsigned long long v = value.toULongLong(& ok);
			if (!ok)
				return "???";
			return QString("0x%1").arg(v, 0, 16);
		}
		default: return "<<< bad column number >>>";
		}
	}
	/*! \todo	I don't understand why the 'const_cast' is needed here. */
	int row() const { if (parent) return parent->children.indexOf(const_cast<GdbVarObjectTreeItem *>(this)); return 0; }
	void dump(int indentationLevel = 0)
	{
		qDebug() << QString(indentationLevel, ' ') << miName << ":" << name;
		for (const auto & c : children)
			c->dump(indentationLevel + 1);
	}
};

class GdbVarObjectTreeItemModel : public QAbstractItemModel
{
	Q_OBJECT
private:
	/* Dummy root node. */
	/*! \todo	THIS CURRENTLY LEAKS MEMORY */
	GdbVarObjectTreeItem root;
	/*! \todo	This is very evil... */
	std::unordered_set<const GdbVarObjectTreeItem *> highlightedItems;

	void markIndexAsChanged(const QModelIndex & nodeIndex, const GdbVarObjectTreeItem * item = 0)
	{
		if (!item)
		{
			if (!nodeIndex.isValid())
				return;
			item = static_cast<GdbVarObjectTreeItem *>(nodeIndex.internalPointer());
		}
		int row = item->row();
		emit dataChanged(index(row, 0, nodeIndex.parent()), index(row, item->columnCount() - 1, nodeIndex.parent()));
	}
public:
	GdbVarObjectTreeItemModel(QObject * parent = 0) : QAbstractItemModel(parent)
	{
		root.name = "Name";
		root.value = "Value";
		root.type = "Type";
	}
	~GdbVarObjectTreeItemModel() { /*! \todo WRITE THIS */ }
	void dumpTree()
	{
		root.dump();
	}
	void setHighlightedItems(const std::unordered_set<const GdbVarObjectTreeItem *> items) { highlightedItems = items; }

	void appendRootItem(GdbVarObjectTreeItem * item)
	{
		emit beginInsertRows(QModelIndex(), root.childCount(), root.childCount());
		root.appendChild(item);
		emit endInsertRows();
	}

	QVariant data(const QModelIndex & index, int role) const override
	{
		if (!index.isValid())
			return QVariant();
		const GdbVarObjectTreeItem * node = static_cast<GdbVarObjectTreeItem*>(index.internalPointer());
		if (role == Qt::ForegroundRole && highlightedItems.count(node))
			return QBrush(Qt::red);
		if (role != Qt::DisplayRole)
			return QVariant();
		return node->data(index.column());

	}
	Qt::ItemFlags flags(const QModelIndex &index) const override
	{
		if (!index.isValid())
			return 0;
		return QAbstractItemModel::flags(index);
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
	{
		if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
		{
			switch (section)
			{
				case 0: return("Name");
				case 1: return("Value");
				case 2: return("Type");
				case 3: return("Hex value");
			default: *(int*)0=0;
			}
		}
		return QVariant();
	}
	QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const override
	{
		if (!hasIndex(row, column, parent))
			return QModelIndex();
		const GdbVarObjectTreeItem * t;
		if (!parent.isValid())
			t = & root;
		else
			t = static_cast<GdbVarObjectTreeItem *>(parent.internalPointer());
		GdbVarObjectTreeItem * c = t->child(row);
		if (c)
			return createIndex(row, column, c);
		return QModelIndex();
	}
	QModelIndex parent(const QModelIndex & index) const override
	{
		if (!index.isValid())
			return QModelIndex();
		GdbVarObjectTreeItem * child = static_cast<GdbVarObjectTreeItem *>(index.internalPointer()), * parent = child->parentItem();
		if (parent == & root)
			return QModelIndex();
		return createIndex(parent->row(), 0, parent);
	}
	int rowCount(const QModelIndex &parent = QModelIndex()) const override
	{
		const GdbVarObjectTreeItem * t;
		if (parent.column() > 0)
			return 0;

		if (!parent.isValid())
			t = & root;
		else
			t = static_cast<GdbVarObjectTreeItem *>(parent.internalPointer());

		return t->childCount();
	}
	int columnCount(const QModelIndex &parent = QModelIndex()) const override
	{
		if (parent.isValid())
			return static_cast<GdbVarObjectTreeItem *>(parent.internalPointer())->columnCount();
		else
			return root.columnCount();
	}
	bool hasChildren(const QModelIndex &parent) const override
	{
		if (!parent.isValid())
			return root.childCount();
		GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(parent.internalPointer());
		return t->getReportedChildCount() || t->childCount();
	}
	bool canFetchMore(const QModelIndex &parent) const override
	{
		if (!parent.isValid())
			return false;
		GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(parent.internalPointer());
		return (t->getReportedChildCount() != t->childCount()) && !t->isChildrenFetchingInProgress;
	}
	void fetchMore(const QModelIndex &parent)
	{
		if (!parent.isValid())
			return;
		GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(parent.internalPointer());
		t->isChildrenFetchingInProgress = true;
		if (t->getReportedChildCount() && !t->childCount())
			emit readGdbVarObjectChildren(parent);
	}
	void childrenFetched(const QModelIndex &parent, const std::vector<GdbVarObjectTreeItem *>& children)
	{
		if (!parent.isValid())
			return;
		emit beginInsertRows(parent, 0, children.size() - 1);
		GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(parent.internalPointer());
		t->isChildrenFetchingInProgress = false;
		for (const auto & c : children)
			t->appendChild(c);
		emit endInsertRows();
	}
	void updateNodeValue(const QModelIndex & nodeIndex, const QString & newValue)
	{
		if (!nodeIndex.isValid())
			return;
		GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(nodeIndex.internalPointer());
#if 0
		/* Sanity checks - this must be a leaf node. */
		/* It turns out that this case IS possible. It has been observed on an expression like "(char(*)[8])buf".
		 * In this case, gdb reports that the value has been "optimized out", and reports empty values for
		 * the varobject chidlren. */
		if (t->childCount())
			*(int*)0=0;
#endif
		t->value = newValue;
		markIndexAsChanged(nodeIndex, t);
	}
	void updateNodeType(const QModelIndex & nodeIndex, const QString & newType, const QString & newValue, int newNumChildren)
	{
		if (!nodeIndex.isValid())
			return;
		GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(nodeIndex.internalPointer());
		if (t->childCount())
		{
			beginRemoveRows(nodeIndex, 0, t->childCount() - 1);
			t->deleteChildren();
			endRemoveRows();
		}
		t->type = newType;
		t->value = newValue;
		t->setReportedChildCount(newNumChildren);
		markIndexAsChanged(nodeIndex, t);
	}
	void markNodeAsOutOfScope(const QModelIndex & nodeIndex)
	{
		if (!nodeIndex.isValid())
			return;
		GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(nodeIndex.internalPointer());
		if (!t->isInScope)
			return;
		t->isInScope = false;
		if (t->childCount())
		{
			beginRemoveRows(nodeIndex, 0, t->childCount() - 1);
			t->deleteChildren();
			endRemoveRows();
		}
		t->value = "<<< data object out of scope >>>";
		t->isInScope = false;
		/*! \todo DON'T JUST WIPE OUT THE TYPE STRING: t->type = "???"; */
		markIndexAsChanged(nodeIndex, t);
	}
	void markNodeAsInsideScope(const QModelIndex & nodeIndex)
	{
		if (!nodeIndex.isValid())
			return;
		GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(nodeIndex.internalPointer());
		if (t->isInScope)
			return;
		t->isInScope = true;
		/*! \todo DON'T JUST WIPE OUT THE TYPE STRING: t->type = "???"; */
		markIndexAsChanged(nodeIndex, t);
	}
signals:
	void readGdbVarObjectChildren(const QModelIndex parent);
};


class MainWindow : public QMainWindow
{
	Q_OBJECT

private:
	/* The minimum string length that needs to be entered so that the object locator performs a search for the text entered. */
	const int MIN_STRING_LENGTH_FOR_OBJECT_LOCATOR = 3;

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow();

public slots:
	void gdbMiLineAvailable(QString line);
private slots:
	void displayHelp(void);
	void on_lineEditGdbCommand_returnPressed();
	void gdbProcessError(QProcess::ProcessError error);
	void gdbProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void gdbStandardErrorDataAvailable(void);
	void sendDataToGdbProcess(const QString &data);
	void readGdbVarObjectChildren(const QModelIndex parent);
	void showSourceCode(QTreeWidgetItem * item);
	QString getExecutableFilename(void);
	void breakpointsContextMenuRequested(QPoint p);
	void svdContextMenuRequested(QPoint p);
	void bookmarksContextMenuRequested(QPoint p);
	void breakpointViewItemChanged(QTreeWidgetItem * item, int column);
	void stringSearchReady(const QString pattern, QSharedPointer<QVector<StringFinder::SearchResult>> results, bool resultsTruncated);
	void targetAttacherErrorReceived(const QString errorMessage) { QMessageBox::critical(0, "Error communicating with target", errorMessage); }
	void targetAttacherSuccess(void) {
		connectGdbToGdbServer();
		QMessageBox::information(0, "Successfully attached to target", "Successfully attached to target");
	}
	void connectGdbToGdbServer(void);
	void createSvdRegisterView(QTreeWidgetItem * item, int column);

	void updateSourceListView(void);
	void updateSymbolViews(void);
	void updateBreakpointsView(void);
	void updateBookmarksView(void);

	void on_lineEditGdbMiCommand_returnPressed();

	void on_lineEditVarObjectExpression_returnPressed();


	void on_lineEditSearchFilesForText_returnPressed();

	void on_lineEditObjectLocator_textChanged(const QString &arg1);

	void on_pushButtonDeleteAllBookmarks_clicked();

	void on_pushButtonConnectToBlackmagic_clicked();

	void on_lineEditBlackmagicMonitorCommand_returnPressed();

	void on_pushButtonDisconnectGdbServer_clicked();

	void on_lineEditFindText_returnPressed();

	void on_pushButtonRequestGdbHalt_clicked();

	void on_pushButtonDumpVarObjects_clicked();

	void on_pushButtonLoadProgramToTarget_clicked();

	void on_comboBoxSelectLayout_activated(int index);

	void on_pushButtonXmlTest_clicked();

private:
	struct SourceCodeLocation
	{
		QString	fullFileName;
		int	lineNumber = -1;
		SourceCodeLocation(const QString & fullFileName = QString(), int lineNumber = -1) :
			fullFileName(fullFileName), lineNumber(lineNumber) {}
		bool operator ==(const SourceCodeLocation & other) const
			{ return other.lineNumber == lineNumber && other.fullFileName == fullFileName; }
		bool operator !=(const SourceCodeLocation & other) const
			{ return ! operator ==(other); }
	};
	struct
	{
	private:
		std::vector<SourceCodeLocation> locations;
	public:
		int size(void) const { return locations.size(); }
		const SourceCodeLocation & back(void) const { return locations.back(); }
		void drop() { if (locations.size()) locations.pop_back(); }
		void dump(void) {
			qDebug() << "navigation stack dump:";
			for (const auto & l : locations)
				qDebug() << l.fullFileName << l.lineNumber;
			qDebug() << "------------- navigation stack dump end";
		}
		void push(const struct SourceCodeLocation & location)
		{
			if (locations.size() == 0 || locations.back() != location)
				locations.push_back(location);
		}
	}
	navigationStack;
	struct GdbTokenContext
	{
		struct GdbResponseContext
		{
			enum GDB_RESPONSE_ENUM
			{
				GDB_RESPONSE_INVALID = 0,
				/* Response to the "-var-create - @ \"<expression>\"" machine interface gdb command. */
				GDB_RESPONSE_NAME,
				/* Response to the "-var-list-children --all-values <varobject>" machine interface gdb command. */
				GDB_RESPONSE_NUMCHILD,
				/* Response to the "-file-list-exec-source-files" machine interface gdb command. */
				GDB_RESPONSE_FILES,
				/* Response to the "-symbol-list-lines <filename>" machine interface gdb command. */
				GDB_RESPONSE_LINES,
				/* Annotated response to the "-file-exec-and-symbols <filename>" machine interface gdb command. */
				GDB_RESPONSE_EXECUTABLE_SYMBOL_FILE_LOADED,
				/* Annotated response to the "-symbol-info-functions" machine interface gdb command. */
				GDB_RESPONSE_FUNCTION_SYMBOLS,
				/* Annotated response to the "-symbol-info-variables" machine interface gdb command. */
				GDB_RESPONSE_VARIABLE_SYMBOLS,
				/* Annotated response to the "-symbol-info-types" machine interface gdb command. */
				GDB_RESPONSE_TYPE_SYMBOLS,

				/* When receiving this response code, update the breakpoint information by
				 * issuing a "-break-list" gdb machine interface command.
				 * This may be needed when issuing breakpoint commands, such as
				 * "-break-delete", "-break-enable", "-break-disable", etc., because
				 * gdb answers to such commands with a "^done" response packet,
				 * with no other details available. */
				GDB_REQUEST_BREAKPOINT_LIST_UPDATE,

				/*******************************************************
				 * The codes below are not really responses from gdb.
				 * Instead, they are meant to serve as 'checkpoints', or
				 * 'sequence points', when talking to gdb.
				 *
				 * For example, it may be needed that the list of source
				 * code files, that are used to build the target
				 * executable, be constructed. However, it may be the case
				 * that only those source code files, for which actual
				 * machine code has been generated, be displayed.
				 * In such a case, all of the lists of machine code
				 * addresses for all source code files need to be fetched
				 * from gdb before the list can be constructed.
				 * In this case, a list of gdb "-symbol-list-lines"
				 * command is sent to gdb, for all source code files
				 * reported by gdb, and after these commands,
				 * an empty command is sent to gdb, containing only
				 * a token number, i.e. "<token-number><cr>". In this
				 * case, gdb will reply to all "-symbol-list-lines"
				 * requests, and after that will respond with an empty
				 * "<token-number>^done<cr>" packet, so in this case
				 * the token number, along with the pseudo
				 * gdb answer code from here, is used to determine that
				 * the list of source code files should already have been
				 * retrieved, and so the final list of source code files
				 * (of only those source files that actually contain any
				 * machine code) can be built. */
				GDB_SEQUENCE_POINT_SOURCE_CODE_ADDRESSES_RETRIEVED,
			};
			enum GDB_RESPONSE_ENUM gdbResponseCode = GDB_RESPONSE_INVALID;
			QString		s;
			void		* p = 0;
			GdbResponseContext(enum GDB_RESPONSE_ENUM gdbResponseCode) : gdbResponseCode(gdbResponseCode) {}
			GdbResponseContext(enum GDB_RESPONSE_ENUM gdbResponseCode, const QString & s) : gdbResponseCode(gdbResponseCode), s(s) {}
			GdbResponseContext(enum GDB_RESPONSE_ENUM gdbResponseCode, const QString & s, void * p) : gdbResponseCode(gdbResponseCode), s(s), p(p) {}
		};

		struct GdbResponseContext readAndRemoveContext(unsigned tokenNumber)
		{
			auto t = gdbTokenContextMap.find(tokenNumber);
			if (t == gdbTokenContextMap.end())
				*(int*)0=0;
			struct GdbResponseContext c = t->second;
			gdbTokenContextMap.erase(t);
			gdbTokenPool[tokenNumber >> 3] &=~ (1 << (tokenNumber & 7));
			return c;
		}
		const struct GdbResponseContext & contextForTokenNumber(unsigned tokenNumber)
		{
			auto t = gdbTokenContextMap.find(tokenNumber);
			if (t == gdbTokenContextMap.end())
				*(int*)0=0;
			return t->second;
		}
		unsigned insertContext(const GdbResponseContext & context)
		{
			unsigned t = getTokenNumber();
			gdbTokenContextMap.insert(std::pair<unsigned, struct GdbResponseContext>(t, context));
			return t;
		}
		bool hasContextForToken(unsigned tokenNumber)
		{
			/* Token number 0 is regarded as invalid, and must never be used. */
			if (!tokenNumber) return false;
			return gdbTokenContextMap.count(tokenNumber) != 0;
		}
	private:
		enum
		{
			GDB_TOKEN_POOL_SIZE_BYTES	= 1024,
			GDB_TOKEN_POOL_BASE_NUMBER	= 1024,
		};
		unsigned getTokenNumber(void)
		{
			size_t i;
			for (i = 0; i < sizeof gdbTokenPool; i ++)
				if (gdbTokenPool[i] != 0xff)
				{
					int t = 0;
					while (1)
					{
						if (!(gdbTokenPool[i] & (1 << t)))
						{
							gdbTokenPool[i] |= 1 << t;
							return (i << 3) + t;
						}
						t ++;
					}
				}
			*(int*)0=0;
		}
		std::unordered_map<unsigned /* token number */, struct GdbResponseContext> gdbTokenContextMap;
		/* Token number 0 is invalid, so should be never allocated. */
		uint8_t gdbTokenPool[GDB_TOKEN_POOL_SIZE_BYTES] = { 1, };
	}
	gdbTokenContext;
	struct GdbBreakpointData
	{
		enum
		{
			TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER	= 3,
		};
		QString		gdbReportedNumberString = "???";
		QString		type = "<<< unknown >>>", disposition = "<<< unknown >>>";
		bool		enabled = false;
		uint32_t	address = -1;
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
			/*! \todo	Also handle multiple-location breakpoints here. */
			for (const auto & b : breakpoints)
				if (b.sourceCodeLocation == sourceCodeLocation)
					foundBreakpoints.push_back(& b);
		}
	};
	std::vector<GdbBreakpointData>	breakpoints;

	struct
	{
		QTextCharFormat navigatedLine;
		QTextCharFormat currentLine;
		QTextCharFormat enabledBreakpoint;
		QTextCharFormat disabledBreakpoint;
		QTextCharFormat bookmark;
		QTextCharFormat searchedText;
	}
	sourceCodeViewHighlightFormats;
	struct
	{
		QList<QTextEdit::ExtraSelection> navigatedSourceCodeLine;
		QList<QTextEdit::ExtraSelection> currentSourceCodeLine;
		QList<QTextEdit::ExtraSelection> disabledBreakpointedLines;
		QList<QTextEdit::ExtraSelection> enabledBreakpointedLines;
		QList<QTextEdit::ExtraSelection> bookmarkedLines;
		QList<QTextEdit::ExtraSelection> searchedTextMatches;
	}
	sourceCodeViewHighlights;

	struct
	{
		/* Last searched text in the current source code document. */
		QString lastSearchedText;
		/* The matching positions for the text that has been last serched in the
		 * currently displayed source code document. This is used for highlighting matching text
		 * in the current document, and for navigating between matches. */
		std::vector<int /* Absolute position in document. */> matchPositions;
	}
	searchData;

private:

	enum TARGET_STATE
	{
		GDBSERVER_DISCONNECTED = 0,
		TARGET_RUNNING,
		TARGET_STOPPED,
	}
	target_state = GDBSERVER_DISCONNECTED;
	struct
	{
		QWidgetList enabledWidgetsWhenGdbServerDisconnected;
		QWidgetList disabledWidgetsWhenGdbServerDisconnected;
		QWidgetList enabledWidgetsWhenTargetRunning;
		QWidgetList disabledWidgetsWhenTargetRunning;
		QWidgetList enabledWidgetsWhenTargetStopped;
		QWidgetList disabledWidgetsWhenTargetStopped;
		void enterTargetState(enum TARGET_STATE target_state)
		{
			switch (target_state)
			{
			default:
				*(int*)0=0;
			case GDBSERVER_DISCONNECTED:
				for (const auto & w : disabledWidgetsWhenGdbServerDisconnected)
					w->setEnabled(false);
				for (const auto & w : enabledWidgetsWhenGdbServerDisconnected)
					w->setEnabled(true);
				break;
			case TARGET_RUNNING:
				for (const auto & w : disabledWidgetsWhenTargetRunning)
					w->setEnabled(false);
				for (const auto & w : enabledWidgetsWhenTargetRunning)
					w->setEnabled(true);
				break;
			case TARGET_STOPPED:
				for (const auto & w : disabledWidgetsWhenTargetStopped)
					w->setEnabled(false);
				for (const auto & w : enabledWidgetsWhenTargetStopped)
					w->setEnabled(true);
				break;
			}
		}
	}
	targetStateDependentWidgets;
	bool isGdbServerConnectedToGdb = false;
	QString loadedExecutableFileName;

	QFileSystemWatcher sourceFileWatcher;
	QString displayedSourceCodeFile;
	void highlightBreakpointedLines(void);
	void highlightBookmarks(void);
	void refreshSourceCodeView(void);
	void displaySourceCodeFile(const SourceCodeLocation & sourceCodeLocation, bool saveCurrentLocationToNavigationStack = true);
	void parseGdbCreateVarObjectResponse(const QString & variableExpression, const QString response, struct GdbVarObjectTreeItem & node);
	void navigateToSymbolAtCursor(void);
	void navigateBack(void);
	QString escapeString(const QString & s) { QString t = s; return t.replace('\\', "\\\\").replace('\"', "\\\""); }

	/* Returns true, if the source view was refreshed, false otherwise. */
	bool searchCurrentSourceText(const QString pattern);
	void moveCursorToNextMatch(void);
	void moveCursorToPreviousMatch(void);

	struct SourceFileData {
		/* Enumeration constants used as a 'role' parameter in the treeview widget used for displaying
		 * the list of source code files. */
		enum
		{
			FILE_NAME = Qt::UserRole,
			LINE_NUMBER,
			/* The data contents are a 'void' pointer that can be cast to a 'GdbBreakpointData' data structure. */
			BREAKPOINT_DATA_POINTER,
			/* Generic, 'void' pointer, used for referencing some data structures from a tree widget
			 * item. The interpretation of this pointer depends on context. It is expected that,
			 * depending on context, this pointer will be cast to the actual type that it points
			 * to, before use. */
			GENERIC_DATA_POINTER,
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
			int line = -1;
			/* For data type symbols, only the 'name' field is appropriate. */
			QString	name, type, description;
			const bool operator ==(const SymbolData & other) const
			{
				return name == other.name && type == other.type && description == other.description && line == other.line;
			}
		};
		struct SymbolHash
		{
			size_t operator ()(const MainWindow::SourceFileData::SymbolData & t) const
			{
				return std::hash<std::string>()(QString("%1:%2:%3:%4").arg(t.name).arg(t.type).arg(t.description).arg(t.line).toStdString());
			}
		};

		/*! \todo	Make the 'subprograms' and 'variables' fields below unordered_set-s as well. */
		std::vector<struct SymbolData> subprograms;
		std::vector<struct SymbolData> variables;
		std::unordered_set<struct SymbolData, SymbolHash> dataTypes;
	};
	struct StackFrameData
	{
		QString fileName, gdbReportedFileName, fullFileName, subprogramName;
		/* Frame number 0 is the innermost (most recent) stack frame. */
		int		level = -1;
		int		lineNumber = -1;
		uint32_t	pcAddress = -1;
	};
	std::vector<StackFrameData>	backtrace;

	/* NOTE: it was found that QHash orders elements nondeterministically,
	 * i.e., in different runs of the program, inserting the same elements in the same order,
	 * afterwards results, when iterating over the elements with 'for (const auto & t : sourceFiles)',
	 * in different order of accessing the elements in the hash table.
	 *
	 * This accidentally revealed some strange behavior in gdb, in which gdb
	 * returns different responses to the '-symbol-list-lines' machine interface
	 * commands to gdb, for the same arguments of the '-symbol-list-lines', depending on
	 * the order in which the '-symbol-list-lines' machine interface commands are
	 * issued! This was confirmed by manually running gdb for a test elf file, and
	 * manually issuing the '-symbol-list-lines' requests. A bug report has been
	 * submitted here:
	 * https://sourceware.org/bugzilla/show_bug.cgi?id=26735 */
	QHash<QString /* gdb reported full file name */, SourceFileData> sourceFiles;

	QList<struct SourceCodeLocation> bookmarks;

	bool navigatorModeActivated = false;

private:
	/* SVD register view dialogs related data. */
	struct SvdRegisterViewData
	{
		uint32_t address;
		QDialog * dialog;
		QGroupBox * fieldsGroupBox;
		SvdRegisterViewData(QDialog * dialog = 0, uint32_t address = -1) : address(address), dialog(dialog) {}
		struct RegField { int bitoffset, bitwidth; QSpinBox * spinbox;
				RegField(int bitoffset, int bitwidth, QSpinBox * spinbox) :
					bitoffset(bitoffset), bitwidth(bitwidth), spinbox(spinbox) {}};
		QList<RegField> fields;
	};
	QVector<SvdRegisterViewData> svdViews;

private:
	struct
	{
		QTimer timer;
		const QString flashStyleSheets[2] = { "background-color: lightblue; color: black;", "background-color: white; color: black;", };
		const QString defaultStyleSheet = "";
		const int flashIntervalMs = 70;
		const int flashRepeatCount = 4;
		QDockWidget * highlightedWidget = 0;
		int flashCount;
	}
	widgetFlashHighlighterData;
private slots:
	void updateHighlightedWidget(void);
	void on_pushButtonSendScratchpadToGdb_clicked();

private:
	QTimer controlKeyPressTimer;
	const int controlKeyPressLockTimeMs = 400;
	QTime controlKeyPressTime;
	void displayHelpMenu(void);
	void flashHighlightDockWidget(QDockWidget * w);
	QList<QAction *> highlightWidgetActions;

	/* Enumeration constants used as a 'role' parameter in the treeview widget used for displaying
	 * svd device information. */
	enum
	{
		SVD_REGISTER_POINTER = Qt::UserRole,
		SVD_REGISTER_ADDRESS,
	};
	SvdFileParser svdParser;
	BlackMagicProbe blackMagicProbe;
	const QString vimEditorLocation = "C:\\Program Files (x86)\\Vim\\vim80\\gvim.exe";
	Ui::MainWindow *ui;
	std::shared_ptr<QProcess> gdbProcess;
	////GdbServer	* gdbserver;
	PassThroughGdbserver	* gdbserver;
	TargetCorefile	* targetCorefile;
	QString normalizeGdbString(const QString & miString);
	/*! \todo This is no longer needed. */
	QThread gdbMiReceiverThread;
	QThread fileSearchThread;
	QThread targetAttachThread;
	StringFinder * stringFinder;
	BlackmagicTargetAttacher * blackmagicTargetAttacher;
	GdbMiReceiver			* gdbMiReceiver;
	QStringList targetRegisterNames;

	QModelIndex locateVarObject(const QString & miName, const QModelIndex & root, const QModelIndex &parent);

	/* This map holds information about where children of gdb varobjects should be displayed in the user interface.
	 *
	 * Currently, gdb varobjects are handled as nodes in a tree. For a simple gdb varobject, that has no children,
	 * this is a single root node - with no children, and such objects cannot be 'expanded' in the user interface
	 * (because they have no children).
	 *
	 * Compound gdb varobjects are ones, that have children, such as C 'struct' and array data types.
	 * Such compound gdb varobjects can be expanded in the user interface, to show their contents, but these
	 * compound gdb varobjects are not expanded by default (gdb does not expand them for the sake of efficiency).
	 * To view the contents of such compound gdb varobjects, the debugger user explicitly requests expanding the
	 * compound gdb varobjects through the user interface. Such requests from the debugger user are translated to
	 * gdb machine interface commands that request fetching the gdb varobject's child.
	 * However, multiple such requests - for expanding different gdb varobject children - may have been issued to gdb
	 * at a time, before a response from gdb to any of these responses has been received.
	 * When a response from gdb is received, it is necessary to know, for each such response, where, in the
	 * user interface, to display the contents of the response.
	 *
	 * This map is devised for this purpose - to be able to determine, for a specific gdb varobject
	 * child fetch request, where in the user interface to show the result of that request, when a response to
	 * the request has been received.
	 *
	 * The basic workflow for expanding gdb varobjects is this:
	 * - The debugger user requests, through the user interface, that a gdb varobject child is expanded, so that
	 * the child's contents are shown.
	 * - The debugger frontend captures this request from the debugger user, and translates it to an *annotated*
	 * gdb machine interface command request for fetching the gdb varobject child's contents. Here, *annotated*
	 * means that the gdb machine interface command for fetching the gdb varobject child's contents is sent to gdb
	 * along with an *annotation* token, which is simply a non-negative number. By design, the gdb machine interface
	 * protocol, guarantees, that the response to a specific gdb machine interface command request  - will contain
	 * the very same, if any, annotation token that was present in the specific gdb machine interface command request.
	 * These tokens (numbers) have absolutely no meaning to gdb, and are meant to be used as discriminators by gdb
	 * machine interface consumers, such as gdb frontends.
	 * - The annotation token (i.e., number) is used to set up this map, and also the 'gdbTokenContext' data
	 * structure, so that a response from gdb is received, it can be determined where in the user interface
	 * to show the requested gdb varobject child's contents.
	 *
	 * This is all there is about it. It may not seem too straightforward, but there is nothing special altogether.
	 * There are details that need to be worked out when communicating with gdb over the gdb machine interface,
	 * the approach here is probably not the most sane one to accomplish all of this, but I hope it is not too
	 * irritating. */
	std::unordered_map<unsigned /* gdb token number */, QModelIndex /* where to place the gdb varobject child fetch response */>
		varobjectParentModelIndexes;
	GdbVarObjectTreeItemModel varObjectTreeItemModel;

	/* Functions for handling different response packets from gdb. */
	/* Handle the response to the "-var-create - @ \"<expression>\"" machine interface gdb command. */
	bool handleNameResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-var-list-children --all-values <varobject>" machine interface gdb command. */
	bool handleNumchildResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-file-list-exec-source-files" machine interface gdb command. */
	bool handleFilesResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-symbol-list-lines <filename>" machine interface gdb command. */
	bool handleLinesResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-symbol-info-functions", "-symbol-info-variables", and "-symbol-info-types" machine interface gdb commands. */
	bool handleSymbolsResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-file-exec-and-symbols" machine interface gdb command. */
	bool handleFileExecAndSymbolsResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-break-list" machine interface gdb command. */
	bool handleBreakpointTableResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-stack-list-frames" machine interface gdb command. */
	bool handleStackResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-data-list-register-names" machine interface gdb command. */
	bool handleRegisterNamesResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-data-list-register-values" machine interface gdb command. */
	bool handleRegisterValuesResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-var-update" machine interface gdb command. */
	bool handleChangelistResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-stack-list-variables" machine interface gdb command. */
	bool handleVariablesResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-stack-info-frame" machine interface gdb command. */
	bool handleFrameResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle the response to the "-data-disassemble" machine interface gdb command. */
	bool handleDisassemblyResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);

	/* Sequence point handling. Also see comments about 'sequence points' for 'enum GDB_RESPONSE_ENUM' */
	bool handleSequencePoints(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle various responses from gdb that may lead to updating the breakpoint information. */
	bool handleBreakpointUpdateResponses(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);

protected:
	void closeEvent(QCloseEvent *event) override;
	bool event(QEvent *event);
	bool eventFilter(QObject *watched, QEvent *event) override;
signals:
	void attachToFirstTarget(void);
	void readyReadGdbProcess(const QByteArray data);
	void findString(const QString & str, unsigned flags);
	void targetRunning(void);
	void gdbServerConnected(void);
	void targetStopped(void);
	void targetCallStackFrameChanged(void);
	void addFilesToSearchSet(const QStringList sourceCodeFiles);
};

#endif // MAINWINDOW_HXX
