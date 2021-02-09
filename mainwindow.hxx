/*
 * Copyright (C) 2020-2021 Stoyan Shopov <stoyan.shopov@gmail.com>
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
#include <QInputDialog>
#include <QSettings>

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

#include <elfio/elfio.hpp>

#include "gdbmireceiver.hxx"
#include "gdb-mi-parser.hxx"
#include "target-corefile.hxx"
#include "gdbserver.hxx"
#include "gdb-remote.hxx"

#include "svdfileparser.hxx"

#include <functional>

#include "bmpdetect.hxx"
#include "ui_settings-dialog.h"

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
/* It seems that prior to Qt version 5.14, Qt does not provide a specialization
 * for 'std::hash<QString>'. If this is the case, provide a specialization for
 * Qt versions prior to 5.14. */
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
#endif

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


class BlackMagicProbeServer : public QObject
{
	Q_OBJECT
private:
	QSerialPort	bmport;
	QTcpServer	gdb_tcpserver;
	QTcpSocket	* gdb_client_socket = 0;

	static const int DEFAULT_GDB_SERVER_PORT	= 1122;

	void shutdown(void)
	{
		qDebug() << "shutting down gdb server, and blackmagic probe connections";
		bmport.blockSignals(true);

		if (bmport.isOpen())
		{
			qDebug() << "data available when closing the blackmagic port:" << bmport.readAll();
			bmport.setDataTerminalReady(false);
			bmport.close();
		}

		if (gdb_client_socket)
		{
			gdb_client_socket->blockSignals(true);
			if (gdb_client_socket->isValid() && gdb_client_socket->isOpen())
			{
				qDebug() << "data available when closing the gdb server port:" << gdb_client_socket->readAll();
				gdb_client_socket->close();
			}
			/* Do not explicitly delete the socket. It is not explicitly stated in the Qt documentation,
			 * and I have not checked the Qt sources, but it seems that calling 'QTcpServer::close()'
			 * also deletes the connected sockets. Trying to delete the socket here is crashing the program. */
			//delete gdb_client_socket;
			gdb_client_socket = 0;
			emit GdbClientDisconnected();
		}

		gdb_tcpserver.close();

		bmport.blockSignals(false);
		emit BlackMagicProbeDisconnected();
	}
signals:
	void BlackMagicProbeConnected(void);
	void GdbClientDisconnected(void);
	void BlackMagicProbeDisconnected(void);
private slots:
	void probeErrorOccurred(QSerialPort::SerialPortError error)
	{
		if (error != QSerialPort::NoError)
		{
			QMessageBox::critical(0, "Blackmagic serial port error", QString("Blackmagic serial port error:\n%1").arg(bmport.errorString()));
			qDebug() << "blackmagic probe error occurred, error code:" << error;
			shutdown();
		}
	}
	void gdbSocketErrorOccurred(QAbstractSocket::SocketError error)
	{
		qDebug() << "gdb server socket error occurred, error code:" << error;
		shutdown();
	}

	void gdbSocketReadyRead(void)
	{
		if (!bmport.isOpen())
			qDebug() << "WARNING: gdb data received, but no blackmagic probe connected; discarding gdb data:" << gdb_client_socket->readAll();
		else
			bmport.write(gdb_client_socket->readAll());
	}

	void bmportReadyRead(void)
	{
		if (!gdb_client_socket || !gdb_client_socket->isValid() || !gdb_client_socket->isOpen())
			qDebug() << "WARNING: blackmagic probe data received, but no gdb client connected; discarding blackmagic data:" << bmport.readAll();
		else
			gdb_client_socket->write(bmport.readAll());
	}

	void newGdbConnection(void)
	{
		qDebug() << "gdb client connected";
		gdb_client_socket = gdb_tcpserver.nextPendingConnection();
		connect(gdb_client_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(gdbSocketErrorOccurred(QAbstractSocket::SocketError)));
		connect(gdb_client_socket, SIGNAL(readyRead()), this, SLOT(gdbSocketReadyRead()));
		connect(gdb_client_socket, &QTcpSocket::disconnected, [&] { shutdown(); });
	}
public:
	BlackMagicProbeServer(void)
	{
		connect(& bmport, SIGNAL(errorOccurred(QSerialPort::SerialPortError)), this, SLOT(probeErrorOccurred(QSerialPort::SerialPortError)));
		connect(& bmport, SIGNAL(readyRead()), this, SLOT(bmportReadyRead()));
		connect(& gdb_tcpserver, SIGNAL(newConnection()), this, SLOT(newGdbConnection()));
	}
	~BlackMagicProbeServer(void)
	{
		disconnect();
		shutdown();
	}
	void connectToProbe(void)
	{
		if (bmport.isOpen())
			shutdown();

		std::vector<BmpProbeData> probes;
		findConnectedProbes(probes);
		if (!probes.size())
			return;
		QString portName;
		if (probes.size() > 1)
		{
			QStringList probeDescriptions;
			for (const auto & p : probes)
				probeDescriptions << p.description + " \\\\Serial# " + p.serialNumber + " \\\\Port# " + p.portName;
			bool ok;
			QString probe = QInputDialog::getItem(0, "Select probe to connect to",
							      "Multiple blackmagic probes detected.\n"
							      "Select the blackmagic probe to connect to",
							      probeDescriptions, 0, false, &ok);
			if (!ok)
				return;
			QRegularExpression rx("\\\\Port# (.*)");
			QRegularExpressionMatch match = rx.match(probe);
			if (match.hasMatch())
				portName = match.captured(1);
			else
				return;
		}
		else
			portName = probes.at(0).portName;

		bmport.setPortName(portName);
		if (bmport.open(QSerialPort::ReadWrite))
		{
			if (!gdb_tcpserver.listen(QHostAddress::Any, DEFAULT_GDB_SERVER_PORT))
			{
				QMessageBox::critical(0, "Cannot listen on gdbserver port", "Error opening a gdbserver port for listening for incoming gdb connections");
				shutdown();
				return;
			}
			bmport.setDataTerminalReady(false);
			bmport.setDataTerminalReady(true);
			emit BlackMagicProbeConnected();
		}
	}
	void sendRawGdbPacket(const QByteArray & packet)
	{
		if (bmport.isOpen())
			bmport.write(packet);
		else
			qDebug() << "WARNING: tried to send data to the blackmagic probe, and no probe is connected; data:" << packet;
	}
	void closeConnections(void)
	{
		shutdown();
	}
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
/* The 'Editable Tree Model' Qt example was very useful when making this customized
 * item model class. */
	Q_OBJECT
private:
	/* Dummy root node. */
	/*! \todo	THIS CURRENTLY LEAKS MEMORY */
	GdbVarObjectTreeItem root;
	/*! \todo	This is very evil... */
	QSet<const QString> highlightedVarObjectNames;

	void markIndexAsChanged(const QModelIndex & nodeIndex, const GdbVarObjectTreeItem * item)
	{
		if (!nodeIndex.isValid())
			return;
		int row = item->row();
		highlightedVarObjectNames.insert(item->miName);
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
	void clearHighlightedVarObjectNames(void) { highlightedVarObjectNames.clear(); }
	void dumpTree()
	{
		root.dump();
	}

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
		if (role == Qt::ForegroundRole && highlightedVarObjectNames.contains(node->miName))
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
			emit readGdbVarObjectChildren(t->miName);
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
	QModelIndex indexForMiVariableName(const QString & miName)
	{
		std::function<QModelIndex(const QModelIndex & root)> scan = [&](const QModelIndex & root) -> QModelIndex
		{
			GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(root.internalPointer());
			if (t->miName == miName)
				return root;
			int i;
			QModelIndex x;
			while ((x = index(i ++, 0, root)).isValid())
				if ((x = scan(x)).isValid())
					break;
			return x;
		};

		int i;
		QModelIndex x, invalid = QModelIndex();
		while ((x = index(i ++, 0, invalid)).isValid())
			if ((x = scan(x)).isValid())
				break;
		return x;
	}
signals:
	void readGdbVarObjectChildren(const QString varObjectName);
};

class MainWindow : public QMainWindow
{
	Q_OBJECT

private:
	/* The minimum string length that needs to be entered so that the object locator performs a search for the text entered. */
	const int MIN_STRING_LENGTH_FOR_OBJECT_LOCATOR = 3;
	const int MAX_LINE_LENGTH_IN_GDB_LOG_LIMITING_MODE = 1 * 1024;
	const int MAX_GDB_LINE_COUNT_IN_GDB_LIMITING_MODE = 1 * 1024;

	/* This is the number of maximum kept sessions, saved in the frontend settings file. */
	const int MAX_KEPT_SESSIONS	= 10;

	/* Settings-related data. */
	const QString SETTINGS_FILE_NAME					= "turbo.rc";

	const QString SETTINGS_MAINWINDOW_STATE					= "mainwindows-state";
	const QString SETTINGS_MAINWINDOW_GEOMETRY				= "mainwindows-geometry";
	const QString SETTINGS_SPLITTER_VERTICAL_SOURCE_VIEW_STATE		= "splitter-vertical-source-view-state";
	const QString SETTINGS_SPLITTER_HORIZONTAL_SOURCE_VIEW_STATE		= "splitter-horizontal-source-view-state";
	const QString SETTINGS_SPLITTER_HORIZONTAL_GDB_CONSOLES_STATE		= "splitter-horizontal-gdb-consoles-state";
	const QString SETTINGS_IS_SPLITTER_HORIZONTAL_GDB_CONSOLES_VISIBLE	= "is-splitter-horizontal-gdb-consoles-visible";
	const QString SETTINGS_IS_DISASSEMBLY_VIEW_VISIBLE			= "is-disassembly-view-visible";
	const QString SETTINGS_IS_TARGET_OUTPUT_VIEW_VISIBLE			= "is-target-output-view-visible";

	const QString SETTINGS_CHECKBOX_GDB_OUTPUT_LIMITING_MODE_STATE		= "checkbox-gdb-output-limiting-mode-state";
	const QString SETTINGS_CHECKBOX_SHOW_FULL_FILE_NAME_STATE		= "checkbox-show-full-file-name-state";
	const QString SETTINGS_CHECKBOX_SHOW_ONLY_SOURCES_WITH_MACHINE_CODE_STATE		= "checkbox-show-only-sources-with-machine-code-state";
	const QString SETTINGS_CHECKBOX_SHOW_ONLY_EXISTING_SOURCE_FILES		= "checkbox-show-only-existing-source-files";
	const QString SETTINGS_CHECKBOX_ENABLE_NATIVE_DEBUGGING_STATE		= "checkbox-enable-native-debugging";

	const QString SETTINGS_SCRATCHPAD_TEXT_CONTENTS				= "scratchpad-text-contents";

	const QString SETTINGS_TRACE_LOG					= "trace-log";
	const QString SETTINGS_CHECKBOX_SHOW_FULL_FILE_NAME_IN_TRACE_LOG_STATE	= "checkbox-show-full-file-name-in-trace-log-state";

	const QString SETTINGS_LAST_LOADED_EXECUTABLE_FILE			= "last-loaded-executable-file";
	const QString SETTINGS_GDB_EXECUTABLE_FILENAME				= "gdb-executable-filename";

	const QString SETTINGS_SAVED_SESSIONS					= "saved-sessions";

	std::shared_ptr<QSettings> settings;

	struct SessionState
	{
		QString		executableFileName;
		QStringList	breakpoints;
		QStringList	bookmarks;
		static SessionState fromQVariant(const QVariant & v)
		{
			struct SessionState s;
			QList<QVariant> l = v.toList();
			if (l.size() > 0)
				s.executableFileName = l.at(0).toString();
			if (l.size() > 1)
				s.breakpoints = l.at(1).toStringList();
			if (l.size() > 2)
				s.bookmarks = l.at(2).toStringList();
			return s;
		}
		QVariant toVariant(void) const
		{
			QList<QVariant> v;
			v << executableFileName;
			v << breakpoints;
			v << bookmarks;
			return v;
		}
		const bool operator ==(const struct SessionState & rhs) const { return executableFileName == rhs.executableFileName; }
	};
	QList<struct SessionState> sessions;
	/* This flag is used to know if a session has been restored, and only later save this session if it has been previously restored.
	 * Otherwise, sessions may get wiped out. */
	bool isSessionRestored = false;
	void loadSessions(void);
	void restoreSession(const QString & executableFileName);
	void saveSessions(void);

	const QString internalHelpFileName = ":/resources/init.txt";

	Ui::DialogSettings settingsUi;
	QDialog * dialogEditSettings;

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow();

public slots:
	void gdbMiLineAvailable(QString line);
private slots:
	void displayHelp(void);
	void gdbProcessError(QProcess::ProcessError error);
	void gdbProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void sendDataToGdbProcess(const QString &data);
	void readGdbVarObjectChildren(const QString varObjectName);
	void showSourceCode(const QTreeWidgetItem * item);
	void breakpointsContextMenuRequested(QPoint p);
	void svdContextMenuRequested(QPoint p);
	void bookmarksContextMenuRequested(QPoint p);
	void breakpointViewItemChanged(QTreeWidgetItem * item, int column);
	void stringSearchReady(const QString pattern, QSharedPointer<QVector<StringFinder::SearchResult>> results, bool resultsTruncated);
	void createSvdRegisterView(QTreeWidgetItem *item, int column);

	void updateSourceListView(void);
	void updateSymbolViews(void);
	void updateBreakpointsView(void);
	void updateBookmarksView(void);

	void on_lineEditVarObjectExpression_returnPressed();


	void on_lineEditSearchFilesForText_returnPressed();

	void on_lineEditObjectLocator_textChanged(const QString &arg1);

	void on_pushButtonDeleteAllBookmarks_clicked();

	void on_pushButtonDisconnectGdbServer_clicked();

	void on_lineEditFindText_returnPressed();

	void requestTargetHalt(void);

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
		int index = -1;
	public:
		bool canNavigateBack(void) const { return index > 0; }
		bool canNavigateForward(void) const { return index < locations.size() - 1; }
		const SourceCodeLocation & previous(void) { return locations.at(-- index); }
		const SourceCodeLocation & following(void) { return locations.at(++ index); }
		const SourceCodeLocation & current(void) { return locations.at(index); }
		void dump(void) const {
			qDebug() << "navigation stack dump, index" << index << "size" << locations.size();
			for (const auto & l : locations)
				qDebug() << l.fullFileName << l.lineNumber;
			qDebug() << "------------- navigation stack dump end";
		}
		void push(const struct SourceCodeLocation & location)
		{
			locations.erase(locations.begin() + index + 1, locations.end());
			index = locations.size() - 1;
			if (locations.size() == 0 || locations.at(index) != location)
			{
				locations.push_back(location);
				index ++;
			}
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
				/* Response for target monitor scan commands, i.e. 'monitor swdp_scan' and
				 * 'monitor jtag_scan' commands. This is a somewhat special case, because there is
				 * no machine interface command corresponding to the 'monitor' command. */
				GDB_RESPONSE_TARGET_SCAN_COMPLETE,

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
				/* When receiving this response code, update the breakpoint information by
				 * issuing a "-break-list" gdb machine interface command.
				 * This may be needed when issuing breakpoint commands, such as
				 * "-break-delete", "-break-enable", "-break-disable", etc., because
				 * gdb answers to such commands with a "^done" response packet,
				 * with no other details available. This is also used for
				 * updating the breakpoint list after receiving 'notify-async-output'
				 * records for breakpoint changes from gdb - these are sent to the
				 * frontend when breakpoints are modified directly by the user, through
				 * breakpoint commands. The 'notify-async-output' records in such cases
				 * are '=breakpoint-created,bkpt={...}', '=breakpoint-modified,bkpt={...}',
				 * '=breakpoint-deleted,id=number'. */
				GDB_REQUEST_BREAKPOINT_LIST_UPDATE,
				/* This response code is expected after the target non-volatile memory
				 * contents have been retrieved, and a verification of the target memory
				 * contents against the ELF file should be performed. */
				GDB_SEQUENCE_POINT_CHECK_MEMORY_CONTENTS,
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
		TARGET_DETACHED,
	}
	target_state = GDBSERVER_DISCONNECTED;

	/* This structure captures target output data, e.g., the target responses
	 * for 'monitor swdp_scan' and 'monitor jtag_scan' commands. */
	struct
	{
	private:
		QStringList capturedDataLines;
		bool isCapturing = false;
	public:
		const QStringList & capturedLines(void) { return capturedDataLines; }
		void startCapture(void) { isCapturing = true; capturedDataLines.clear(); }
		void stopCapture(void) { isCapturing = false; }
		void captureLine(const QString & dataLine) { if (isCapturing) capturedDataLines << dataLine; }
	}
	targetDataCapture;

	struct
	{
		QWidgetList enabledWidgetsWhenGdbServerDisconnected;
		QWidgetList disabledWidgetsWhenGdbServerDisconnected;
		QWidgetList enabledWidgetsWhenTargetRunning;
		QWidgetList disabledWidgetsWhenTargetRunning;
		QWidgetList enabledWidgetsWhenTargetStopped;
		QWidgetList disabledWidgetsWhenTargetStopped;
		QWidgetList enabledWidgetsWhenTargetDetached;
		QWidgetList disabledWidgetsWhenTargetDetached;

		void enterTargetState(enum TARGET_STATE target_state)
		{
			qDebug() << "Entering new target state:" << target_state;
			switch (target_state)
			{
			default:
				*(int*)0=0;
			case TARGET_DETACHED:
				for (const auto & w : disabledWidgetsWhenTargetDetached)
					w->setEnabled(false);
				for (const auto & w : enabledWidgetsWhenTargetDetached)
					w->setEnabled(true);
				return;
			case GDBSERVER_DISCONNECTED:
				for (const auto & w : disabledWidgetsWhenGdbServerDisconnected)
					w->setEnabled(false);
				for (const auto & w : enabledWidgetsWhenGdbServerDisconnected)
					w->setEnabled(true);
				return;
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

	std::shared_ptr<ELFIO::elfio> elfReader;
	/* This list contains the set of temporary file names used when verifying target memory area
	 * contents.
	 *
	 * The file names are constructed, one for each program segment in the ELF file that is being
	 * used for debugging. These file names are used in requests to gdb, to read the target memory
	 * areas corresponding to the program segments. When the target memory areas are read, the files
	 * are checked against the contents of the program segments from the ELF file. */
	QStringList targetMemorySectionsTempFileNames;

	QFileSystemWatcher sourceFileWatcher;
	QString displayedSourceCodeFile;
	void highlightBreakpointedLines(void);
	void highlightBookmarks(void);
	void refreshSourceCodeView(void);
	void displaySourceCodeFile(const SourceCodeLocation & sourceCodeLocation, bool saveCurrentLocationToNavigationStack = true, bool saveNewLocationToNavigationStack = false);
	void parseGdbCreateVarObjectResponse(const QString & variableExpression, const QString response, struct GdbVarObjectTreeItem & node);
	void navigateToSymbolAtCursor(void);
	QString escapeString(const QString & s) { QString t = s; return t.replace('\\', "\\\\").replace('\"', "\\\""); }
	void sourceItemContextMenuRequested(const QTreeWidget * treeWidget, QPoint p);

	/* Returns true, if the source view was refreshed, false otherwise. */
	void searchCurrentSourceText(const QString &pattern);
	void moveCursorToNextMatch(void);
	void moveCursorToPreviousMatch(void);

	void sendCommandsToGdb(QLineEdit * lineEdit);

	struct SourceFileData {
		/* Enumeration constants used as a 'role' parameter in the treeview widget used for displaying
		 * the list of source code files. */
		enum
		{
			FILE_NAME = Qt::UserRole,
			LINE_NUMBER,

			/* Item type values in the object locator view. Used for creating
			 * custom context menus depending on the item type. */
			/*! \todo	It may be simpler to add here all 'source location'-related objects,
			 * i.e., bookmarks, breakpoints, stack frames, etc., and handle their context menus depending on
			 * the item types. */
			/* The values for this role are from the SymbolData:SymbolKind enumeration. */
			ITEM_KIND,
			/* The data contents are a 'void' pointer that can be cast to a 'GdbBreakpointData' data structure. */
			BREAKPOINT_DATA_POINTER,
			/* Generic, 'void' pointer, used for referencing some data structures from a tree widget
			 * item. The interpretation of this pointer depends on context. It is expected that,
			 * depending on context, this pointer will be cast to the actual type that it points
			 * to, before use. */
			/*! \todo	This was once usedm but is currently unused. Maybe remove it altogether. */
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
			size_t operator ()(const MainWindow::SourceFileData::SymbolData & t) const
			{
				return std::hash<std::string>()(QString("%1:%2:%3:%4").arg(t.name).arg(t.type).arg(t.description).arg(t.line).toStdString());
			}
		};

		/*! \todo	Make the 'subprograms' and 'variables' fields below unordered_set-s as well. */
		std::unordered_set<struct SymbolData, SymbolHash> subprograms;
		std::unordered_set<struct SymbolData, SymbolHash> variables;
		std::unordered_set<struct SymbolData, SymbolHash> dataTypes;
	};

	QTreeWidgetItem * createNavigationWidgetItem(const QStringList & columnTexts, const QString fullFileName = QString(), int lineNumber = -1,
						     enum SourceFileData::SymbolData::SymbolKind itemKind = SourceFileData::SymbolData::INVALID);
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

	void on_pushButtonScanForTargets_clicked();

	void on_pushButtonConnectGdbToGdbServer_clicked();

	void compareTargetMemory();

	void gdbStarted(void);

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
	BlackMagicProbeServer blackMagicProbeServer;
	const QString vimEditorLocation = "C:\\Program Files (x86)\\Vim\\vim80\\gvim.exe";
	Ui::MainWindow *ui;
	std::shared_ptr<QProcess> gdbProcess;
	/* This is the process identifier of the debugged process, needed for sending signals
	 * for interrupting the process. This is appropriate only when debugging local processes,
	 * it is invalid for remote debugging. */
	unsigned debugProcessId = -1;
	////GdbServer	* gdbserver;
	TargetCorefile	* targetCorefile;
	QString normalizeGdbString(const QString & miString);
	/*! \todo This is no longer needed. */
	QThread gdbMiReceiverThread;
	QThread fileSearchThread;
	StringFinder * stringFinder;
	GdbMiReceiver			* gdbMiReceiver;
	QStringList targetRegisterNames;

	/* Attempts to build an error strings out of a gdb result response. */
	QString gdbErrorString(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results)
	{
		QString errorMessage;
		if (parseResult == GdbMiParser::ERROR)
			for (const auto & r : results)
			{
				if (r.variable == "msg")
					errorMessage += QString("Gdb message: %1\n").arg(QString::fromStdString(r.value->asConstant()->constant()));
				if (r.variable == "code")
					errorMessage += QString("Gdb error code: %1\n").arg(QString::fromStdString(r.value->asConstant()->constant()));
			}
		else
			errorMessage = "No error";
		return errorMessage;
	}
	void appendLineToGdbLog(const QString & data);

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
	/*! \todo	Handle sequence points in separate functions. Rework and rename this function. */
	bool handleSequencePoints(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle sequence point response for vrtifying target memory area contents. */
	bool handleVerifyTargetMemoryContentsSeqPoint(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle various responses from gdb that may lead to updating the breakpoint information. */
	bool handleBreakpointUpdateResponses(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);
	/* Handle target scan ('monitor swdp_scan' and 'monitor jtag_scan') response. */
	bool handleTargetScanResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber);

protected:
	void closeEvent(QCloseEvent *event) override;
	bool event(QEvent *event);
	bool eventFilter(QObject *watched, QEvent *event) override;
signals:
	void readyReadGdbProcess(const QByteArray data);
	void findString(const QString & str, unsigned flags);
	void targetCallStackFrameChanged(void);
	void addFilesToSearchSet(const QStringList sourceCodeFiles);

signals:
	/* Gdb and target state change signals. */
	void gdbServerConnected(void);
	void targetStopped(void);
	void targetRunning(void);
	void targetDetached(void);
};

#endif // MAINWINDOW_HXX
