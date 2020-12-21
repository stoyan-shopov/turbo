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

/* !!! TODO: do mainwindow layout saving/selection; that is similar to rts games.
 * It should be possible to be able to quickly save and restore mainwindow layout,
 * the mechanism of which should allow for keystrokes; for example,
 *'ALT + n' for saving layout, and 'CTRL + n' for restoring. */
#include "mainwindow.hxx"
#include "ui_mainwindow.h"

#include <QSettings>
#include <QFileDialog>
#include <QPainter>

#include <set>

#include "cscanner.hxx"

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	QSettings s("qgbd.rc", QSettings::IniFormat);
	restoreState(s.value("mainwindow-state", QByteArray()).toByteArray());
	restoreGeometry(s.value("mainwindow-geometry", QByteArray()).toByteArray());
	ui->splitterVerticalSourceView->restoreState(s.value("splitter-vertical-source-view-state", QByteArray()).toByteArray());
	ui->splitterHorizontalSourceDisassemblyView->restoreState(s.value("splitter-horizontal-source-disassembly-view-state", QByteArray()).toByteArray());
	ui->splitterHorizontalGdbConsoles->restoreState(s.value("splitter-horizontal-gdb-consoles-state", QByteArray()).toByteArray());

	gdbProcess = std::make_shared<QProcess>();
	/*! \todo This doesn't need to live in a separate thread. */
	gdbMiReceiver = new GdbMiReceiver();
	gdbMiReceiver->moveToThread(&gdbMiReceiverThread);

	//connect(gdbProcess.get(), SIGNAL(readyReadStandardOutput()), gdbMiReceiver, SLOT(gdbInputAvailable()));
	connect(gdbProcess.get(), & QProcess::readyReadStandardOutput, [&] { emit readyReadGdbProcess(gdbProcess->readAll()); });
	connect(this, SIGNAL(readyReadGdbProcess(QByteArray)), gdbMiReceiver, SLOT(gdbInputAvailable(QByteArray)));

	connect(ui->pushButtonNavigateBack, &QPushButton::clicked, [&] { navigateBack(); });

	connect(gdbProcess.get(), SIGNAL(readyReadStandardError()), this, SLOT(gdbStandardErrorDataAvailable()));
	connect(gdbProcess.get(), SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(gdbProcessFinished(int,QProcess::ExitStatus)));
	connect(gdbMiReceiver, SIGNAL(gdbMiOutputLineAvailable(QString)), this, SLOT(gdbMiLineAvailable(QString)));
	connect(gdbProcess.get(), SIGNAL(errorOccurred(QProcess::ProcessError)), this, SLOT(gdbProcessError(QProcess::ProcessError)));
	gdbMiReceiverThread.start();

	connect(&varObjectTreeItemModel, SIGNAL(readGdbVarObjectChildren(const QModelIndex)), this, SLOT(readGdbVarObjectChildren(QModelIndex)));
	ui->treeViewDataObjects->setModel(&varObjectTreeItemModel);

	ui->treeViewDataObjects->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeViewDataObjects->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeViewDataObjects->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	ui->treeViewDataObjects->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

	ui->treeWidgetSourceFiles->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetSourceFiles->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	//ui->treeWidgetSourceFiles->header()->setSectionResizeMode(1,1);

	ui->treeWidgetStackVariables->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetStackVariables->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeWidgetStackVariables->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

	ui->treeWidgetTraceLog->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetTraceLog->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeWidgetTraceLog->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

	ui->treeWidgetBookmarks->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetBookmarks->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

	ui->treeWidgetSearchResults->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetSearchResults->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeWidgetSearchResults->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

	ui->treeWidgetSubprograms->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetSubprograms->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeWidgetSubprograms->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	ui->treeWidgetSubprograms->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

	ui->treeWidgetStaticDataObjects->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetStaticDataObjects->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeWidgetStaticDataObjects->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	ui->treeWidgetStaticDataObjects->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

	ui->treeWidgetDataTypes->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetDataTypes->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeWidgetDataTypes->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

	ui->treeWidgetBacktrace->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetBacktrace->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeWidgetBacktrace->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	ui->treeWidgetBacktrace->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	ui->treeWidgetBacktrace->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

	ui->treeWidgetSvd->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeWidgetSvd->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeWidgetSvd->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

	ui->treeWidgetBreakpoints->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

	connect(ui->checkBoxShowFullFileNames, & QCheckBox::stateChanged, [=](int newState) { ui->treeWidgetSourceFiles->setColumnHidden(1, newState == 0); });
	ui->checkBoxShowFullFileNames->setChecked(s.value("checkbox-show-full-file-name-state", false).toBool());

	connect(ui->checkBoxShowOnlySourcesWithMachineCode, SIGNAL(stateChanged(int)), this, SLOT(updateSourceListView()));
	ui->checkBoxShowOnlySourcesWithMachineCode->setChecked(s.value("checkbox-show-only-sources-with-machine-code", false).toBool());


	connect(ui->treeWidgetBookmarks, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetBookmarks, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(ui->treeWidgetObjectLocator, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetObjectLocator, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(ui->treeWidgetSourceFiles, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetSourceFiles, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(ui->treeWidgetSearchResults, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetSearchResults, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(ui->treeWidgetSubprograms, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetSubprograms, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(ui->treeWidgetStaticDataObjects, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetStaticDataObjects, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(ui->treeWidgetDataTypes, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetDataTypes, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(ui->treeWidgetBacktrace, & QTreeWidget::itemSelectionChanged, [=] ()
		{	if (target_state != TARGET_STOPPED)
				return;
			QList<QTreeWidgetItem *> selectedItems = ui->treeWidgetBacktrace->selectedItems();
			if (selectedItems.size())
			{
				sendDataToGdbProcess(QString("-stack-select-frame %1\n").arg(ui->treeWidgetBacktrace->indexOfTopLevelItem(selectedItems.at(0))));
				sendDataToGdbProcess("-stack-info-frame\n");
			}
		} );

	connect(ui->treeWidgetBreakpoints, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ if (column != GdbBreakpointData::TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER) showSourceCode(item); } );
	connect(ui->treeWidgetBreakpoints, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ if (column != GdbBreakpointData::TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER) showSourceCode(item); } );

	QFontDatabase::addApplicationFont(":/fonts/resources/Hack-Regular.ttf");
	setStyleSheet(QString(

/*! \todo !!! This is reported as a workaround to activate hover events for splitter handles !!!
 * https://bugreports.qt.io/browse/QTBUG-13768 */

"QSplitterHandle:hover {}\n"

"QSplitter::handle:horizontal {\n"
	"width: 6px;\n"
	"background: grey;\n"
"}\n"

"QSplitter::handle:horizontal:hover {\n"
	"background: cyan;\n"
"}\n"

"QSplitter::handle:vertical {\n"
	"height: 6px;\n"
	"background: orange;\n"
"}\n"

"QSplitter::handle:vertical:hover {\n"
	"background: cyan;\n"
"}\n"

/*
"QPlainTextEdit {\n"
	"font: 10pt 'Lucida Sans Typewriter';\n"
"}\n"
*/
"QPlainTextEdit {\n"
	"font: 10pt 'Hack';\n"
"}\n"
			      ));

	gdbProcess->start("arm-none-eabi-gdb.exe", QStringList() << "--interpreter=mi3");

	ui->plainTextEditScratchpad->setPlainText(s.value("scratchpad-text-contents", QString("Lorem ipsum dolor sit amet")).toString());

	ui->plainTextEditSourceView->installEventFilter(this);
	ui->plainTextEditSourceView->viewport()->installEventFilter(this);
	QFileInfo f(s.value("last-loaded-executable-file", QString()).toString());
	if (f.exists())
	{
		int choice = QMessageBox::question(0, "Reopen last executable for debugging",
				      QString("Last opened executable file for debugging is:\n")
				      + f.canonicalFilePath() + "\n\n"
				      "Do you want to reopen it, or load a new file?",
				      "Reopen last file", "Select new file", "Cancel");
		if (choice == 0)
			/* Reopen last file. */
			goto reopen_last_file;
		else if (choice == 1)
			/* Select new file. */
			goto select_new_file;
		/* Otherwise, do not load anything. */
	}
	else
	{
select_new_file:
		f = QFileInfo(getExecutableFilename());
		if (!f.canonicalFilePath().isEmpty())
		{
reopen_last_file:
			unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
									   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_EXECUTABLE_SYMBOL_FILE_LOADED,
									   f.canonicalFilePath())
								   );
			sendDataToGdbProcess("-gdb-set mem inaccessible-by-default off\n");
			sendDataToGdbProcess("-gdb-set print elements unlimited\n");
			sendDataToGdbProcess(QString("%1-file-exec-and-symbols \"%2\"\n").arg(t).arg(f.canonicalFilePath()));
		}
	}

	ui->treeWidgetBreakpoints->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetBreakpoints, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(breakpointsContextMenuRequested(QPoint)));

	ui->treeWidgetBookmarks->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetBookmarks, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(bookmarksContextMenuRequested(QPoint)));

	ui->treeWidgetSvd->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetSvd, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(svdContextMenuRequested(QPoint)));

	/* Use this for handling changes to the breakpoint enable/disable checkbox modifications. */
	connect(ui->treeWidgetBreakpoints, SIGNAL(itemChanged(QTreeWidgetItem*,int)), this, SLOT(breakpointViewItemChanged(QTreeWidgetItem*,int)));

	sourceCodeViewHighlightFormats.navigatedLine.setProperty(QTextFormat::FullWidthSelection, true);
	sourceCodeViewHighlightFormats.navigatedLine.setBackground(QBrush(Qt::gray));
	sourceCodeViewHighlightFormats.enabledBreakpoint.setProperty(QTextFormat::FullWidthSelection, true);
	sourceCodeViewHighlightFormats.enabledBreakpoint.setBackground(QBrush(Qt::red));
	sourceCodeViewHighlightFormats.disabledBreakpoint.setProperty(QTextFormat::FullWidthSelection, true);
	sourceCodeViewHighlightFormats.disabledBreakpoint.setBackground(QBrush(Qt::darkRed));
	sourceCodeViewHighlightFormats.currentLine.setProperty(QTextFormat::FullWidthSelection, true);
	sourceCodeViewHighlightFormats.currentLine.setBackground(QBrush(Qt::lightGray));
	sourceCodeViewHighlightFormats.bookmark.setProperty(QTextFormat::FullWidthSelection, true);
	sourceCodeViewHighlightFormats.bookmark.setBackground(QBrush(Qt::darkCyan));
	sourceCodeViewHighlightFormats.searchedText.setBackground(QBrush(Qt::yellow));

	connect(ui->plainTextEditSourceView, &QPlainTextEdit::cursorPositionChanged, [=]()
		{
			QTextCursor c(ui->plainTextEditSourceView->textCursor());
			c.movePosition(QTextCursor::StartOfBlock);
			c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
			sourceCodeViewHighlights.currentSourceCodeLine.clear();
			QTextEdit::ExtraSelection s;
			s.cursor = c;
			s.format = sourceCodeViewHighlightFormats.currentLine;
			sourceCodeViewHighlights.currentSourceCodeLine << s;
			refreshSourceCodeView();
		});

	QString targetFilesBaseDirectory =
			"C:/src/build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug/troll-test-drive-files/";
	targetCorefile = new TargetCorefile(targetFilesBaseDirectory + "flash.bin", 0x08000000,
					    targetFilesBaseDirectory + "ram.bin", 0x20000000,
					    targetFilesBaseDirectory + "registers.bin");
	////gdbserver = new GdbServer(targetCorefile);
	gdbserver = new PassThroughGdbserver(& blackMagicProbe);

	/* Load bookmarks. */
	QStringList bookmarkStrings = s.value("bookmarks", QStringList()).toStringList();
	for (const auto & bookmark : bookmarkStrings)
	{
		QStringList bookmarkData = bookmark.split('\n');
		if (bookmarkData.size() != 2)
			continue;
		bookmarks << SourceCodeLocation(bookmarkData.at(0), bookmarkData.at(1).toInt());
	}
	updateBookmarksView();

	QStringList traceLog = s.value("trace-log", QStringList()).toStringList();
	for (const auto & l : traceLog)
	{
		QStringList t = l.split('|');
		QString x;
		if (t.count() != 3)
			continue;
		int lineNumber;
		bool ok;
		lineNumber = t.at(1).toUInt(& ok);
		if (!ok)
			lineNumber = -1;
		QTreeWidgetItem * w = new QTreeWidgetItem(t);
		w->setData(0, SourceFileData::FILE_NAME, t.at(2));
		w->setData(0, SourceFileData::LINE_NUMBER, lineNumber);
		ui->treeWidgetTraceLog->addTopLevelItem(w);
	}

	connect(ui->checkBoxShowFullFileNamesInTraceLog, & QCheckBox::stateChanged, [&](int newState) { ui->treeWidgetTraceLog->setColumnHidden(2, newState == 0); });
	ui->checkBoxShowFullFileNamesInTraceLog->setChecked(s.value("checkbox-show-full-file-name-state-in-trace-log", false).toBool());

	connect(ui->treeWidgetTraceLog, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetTraceLog, & QTreeWidget::currentItemChanged, [=] (QTreeWidgetItem * current, QTreeWidgetItem * previous)
		{ showSourceCode(current); } );
	connect(ui->treeWidgetTraceLog, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(& blackMagicProbe, & BlackMagicProbe::BlackMagicProbeConnected,
		[&] {
			ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: lawngreen");
			ui->pushButtonConnectToBlackmagic->setText(tr("Blackmagic connected"));
			ui->groupBoxBlackMagicDisconnectedWidgets->setEnabled(false);
			ui->groupBoxBlackMagicConnectedWidgets->setEnabled(true);
			});
	connect(& blackMagicProbe, & BlackMagicProbe::BlackMagicProbeDisconnected,
		[&] {
			ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: yellow");
			ui->pushButtonConnectToBlackmagic->setText(tr("Connect to blackmagic"));
			ui->groupBoxBlackMagicDisconnectedWidgets->setEnabled(true);
			ui->groupBoxBlackMagicConnectedWidgets->setEnabled(false);
			});
	ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: yellow");

	//connect(ui->pushButtonConnectToFirstTarget, & QPushButton::clicked, [&] { blackMagicProbe.connectToFirstTarget(); });
	connect(ui->pushButtonConnectToFirstTarget, & QPushButton::clicked, [&] { emit attachToFirstTarget(); });
	connect(ui->pushButtonDisplayHelp, SIGNAL(clicked(bool)), this, SLOT(displayHelp()));
	displayHelp();

	targetStateDependentWidgets.enabledWidgetsWhenGdbServerDisconnected << ui->groupBoxTargetDisconnected;
	targetStateDependentWidgets.disabledWidgetsWhenGdbServerDisconnected << ui->groupBoxTargetConnected;
	connect(this, &MainWindow::gdbServerConnected, [&] {
		if (isGdbServerConnectedToGdb)
			*(int*)0=0;
		isGdbServerConnectedToGdb = true;
		QMessageBox::information(0, "Gdb connection established", "Gdb successfully connected to remote gdb server");
	});
	targetStateDependentWidgets.enabledWidgetsWhenTargetStopped << ui->groupBoxTargetHalted << ui->groupBoxTargetConnected;
	targetStateDependentWidgets.disabledWidgetsWhenTargetStopped << ui->groupBoxTargetRunning;
	connect(this, &MainWindow::targetStopped, [&] {
		targetStateDependentWidgets.enterTargetState(target_state = TARGET_STOPPED);
		/*! \todo Make the frame limits configurable. */
		sendDataToGdbProcess("-stack-list-frames 0 100\n");
		if (!targetRegisterNames.size())
			sendDataToGdbProcess("-data-list-register-names\n");
		sendDataToGdbProcess("-stack-info-frame\n");
	});

	connect(this, &MainWindow::targetCallStackFrameChanged, [&] {
		sendDataToGdbProcess("-data-list-register-values x\n");
		sendDataToGdbProcess("-var-update --all-values *\n");
		sendDataToGdbProcess("-stack-list-variables --all-values\n");
	} );


	targetStateDependentWidgets.enabledWidgetsWhenTargetRunning << ui->groupBoxTargetRunning;
	targetStateDependentWidgets.disabledWidgetsWhenTargetRunning << ui->groupBoxTargetHalted;
	connect(this, &MainWindow::targetRunning, [&] {
		targetStateDependentWidgets.enterTargetState(target_state = TARGET_RUNNING);
	});

	targetStateDependentWidgets.enterTargetState(target_state = GDBSERVER_DISCONNECTED);

	connect(ui->pushButtonConnectGdbToGdbServer, SIGNAL(clicked(bool)), this, SLOT(connectGdbToGdbServer()));

	connect(ui->pushButtonResetAndRunTarget, & QPushButton::clicked, [&] { sendDataToGdbProcess("-exec-run\n"); });
	connect(ui->pushButtonContinue, & QPushButton::clicked, [&] { sendDataToGdbProcess("-exec-continue\n"); });

	stringFinder = new StringFinder();
	stringFinder->moveToThread(&fileSearchThread);
	connect(this, SIGNAL(findString(QString,uint)), stringFinder, SLOT(findString(QString,uint)));
	connect(this, SIGNAL(addFilesToSearchSet(QStringList)), stringFinder, SLOT(addFilesToSearchSet(QStringList)));

	widgetFlashHighlighterData.timer.setSingleShot(false);
	connect(& widgetFlashHighlighterData.timer, SIGNAL(timeout()), this, SLOT(updateHighlightedWidget()));

	connect(&controlKeyPressTimer, &QTimer::timeout, [&] (void) -> void {
		controlKeyPressTimer.stop();
		if (QApplication::queryKeyboardModifiers() & Qt::ControlModifier)
			displayHelpMenu();
	});

	auto makeHighlightAction = [&, this] (const QString & actionText, const QString & shortcut, QDockWidget * w) -> void
	{
		QAction * act;
		act = new QAction(actionText, this);
		act->setShortcut(shortcut);
		connect(act, &QAction::triggered, [=] { flashHighlightDockWidget(w); });
		highlightWidgetActions << act;
	};
	makeHighlightAction("Backtrace", "Ctrl+t", ui->dockWidgetBacktrace);
	makeHighlightAction("Bookmarks", "Ctrl+b", ui->dockWidgetBookmarks);
	makeHighlightAction("Breakpoints", "Ctrl+r", ui->dockWidgetBreakpoints);
	makeHighlightAction("Data objects", "Ctrl+d", ui->dockWidgetDataObjects);
	makeHighlightAction("Search results", "Ctrl+s", ui->dockWidgetSearchResults);
	makeHighlightAction("Source files", "Ctrl+l", ui->dockWidgetSourceFiles);
	makeHighlightAction("Subprograms", "Ctrl+u", ui->dockWidgetSubprograms);
	for (const auto & a : highlightWidgetActions)
		addAction(a);

	controlKeyPressTime.start();

	connect(& sourceFileWatcher, & QFileSystemWatcher::fileChanged, [=](const QString& path)
	{
		/* There is this situation with vim. Apparently, when saving files, vim first saves data
		 * to a temporary file, then it deletes the original file, and renames the temporary file
		 * to the name of the original file.
		 * If a check if the file still exists is made here, it is too often the case that the file does
		 * not exist at this time. Adding a small delay before checking if the file exists is not a very
		 * nice solution, but works satisfactorily in practice. */
		usleep(20000);
		QFileInfo fi(path);
		if (!fi.exists())
			QMessageBox::warning(0, "File has disappeared", QString("This file has disappeared, it may have been renamed or removed:\n%1").arg(path));
		else
		{
			int choice = QMessageBox::question(0, "File has been changed", QString("File %1 has been modified. Do you want to reload it?").arg(path),
							   "Reload file", "Cancel");
			if (choice == 0)
			{
				/* Reload file. */
				SourceCodeLocation l(displayedSourceCodeFile, ui->plainTextEditSourceView->textCursor().blockNumber());
				displaySourceCodeFile(l, false);
			}
		}
	}
	);

	connect(ui->treeWidgetSvd, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(createSvdRegisterView(QTreeWidgetItem*,int)));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (navigatorModeActivated)
	{
		QWidget::closeEvent(event);
		/*! \todo	This should not be needed, remove it after checking. */
		return;
	}
	QSettings s("qgbd.rc", QSettings::IniFormat);
	s.setValue("mainwindow-geometry", saveGeometry());
	s.setValue("mainwindow-state", saveState());
	s.setValue("checkbox-show-full-file-name-state", ui->checkBoxShowFullFileNames->isChecked());
	s.setValue("checkbox-show-full-file-name-state-in-trace-log", ui->checkBoxShowFullFileNamesInTraceLog->isChecked());
	s.setValue("checkbox-show-only-sources-with-machine-code", ui->checkBoxShowOnlySourcesWithMachineCode->isChecked());
	s.setValue("scratchpad-text-contents", ui->plainTextEditScratchpad->document()->toPlainText());

	/* Save bookmarks. */
	QStringList bookmarkStrings;
	for (const auto & bookmark : bookmarks)
		bookmarkStrings << QString("%1\n%2").arg(bookmark.fullFileName).arg(bookmark.lineNumber);
	s.setValue("bookmarks", bookmarkStrings);

	s.setValue("splitter-vertical-source-view-state", ui->splitterVerticalSourceView->saveState());
	s.setValue("splitter-horizontal-source-disassembly-view-state", ui->splitterHorizontalSourceDisassemblyView->saveState());
	s.setValue("splitter-horizontal-gdb-consoles-state", ui->splitterHorizontalGdbConsoles->saveState());

	QStringList traceLog;
	for (int i = 0; i < ui->treeWidgetTraceLog->topLevelItemCount(); i ++)
	{
		const QTreeWidgetItem * l = ui->treeWidgetTraceLog->topLevelItem(i);
		traceLog << l->text(0) + '|' + l->text(1) + '|' + l->text(2);
	}
	s.setValue("trace-log", traceLog);
	QWidget::closeEvent(event);
}

bool MainWindow::event(QEvent *event)
{
	if (event->type() == QEvent::KeyRelease)
	{
		QKeyEvent * e = static_cast<QKeyEvent *>(event);
		if (e->key() == Qt::Key_Control)
		{
			controlKeyPressTimer.stop();
		}
	}
	else if (event->type() == QEvent::KeyPress)
	{
		QKeyEvent * e = static_cast<QKeyEvent *>(event);
		if (e->key() == Qt::Key_Control)
		{
			if (controlKeyPressTime.restart() < controlKeyPressLockTimeMs)
			{
				controlKeyPressTimer.stop();
				displayHelpMenu();
			}
			else if (!controlKeyPressTimer.isActive())
				controlKeyPressTimer.start(2500);
		}
	}
	return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == ui->plainTextEditSourceView->viewport() && event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
		if (mouseEvent->button() == Qt::LeftButton)
		{
			if( mouseEvent->modifiers() & Qt::ControlModifier)
				navigateToSymbolAtCursor();
			if( mouseEvent->modifiers() & Qt::AltModifier)
			{
				QTextCursor c = ui->plainTextEditSourceView->textCursor();
				c.select(QTextCursor::WordUnderCursor);
				QString s = c.selectedText();
				if (!s.isEmpty())
					emit findString(s, ui->checkBoxSearchForWholeWordsOnly->isChecked() ? StringFinder::SEARCH_FOR_WHOLE_WORDS_ONLY : 0);
			}
		}
		return false;
	}

	bool result = false;
	if (watched == ui->plainTextEditSourceView && event->type() == QEvent::KeyPress)
	{
		QKeyEvent * e = static_cast<QKeyEvent *>(event);
		switch (e->key())
		{
		case Qt::Key_S:
			if (target_state == TARGET_STOPPED)
				sendDataToGdbProcess("s\n");
			result = true;
			break;
		case Qt::Key_Left:
			if (e->modifiers() & Qt::AltModifier)
			{
				navigateBack();
				result = true;
			}
			break;
		case Qt::Key_F2:
			if (displayedSourceCodeFile.isEmpty())
				break;
		{
			struct SourceCodeLocation bookmark(displayedSourceCodeFile, ui->plainTextEditSourceView->textCursor().blockNumber() + 1);
			QList<struct SourceCodeLocation>::iterator b = bookmarks.begin();
			while (b != bookmarks.end())
			{
				if (* b == bookmark)
					break;
				++ b;
			}
			if (b == bookmarks.end())
				/* Bookmark not found - add new. */
				bookmarks << bookmark;
			else
				/* Bookmark found - remove it. */
				bookmarks.erase(b);
			updateBookmarksView();
			refreshSourceCodeView();
			result = true;
		}
			break;
		case Qt::Key_V:
			if (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
			{
				QString sourceFilename = displayedSourceCodeFile;
				if (!QFileInfo(sourceFilename).exists())
					sourceFilename = Utils::filenameToWindowsFilename(sourceFilename);
				QProcess::startDetached(vimEditorLocation,
							QStringList() << sourceFilename << QString("+%1").arg(ui->plainTextEditSourceView->textCursor().blockNumber() + 1),
							sourceFilename.isEmpty() ? QApplication::applicationDirPath() : QFileInfo(sourceFilename).canonicalPath());
				result = true;
			}

			break;
		case Qt::Key_F3:
		case Qt::Key_N:
			result = true;
			if (e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
			{
				/*! \todo	Display a proper messagebox here, explaining the 'navigator' mode. */
				navigatorModeActivated = true;
				ui->dockWidgetBacktrace->hide();
				ui->dockWidgetBreakpoints->hide();
				ui->dockWidgetSubprograms->hide();
				ui->dockWidgetDataObjects->hide();
				ui->dockWidgetStaticDataObjects->hide();
				ui->dockWidgetScratchpad->hide();
				ui->dockWidgetBlackmagicToolbox->hide();
				ui->dockWidgetElfToolbox->hide();
			}
			else if (e->modifiers() == Qt::ShiftModifier)
				moveCursorToPreviousMatch();
			else
				moveCursorToNextMatch();
			break;
		case Qt::Key_F:
			if (e->modifiers() != Qt::ControlModifier)
				break;
		case Qt::Key_Slash:
			result = true;
			ui->lineEditFindText->clear();
			ui->lineEditFindText->setFocus();
			break;
		case Qt::Key_Asterisk:
		{
			QTextCursor c = ui->plainTextEditSourceView->textCursor();
			c.select(QTextCursor::WordUnderCursor);
			QString pattern = c.selectedText();
			searchCurrentSourceText(pattern);
			moveCursorToNextMatch();
			ui->lineEditSearchFilesForText->setText(pattern);
			result = true;
		}
			break;
		case Qt::Key_O:
			if (target_state == TARGET_STOPPED)
				sendDataToGdbProcess("n\n");
			result = true;
			break;
		case Qt::Key_C:
			if (target_state == TARGET_STOPPED)
				sendDataToGdbProcess("c\n");
			else if (e->modifiers() == Qt::ControlModifier)
				blackMagicProbe.sendRawGdbPacket("\x3");
			result = true;
			break;
		case Qt::Key_Space:
#if 0
		{
	QPixmap * pixmap = new QPixmap(ui->treeWidgetSubprograms->viewport()->size());
	QPainter painter(pixmap);
	painter.fillRect(0, 0, pixmap->width(), pixmap->height(), QBrush(Qt::yellow));
	painter.drawLine(0, 0, pixmap->width(), pixmap->height());
	painter.drawText(pixmap->rect(), Qt::AlignCenter, "Subprograms");
	QPalette p(Qt::yellow);
	//p.setBrush(QPalette::Window, QBrush(Qt::cyan));
	p.setBrush(QPalette::Window, QBrush(* pixmap));
	//ui->treeWidgetSubprograms->setPalette(p);
	//ui->treeWidgetSubprograms->setBackgroundRole(QPalette::Window);
	ui->treeWidgetSubprograms->viewport()->setPalette(p);
	ui->treeWidgetSubprograms->viewport()->setBackgroundRole(QPalette::Window);
	ui->treeWidgetSubprograms->viewport()->setAutoFillBackground(true);
		}
#endif
			/* Toggle breakpoint at current source code line number, if a source code line number is active.
			 * If there is no breakpoint on the current source code line - insert a breakpoint.
			 * If there are breakpoints on the current source code line - remove them. */
			if (!displayedSourceCodeFile.isEmpty())
			{
				int lineNumber = ui->plainTextEditSourceView->textCursor().blockNumber() + 1;
				std::vector<const GdbBreakpointData *> b;
				GdbBreakpointData::breakpointsForSourceCodeLineNumber(SourceCodeLocation(displayedSourceCodeFile, lineNumber), breakpoints, b);
				if (b.empty())
					/* Breakpoint not found at current source code line - insert one. */
					sendDataToGdbProcess(QString("-break-insert --source \"%1\" --line %2\n")
							     .arg(escapeString(displayedSourceCodeFile))
							     .arg(lineNumber));
				else
				{
					/* Breakpoint(s) found at current source code line - remove them. */
					for (const auto & t : b)
						sendDataToGdbProcess(QString("-break-delete %1\n").arg(t->gdbReportedNumberString));
					/* Insert a synchronization request for updating the breakpoint list. */
					unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
											   GdbTokenContext::GdbResponseContext::GDB_REQUEST_BREAKPOINT_LIST_UPDATE));
					sendDataToGdbProcess(QString("%1\n").arg(t));
				}
			}
			result = true;
			break;
		default:
			return false;
		}
		return result;
	}
	return false;
}

MainWindow::~MainWindow()
{
	gdbMiReceiverThread.quit();
	gdbProcess->disconnect();
	gdbProcess->kill();
	gdbMiReceiverThread.wait();
	gdbProcess->waitForFinished();

	fileSearchThread.quit();
	fileSearchThread.wait();

	targetAttachThread.quit();
	targetAttachThread.wait();

	delete ui;
}

void MainWindow::gdbMiLineAvailable(QString line)
{
	if (!line.length())
		return;
	/* Process token number prefix, if present. Negative numbers are not possible, because a minus
	 * sign ('-') denotes the start of a machine interface command. */
	unsigned tokenNumber = 0;
	if (line.at(0).isDigit())
	{
		unsigned lineIndex = 0;
		do tokenNumber *= 10, tokenNumber += line.at(lineIndex ++).toLatin1() - '0'; while (line.at(lineIndex).isDigit());
		line = line.right(line.length() - lineIndex);
	}
	switch(line.at(0).toLatin1())
	{
		case '~':
			ui->plainTextEditConsoleStreamOutput->appendPlainText(normalizeGdbString(line.right(line.length() - 1)));
			break;
		case '&':
			ui->plainTextEditLogStreamOutput->appendPlainText(normalizeGdbString(line.right(line.length() - 1)));
			break;
		case '*':
		case '^':
			ui->plainTextEditConsoleStreamOutput->appendPlainText((tokenNumber ? QString("%1").arg(tokenNumber) : QString()) + line);
			{
				std::vector<GdbMiParser::MIResult> results;
				GdbMiParser parser;
				enum GdbMiParser::RESULT_CLASS_ENUM result = parser.parse(line.toStdString(), results);
				if (handleFilesResponse(result, results, tokenNumber))
					break;
				if (handleLinesResponse(result, results, tokenNumber))
					break;
				if (handleNameResponse(result, results, tokenNumber))
					break;
				if (handleNumchildResponse(result, results, tokenNumber))
					break;
				if (handleFileExecAndSymbolsResponse(result, results, tokenNumber))
					break;
				if (handleSequencePoints(result, results, tokenNumber))
					break;
				if (handleBreakpointUpdateResponses(result, results, tokenNumber))
					break;
				if (handleSymbolsResponse(result, results, tokenNumber))
					break;
				if (handleBreakpointTableResponse(result, results, tokenNumber))
					break;
				if (handleStackResponse(result, results, tokenNumber))
					break;
				if (handleRegisterNamesResponse(result, results, tokenNumber))
					break;
				if (handleRegisterValuesResponse(result, results, tokenNumber))
					break;
				if (handleChangelistResponse(result, results, tokenNumber))
					break;
				if (handleVariablesResponse(result, results, tokenNumber))
					break;
				if (handleFrameResponse(result, results, tokenNumber))
					break;
				if (handleDisassemblyResponse(result, results, tokenNumber))
					break;
				switch (result)
				{
					case GdbMiParser::DONE:
						break;
					case GdbMiParser::ERROR:
				{
					/* \todo	Remove the 'gdbTokenContext' for the current token number, if not already removed above. */
					QString errorMessage;
					for (const auto & r : results)
					{
						if (r.variable == "msg")
							errorMessage += QString("Gdb message: %1\n").arg(QString::fromStdString(r.value->asConstant()->constant()));
						if (r.variable == "code")
							errorMessage += QString("Gdb error code: %1\n").arg(QString::fromStdString(r.value->asConstant()->constant()));
					}
					QMessageBox::critical(0, "Gdb error", QString("Gdb error:\n%1").arg(errorMessage));
				}
					break;
				case GdbMiParser::CONNECTED:
					emit gdbServerConnected();
					break;
				case GdbMiParser::RUNNING:
					emit targetRunning();
					break;
				case GdbMiParser::STOPPED:
					emit targetStopped();
					break;
				default:
					*(int*)0=0;
				}
			}
			break;
		default:
			ui->plainTextEditConsoleStreamOutput->appendPlainText(line);
		break;
	}
}

QString MainWindow::normalizeGdbString(const QString &miString)
{
QString s = miString;
	/* Strip any enclosing double quotes. */
	if (s.startsWith('"'))
		s.remove(0, 1);
	if (s.endsWith('"'))
		s.truncate(s.length() - 1);
	/* Unescape. */
	s.replace("\\n", "\n");
	s.replace("\\\"", "\"");
	/* Strip any (single) trailing newline. */
	while (s.endsWith('\n'))
		s.truncate(s.length() - 1);
	return s;
}

QModelIndex MainWindow::locateVarObject(const QString &miName, const QModelIndex & root, const QModelIndex & parent)
{
	if (!root.isValid())
		return QModelIndex();
	GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem*>(root.internalPointer());
	if (t->miName == miName)
		return root;
	int i = 0;
	while (true)
	{
		QModelIndex index = varObjectTreeItemModel.index(i ++, 0, parent);
		if (!index.isValid())
			return QModelIndex();
		if ((index = locateVarObject(miName, index, root)).isValid())
			return index;
	}
}

bool MainWindow::handleNameResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	if (!gdbTokenContext.hasContextForToken(tokenNumber)
		|| gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_NAME)
		return false;
	struct GdbTokenContext::GdbResponseContext context = gdbTokenContext.readAndRemoveContext(tokenNumber);
	struct GdbVarObjectTreeItem * node = new GdbVarObjectTreeItem;

	node->name = context.s;
	int childCount = 0;
	for (const auto & t : results)
	{
		if (t.variable == "name")
			node->miName = QString::fromStdString(t.value->asConstant()->constant());
		else if (t.variable == "value")
			node->value = QString::fromStdString(t.value->asConstant()->constant());
		else if (t.variable == "type")
			node->type = QString::fromStdString(t.value->asConstant()->constant());
		else if (t.variable == "numchild")
			childCount = QString::fromStdString(t.value->asConstant()->constant()).toInt(0, 0);
	}
	node->setReportedChildCount(childCount);
	static_cast<GdbVarObjectTreeItemModel *>(context.p)->appendRootItem(node);
	return true;
}

bool MainWindow::handleNumchildResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	if (!gdbTokenContext.hasContextForToken(tokenNumber)
		|| gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_NUMCHILD)
		return false;
	std::vector<GdbVarObjectTreeItem *> children;
	for (const auto & t : results)
	{
		if (t.variable == "children" && t.value->asList() && t.value->asList()->results.size())
		{
			for (const auto & child : t.value->asList()->results)
			{
				const GdbMiParser::MITuple * t;
				if (!(t = child.value->asTuple()))
					continue;
				GdbVarObjectTreeItem * node = new GdbVarObjectTreeItem;
				int childCount = 0;
				for (const auto & t : child.value->asTuple()->map)
				{
					if (t.first == "name")
						node->miName = t.second->asConstant()->constant().c_str();
					else if (t.first == "numchild")
						childCount = QString::fromStdString(t.second->asConstant()->constant()).toInt(0, 0);
					else if (t.first == "value")
						node->value = t.second->asConstant()->constant().c_str();
					else if (t.first == "type")
						node->type = t.second->asConstant()->constant().c_str();
					else if (t.first == "exp")
						node->name = t.second->asConstant()->constant().c_str();
				}
				node->setReportedChildCount(childCount);
				children.push_back(node);
			}
		}
	}
	gdbTokenContext.readAndRemoveContext(tokenNumber);
	QModelIndex m = varobjectParentModelIndexes.operator[](tokenNumber);
	varobjectParentModelIndexes.erase(tokenNumber);
	varObjectTreeItemModel.childrenFetched(m, children);
	return false;
}

bool MainWindow::handleFilesResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> & results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	if (results.size() != 1 || results.at(0).variable != "files" || !results.at(0).value->asList())
		return false;
	sourceFiles.clear();
	for (const auto & t : results.at(0).value->asList()->values)
	{
		if (!t->asTuple())
			*(int*)0=0;
		SourceFileData s;
		for (const auto & v : t->asTuple()->map)
		{
			if (v.first == "file")
			{
				s.gdbReportedFileName = v.second->asConstant()->constant().c_str();
				s.fileName = QFileInfo(s.gdbReportedFileName).fileName();
			}
			else if (v.first == "fullname")
				s.fullFileName = v.second->asConstant()->constant().c_str();
		}
		sourceFiles.operator [](s.fullFileName) = s;
	}
	updateSourceListView();

	/* Retrieve source line addresses for all source code files reported. */
	for (const auto & f : sourceFiles)
	{
		unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
								   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_LINES,
								   f.fullFileName)
							   );
		sendDataToGdbProcess(QString("%1-symbol-list-lines \"%2\"\n").arg(t).arg(escapeString(f.fullFileName)));
	}
	unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
				   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_FUNCTION_SYMBOLS));
	sendDataToGdbProcess(QString("%1-symbol-info-functions\n").arg(t));
	t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
				   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_VARIABLE_SYMBOLS));
	sendDataToGdbProcess(QString("%1-symbol-info-variables\n").arg(t));
	t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
				   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_TYPE_SYMBOLS));
	sendDataToGdbProcess(QString("%1-symbol-info-types\n").arg(t));

	t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
				   GdbTokenContext::GdbResponseContext::GDB_SEQUENCE_POINT_SOURCE_CODE_ADDRESSES_RETRIEVED));
	/* This is a bit of a hack - send an empty packet, containing just a token number prefix.
	 * In this case, an empty response, containing just this token number prefix, will be received
	 * only after all of the "-symbol-list-lines" requests issued above have completed */
	sendDataToGdbProcess(QString("%1\n").arg(t));
	/* Now that the list of source code files that are used to build the executable is known,
	 * deploy a string searching thread. */
	QStringList sourceCodeFilenames;
	for (const auto & f : sourceFiles)
		sourceCodeFilenames << f.fullFileName;
	//qRegisterMetaType<QSharedPointer<QVector<StringFinder::SearchResult>>>("StringSearchResultType");

	emit addFilesToSearchSet(sourceCodeFilenames);
	qRegisterMetaType<QSharedPointer<QVector<StringFinder::SearchResult>>>();
	connect(stringFinder, SIGNAL(searchReady(QString,QSharedPointer<QVector<StringFinder::SearchResult> >,bool)), this, SLOT(stringSearchReady(QString,QSharedPointer<QVector<StringFinder::SearchResult> >,bool)));
	fileSearchThread.start();

	blackmagicTargetAttacher = new BlackmagicTargetAttacher();
	blackmagicTargetAttacher->moveToThread(&targetAttachThread);
	connect(this, SIGNAL(attachToFirstTarget()), blackmagicTargetAttacher, SLOT(attachToFirstTarget()));
	connect(blackmagicTargetAttacher, SIGNAL(targetCommunicationError(QString)), this, SLOT(targetAttacherErrorReceived(QString)));
	connect(blackmagicTargetAttacher, SIGNAL(targetAttached()), this, SLOT(targetAttacherSuccess()));
	targetAttachThread.start();

	return true;
}

bool MainWindow::handleLinesResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	if (!gdbTokenContext.hasContextForToken(tokenNumber)
		|| gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_LINES)
		return false;
	if (results.size() != 1 || results.at(0).variable != "lines" || !results.at(0).value->asList())
		return false;
	struct GdbTokenContext::GdbResponseContext context = gdbTokenContext.readAndRemoveContext(tokenNumber);
	if (!sourceFiles.count(context.s))
		return false;

	SourceFileData & sourceFile(sourceFiles[context.s]);
	for (const auto & t : results.at(0).value->asList()->values)
	{
		if (!t->asTuple())
			*(int*)0=0;
		int lineNumber = -1;
		for (const auto & v : t->asTuple()->map)
		{
			if (v.first == "line")
				lineNumber = QString::fromStdString(v.second->asConstant()->constant().c_str()).toULong(0, 0);
		}
		sourceFile.machineCodeLineNumbers.insert(lineNumber);
	}
	sourceFile.isSourceLinesFetched = true;
	return true;
}

bool MainWindow::handleSymbolsResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	if (!gdbTokenContext.hasContextForToken(tokenNumber))
		return false;
	const GdbTokenContext::GdbResponseContext & c = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (c.gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_FUNCTION_SYMBOLS
			&& c.gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_VARIABLE_SYMBOLS
			&& c.gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_TYPE_SYMBOLS)
		return false;
	GdbTokenContext::GdbResponseContext context = gdbTokenContext.readAndRemoveContext(tokenNumber);
	const GdbMiParser::MITuple * t;
	if (results.size() != 1 || results.at(0).variable != "symbols" || !(t = results.at(0).value->asTuple()))
		return false;
	for (const auto & x : t->map)
	{
		const GdbMiParser::MIList * sources;
		if (x.first == "debug" && (sources = x.second->asList()))
		{
			for (const auto & s : sources->values)
			{
				const GdbMiParser::MITuple * t = s->asTuple();
				if (!t)
					continue;

				QString fullFileName, gdbReportedFileName;
				std::vector<SourceFileData::SymbolData> symbols;
				const GdbMiParser::MIList * symbolList;
				for (const auto & x : t->map)
					if (x.first == "fullname")
						fullFileName = QString::fromStdString(x.second->asConstant()->constant());
					else if (x.first == "filename")
						gdbReportedFileName = QString::fromStdString(x.second->asConstant()->constant());
					else if (x.first == "symbols" && (symbolList = x.second->asList()))
					{
						const GdbMiParser::MITuple * misymbol;
						for (const auto & s : symbolList->values)
						{
							SourceFileData::SymbolData symbol;

							if (!(misymbol = s->asTuple()))
								continue;
							for (const auto & s : misymbol->map)
								if (s.first == "line")
									symbol.line = QString::fromStdString(s.second->asConstant()->constant()).toInt();
								else if (s.first == "name")
									symbol.name = QString::fromStdString(s.second->asConstant()->constant());
								else if (s.first == "type")
									symbol.type = QString::fromStdString(s.second->asConstant()->constant());
								else if (s.first == "description")
									symbol.description = QString::fromStdString(s.second->asConstant()->constant());
							if (symbol.line != -1)
								/* The source code line number should not normally be set for some symbols,
								 * for example base types. Discard such symbols, as they would most probably
								 * not be informative. */
								symbols.push_back(symbol);
						}
					}
				if (!sourceFiles.count(fullFileName))
				{
					/* Symbols found for file which was not reported by gdb in the list of source code files
					 * by the response of the "-file-list-exec-source-files" machine interface command.
					 * This is possible when gdb replies to a "-symbol-info-types" machine interface command,
					 * and the reported filename in the response was not previously present in the reply of
					 * the "-file-list-exec-source-files" command.
					 * So, create a new file entry here. */
					SourceFileData s;
					s.fileName = QFileInfo(gdbReportedFileName).fileName();
					s.gdbReportedFileName = gdbReportedFileName;
					s.fullFileName = fullFileName;
					/* Force the "SourceFileData" to true so that the file does not appear when only files
					 * with machine code are being shown. */
					s.isSourceLinesFetched = true;
					sourceFiles.operator[](fullFileName) = s;
				}
				if (context.gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_FUNCTION_SYMBOLS)
					sourceFiles.operator [](fullFileName).subprograms.insert(sourceFiles.operator [](fullFileName).subprograms.end(), symbols.begin(), symbols.end());
				else if (context.gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_VARIABLE_SYMBOLS)
					sourceFiles.operator [](fullFileName).variables.insert(sourceFiles.operator [](fullFileName).variables.end(), symbols.begin(), symbols.end());
				else
					//sourceFiles.operator [](fullFileName).dataTypes.insert(sourceFiles.operator [](fullFileName).dataTypes.end(), symbols.begin(), symbols.end());
					for (const auto & symbol : symbols)
						sourceFiles.operator [](fullFileName).dataTypes.insert(symbol);
			}
		}
	}

	/* Update the list of source code files that are searched. */
	QStringList sourceCodeFilenames;
	for (const auto & f : sourceFiles)
		sourceCodeFilenames << f.fullFileName;

	emit addFilesToSearchSet(sourceCodeFilenames);
	return true;
}

bool MainWindow::handleFileExecAndSymbolsResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (!gdbTokenContext.hasContextForToken(tokenNumber)
		|| gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_EXECUTABLE_SYMBOL_FILE_LOADED)
		return false;
	struct GdbTokenContext::GdbResponseContext context = gdbTokenContext.readAndRemoveContext(tokenNumber);
	if (parseResult == GdbMiParser::ERROR)
	{
		QString errorMessage = QString("Failed to load executable file in gdb, could not load file:\n%1").arg(context.s);
		if (results.size() && results.at(0).variable == "msg" && results.at(0).value->asConstant())
			errorMessage += QString("\n\n%1").arg(QString::fromStdString(results.at(0).value->asConstant()->constant()));
		QMessageBox::critical(0, "Error loading executable file in gdb",
				      errorMessage);
		return true;
	}

	loadedExecutableFileName = context.s;
	QSettings s("qgbd.rc", QSettings::IniFormat);
	s.setValue("last-loaded-executable-file", loadedExecutableFileName);
	sendDataToGdbProcess("-file-list-exec-source-files\n");
	return true;
}

bool MainWindow::handleBreakpointTableResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MITuple * t;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "BreakpointTable" || !(t = results.at(0).value->asTuple()))
		return false;
	for (const auto & x : t->map)
	{
		/* Skip the breakpoint table header here, and only handle the breakpoint list. */
		const GdbMiParser::MIList * breakpointList;
		if (x.first != "body" || !(breakpointList = x.second->asList()))
			continue;
		breakpoints.clear();
		for (const auto & breakpoint : breakpointList->results)
		{
			const GdbMiParser::MITuple * b;
			if (breakpoint.variable != "bkpt" || !(b = breakpoint.value->asTuple()))
				return false;
			GdbBreakpointData breakpointDetails;
			for (const auto & x : b->map)
			{
				if (x.first == "number")
					breakpointDetails.gdbReportedNumberString = QString::fromStdString(x.second->asConstant()->constant());
				else if (x.first == "type")
					breakpointDetails.type = QString::fromStdString(x.second->asConstant()->constant());
				else if (x.first == "disp")
					breakpointDetails.disposition = QString::fromStdString(x.second->asConstant()->constant());
				else if (x.first == "enabled")
					breakpointDetails.enabled = ((QString::fromStdString(x.second->asConstant()->constant()) == "y") ? true : false);
				else if (x.first == "addr")
				{
					bool ok;
					breakpointDetails.address = QString::fromStdString(x.second->asConstant()->constant()).toULong(& ok, 0);
					if (!ok)
						/* This can be the case for multiple-location source code breakpoints (i.e., cases,
						 * where more than one machine code locations correspond to the same source code
						 * location. */
						breakpointDetails.address = -1;
				}
				else if (x.first == "func")
					breakpointDetails.subprogramName = QString::fromStdString(x.second->asConstant()->constant());
				else if (x.first == "file")
					breakpointDetails.fileName = QString::fromStdString(x.second->asConstant()->constant());
				else if (x.first == "fullname")
					breakpointDetails.sourceCodeLocation.fullFileName = QString::fromStdString(x.second->asConstant()->constant());
				else if (x.first == "line")
					breakpointDetails.sourceCodeLocation.lineNumber = QString::fromStdString(x.second->asConstant()->constant()).toULong(0, 0);
				else if (x.first == "original-location")
					breakpointDetails.locationSpecifierString = QString::fromStdString(x.second->asConstant()->constant());
				else if (x.first == "locations" && x.second->asList())
				{
					/* Process multiple-location breakpoints. */
					for (const auto & b : x.second->asList()->values)
					{
						if (!b->asTuple())
							break;
						GdbBreakpointData nestedBreakpoint;
						nestedBreakpoint.disposition.clear();
						nestedBreakpoint.type = "<<< multiple >>>";
						nestedBreakpoint.locationSpecifierString.clear();
						for (const auto & x : b->asTuple()->map)
						{
							if (x.first == "number")
								nestedBreakpoint.gdbReportedNumberString = QString::fromStdString(x.second->asConstant()->constant());
							else if (x.first == "enabled")
								nestedBreakpoint.enabled = ((QString::fromStdString(x.second->asConstant()->constant()) == "y") ? true : false);
							else if (x.first == "addr")
							{
								bool ok;
								nestedBreakpoint.address = QString::fromStdString(x.second->asConstant()->constant()).toULong(& ok, 0);
								if (!ok)
									/* Should not happen. */
									nestedBreakpoint.address = -1;
							}
							else if (x.first == "func")
								nestedBreakpoint.subprogramName = QString::fromStdString(x.second->asConstant()->constant());
							else if (x.first == "file")
								nestedBreakpoint.fileName = QString::fromStdString(x.second->asConstant()->constant());
							else if (x.first == "fullname")
								nestedBreakpoint.sourceCodeLocation.fullFileName = QString::fromStdString(x.second->asConstant()->constant());
							else if (x.first == "line")
								nestedBreakpoint.sourceCodeLocation.lineNumber = QString::fromStdString(x.second->asConstant()->constant()).toULong(0, 0);
						}
						breakpointDetails.multipleLocationBreakpoints.push_back(nestedBreakpoint);
					}
				}
			}
			breakpoints.push_back(breakpointDetails);
		}
		updateBreakpointsView();
		refreshSourceCodeView();
	}
	return true;
}

bool MainWindow::handleStackResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	const GdbMiParser::MIList * frames;
	if (results.size() != 1 || results.at(0).variable != "stack" || !(frames = results.at(0).value->asList()))
		return false;
	backtrace.clear();
	for (const auto & frame : frames->results)
	{
		if (frame.variable != "frame" || !frame.value->asTuple())
			break;
		StackFrameData frameData;
		for (const auto & f : frame.value->asTuple()->map)
		{
			if (f.first == "level")
				frameData.level = QString::fromStdString(f.second->asConstant()->constant()).toULong(0, 0);
			else if (f.first == "addr")
				frameData.pcAddress = QString::fromStdString(f.second->asConstant()->constant()).toULong(0, 0);
			else if (f.first == "func")
				frameData.subprogramName = QString::fromStdString(f.second->asConstant()->constant());
			else if (f.first == "file")
			{
				frameData.gdbReportedFileName = QString::fromStdString(f.second->asConstant()->constant());
				frameData.fileName = QFileInfo(frameData.gdbReportedFileName).fileName();
			}
			else if (f.first == "fullname")
				frameData.fullFileName = QString::fromStdString(f.second->asConstant()->constant());
			else if (f.first == "line")
				frameData.lineNumber = QString::fromStdString(f.second->asConstant()->constant()).toULong(0, 0);
		}
		backtrace.push_back(frameData);
	}
	ui->treeWidgetBacktrace->clear();
	for (const auto & frame : backtrace)
	{
		QTreeWidgetItem * w;
		ui->treeWidgetBacktrace->addTopLevelItem(w = new QTreeWidgetItem(QStringList()
										 << QString("%1").arg(frame.level)
										 << frame.subprogramName
										 << frame.fileName
										 << QString("%1").arg(frame.lineNumber)
										 << QString("$%1").arg(frame.pcAddress, 8, 16, QChar('0'))));
		w->setData(0, SourceFileData::FILE_NAME, frame.fullFileName);
		w->setData(0, SourceFileData::LINE_NUMBER, frame.lineNumber);
	}
	if (ui->checkBoxEnableTraceLogging->isChecked() && backtrace.size())
	{
		StackFrameData frame = backtrace.at(0);
		QTreeWidgetItem * w = new QTreeWidgetItem(QStringList() << frame.fileName << QString("%1").arg(frame.lineNumber) << frame.fullFileName);
		w->setData(0, SourceFileData::FILE_NAME, frame.fullFileName);
		w->setData(0, SourceFileData::LINE_NUMBER, frame.lineNumber);
		ui->treeWidgetTraceLog->addTopLevelItem(w);
	}
#if 0
	QTreeWidgetItem * frame = ui->treeWidgetBacktrace->topLevelItem(0);
	if (frame)
		ui->treeWidgetBacktrace->setItemSelected(frame, true);
#endif
	return true;
}

bool MainWindow::handleRegisterNamesResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MIList * registerNames;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "register-names" || !(registerNames = results.at(0).value->asList()))
		return false;
	targetRegisterNames.clear();
	ui->treeWidgetRegisters->clear();
	for (const auto & r : registerNames->values)
	{
		const GdbMiParser::MIConstant * t;
		if ((t = r->asConstant()))
		{
			targetRegisterNames << QString::fromStdString(t->constant());
			ui->treeWidgetRegisters->addTopLevelItem(new QTreeWidgetItem(QStringList() << targetRegisterNames.back()));
		}
	}
	return true;
}

bool MainWindow::handleRegisterValuesResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MIList * registerValues;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "register-values" || !(registerValues = results.at(0).value->asList()))
		return false;
	for (const auto & r : registerValues->values)
	{
		const GdbMiParser::MITuple * t;
		if (!(t = r->asTuple()))
			continue;
		unsigned registerNumber;
		QString registerValue;
		for (const auto & r : t->map)
			if (r.first == "number")
				registerNumber = QString::fromStdString(r.second->asConstant()->constant()).toUInt();
			else if (r.first == "value")
				registerValue = QString::fromStdString(r.second->asConstant()->constant());
		if (registerNumber < ui->treeWidgetRegisters->topLevelItemCount())
			ui->treeWidgetRegisters->topLevelItem(registerNumber)->setText(1, registerValue);
	}
	return false;
}

bool MainWindow::handleChangelistResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MIList * changelist;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "changelist" || !(changelist = results.at(0).value->asList()))
		return false;
	std::unordered_set<const GdbVarObjectTreeItem *> highlightedItems;
	for (const auto & c : changelist->values)
	{
		const GdbMiParser::MITuple * changeDetails;
		if (!(changeDetails = c->asTuple()))
			continue;
		QString miName, value, newType;
		unsigned newNumChildren = 0;
		/*! \todo	There is some strange behavior in gdb - if you create a varobject for an expression
		 * 		that has child items (e.g., an array), and then run the program, and then halt the
		 * 		program at a context in which the varobject is no longer in scope, you are still
		 * 		able to list the varobject child items, and gdb does not return an error, but instead
		 * 		replies with empty strings for the values of leaf varobject items. */
		bool isInScope = false, isTypeChanged = true;
		for (const auto & t : changeDetails->map)
		{
			if (t.first == "name")
				miName = QString::fromStdString(t.second->asConstant()->constant());
			else if (t.first == "value")
				value = QString::fromStdString(t.second->asConstant()->constant());
			else if (t.first == "new_type")
				newType = QString::fromStdString(t.second->asConstant()->constant());
			else if (t.first == "new_num_children")
				newNumChildren = QString::fromStdString(t.second->asConstant()->constant()).toUInt();
			else if (t.first == "in_scope")
				isInScope = (t.second->asConstant()->constant() == "true" ? true : false);
			else if (t.first == "type_changed")
				isTypeChanged = (t.second->asConstant()->constant() == "true" ? true : false);
		}
		QModelIndex index = locateVarObject(miName, varObjectTreeItemModel.index(0, 0), QModelIndex());
		GdbVarObjectTreeItem * node = static_cast<GdbVarObjectTreeItem *>(index.internalPointer());
		/*! \todo	Document the gdb varobject state transition handling rationale and operation. */
		if (!index.isValid())
			*(int*)0=0;
		if ((!isInScope || isTypeChanged) && node->childCount())
		{
			sendDataToGdbProcess(QString("-var-delete -c %1\n").arg(node->miName).toLocal8Bit());
			/* The varobject item will be marked as out of scope below. If the item is currently
			 * expanded, but at a later time again gets into scope, the displaying of the item's
			 * expand indicator may be shown incorrectly. To avoid such a scenario, collapse
			 * the item here. */
			ui->treeViewDataObjects->collapse(index);
		}
		isInScope ? varObjectTreeItemModel.markNodeAsInsideScope(index) : varObjectTreeItemModel.markNodeAsOutOfScope(index);
		if (isInScope)
		{
			if (!isTypeChanged)
				varObjectTreeItemModel.updateNodeValue(index, value);
			else
				varObjectTreeItemModel.updateNodeType(index, newType, value, newNumChildren);
		}
		highlightedItems.insert(node);
	}
	varObjectTreeItemModel.setHighlightedItems(highlightedItems);
	return true;
}


bool MainWindow::handleVariablesResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MIList * variables;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "variables" || !(variables = results.at(0).value->asList()))
		return false;
	ui->treeWidgetStackVariables->clear();
	for (const auto & v : variables->values)
	{
		const GdbMiParser::MITuple * variable;
		if (!(variable = v->asTuple()))
			continue;
		QString name, value, hexValue = "???";
		for (const auto & t : variable->map)
			if (t.first == "name")
				name = QString::fromStdString(t.second->asConstant()->constant());
			else if (t.first == "value")
				value = QString::fromStdString(t.second->asConstant()->constant());
		bool ok;
		unsigned long long t = value.toULongLong(& ok);
		if (ok)
			hexValue = QString("0x%1").arg(t, (t > 255) ? ((t > 0xffffffffUL) ? 0 : 8) : 2, 16, QChar('0'));
		ui->treeWidgetStackVariables->addTopLevelItem(new QTreeWidgetItem(QStringList() << name << value << hexValue));
	}
	return true;
}

bool MainWindow::handleFrameResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MITuple * frame;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "frame" || !(frame = results.at(0).value->asTuple()))
		return false;
	int frameNumber = -1;
	for (const auto & t : frame->map)
		if (t.first == "level")
		{
			frameNumber = QString::fromStdString(t.second->asConstant()->constant()).toUInt();
			break;
		}
	if (frameNumber != -1)
	{
		QTreeWidgetItem * frameItem = ui->treeWidgetBacktrace->topLevelItem(frameNumber);
		if (frameItem)
		{
			ui->treeWidgetBacktrace->blockSignals(true);
			ui->treeWidgetBacktrace->setItemSelected(frameItem, true);
			ui->treeWidgetBacktrace->blockSignals(false);
			showSourceCode(frameItem);
			emit targetCallStackFrameChanged();
		}
	}
	return true;
}

bool MainWindow::handleDisassemblyResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MIList * disassembly;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "asm_insns" || !(disassembly = results.at(0).value->asList()))
		return false;
	ui->plainTextEditDisassembly->setPlainText("Disassembly:");
	for (const auto & d : disassembly->results)
	{
		if (d.variable == "src_and_asm_line")
		{
			const GdbMiParser::MITuple * t = d.value->asTuple();
			for (const auto & details : t->map)
			{
				if (details.first == "line_asm_insn")
				{
					for (const auto & line : details.second->asList()->values)
					{
						std::string address, opcodes, mnemonics;
						for (const auto & line_details : line->asTuple()->map)
						{
							if (line_details.first == "address")
								address = line_details.second->asConstant()->constant();
							else if (line_details.first == "opcodes")
								opcodes = line_details.second->asConstant()->constant();
							else if (line_details.first == "inst")
								mnemonics = line_details.second->asConstant()->constant();
						}
						ui->plainTextEditDisassembly->appendPlainText(QString::fromStdString(address + '\t' + opcodes + '\t' + mnemonics));

					}

				}
			}
		}
		else
			*(int*)0=0;
	}
	return true;
}

bool MainWindow::handleSequencePoints(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	bool response_handled = false;
	if (!gdbTokenContext.hasContextForToken(tokenNumber))
		return false;
	switch (gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode)
	{
	case GdbTokenContext::GdbResponseContext::GDB_SEQUENCE_POINT_SOURCE_CODE_ADDRESSES_RETRIEVED:
		updateSourceListView();
		updateSymbolViews();
		break;
	}
	if (response_handled)
		gdbTokenContext.readAndRemoveContext(tokenNumber);
	return response_handled;
}

bool MainWindow::handleBreakpointUpdateResponses(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	if (results.size() && results.at(0).variable == "bkpt")
	{
update_breakpoint_data:
		sendDataToGdbProcess("-break-list\n");
		return true;
	}
	if (gdbTokenContext.hasContextForToken(tokenNumber)
		&& gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_REQUEST_BREAKPOINT_LIST_UPDATE)
	{
		gdbTokenContext.readAndRemoveContext(tokenNumber);
		goto update_breakpoint_data;
	}
	return false;
}

void MainWindow::on_lineEditGdbCommand_returnPressed()
{
QString s =  ui->lineEditGdbCommand->text();
	/* Remove any newlines. This is possible if pasting text from the clipboard. */
	s.replace('\n', ' ');
	qDebug() << "console command:" << escapeString(s);
	QString data = "-interpreter-exec console \"" + escapeString(s) + "\"\n";
	sendDataToGdbProcess(data);
}

void MainWindow::gdbProcessError(QProcess::ProcessError error)
{
	switch (error)
	{
		case QProcess::FailedToStart:
			QMessageBox::critical(0, "The gdb process failed to start", "Gdb failed to start");
			break;
		case QProcess::Crashed:
			QMessageBox::critical(0, "The gdb process crashed", "Gdb crashed");
			break;
		case QProcess::Timedout:
			QMessageBox::critical(0, "Gdb process timeout", "Timeout communicating with gdb");
			break;
		case QProcess::WriteError:
			QMessageBox::critical(0, "Gdb process write error", "Gdb write error");
			break;
		case QProcess::ReadError:
			QMessageBox::critical(0, "Gdb process read error", "Gdb read error");
			break;
		default:
		case QProcess::UnknownError:
			QMessageBox::critical(0, "Gdb process unknown error", "Unknown gdb error");
			break;
	}
}

void MainWindow::gdbProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	qDebug() << gdbProcess->readAllStandardError();
	qDebug() << gdbProcess->readAllStandardOutput();
	qDebug() << "gdb process finished";
	if (exitStatus == QProcess::CrashExit)
	{
		if (QMessageBox::critical(0, "The gdb process crashed", "Gdb crashed\n\nDo you want to restart the gdb process?", "Restart gdb", "Abort")
				== 0)
			gdbProcess->start();
	}
	else if (exitCode != 0)
	{
		if (QMessageBox::critical(0, "The gdb process exited with error", QString("Gdb exited with error code: %1\n\nDo you want to restart the gdb process?").arg(exitCode), "Restart gdb", "Abort")
				== 0)
			gdbProcess->start();
	}
	else
	{
		if (QMessageBox::information(0, "The gdb process exited normally", "Gdb exited normally.\n\nDo you want to restart the gdb process?", "Restart gdb", "Abort")
				== 0)
			gdbProcess->start();
	}
}

void MainWindow::gdbStandardErrorDataAvailable()
{
	qDebug() << gdbProcess->readAllStandardError();
}

void MainWindow::sendDataToGdbProcess(const QString & data)
{
	ui->plainTextEditConsoleStreamOutput->appendPlainText(">>> " + data);
	gdbProcess->write(data.toLocal8Bit());
	gdbProcess->waitForBytesWritten();
}
void MainWindow::readGdbVarObjectChildren(const QModelIndex parent)
{
	unsigned n = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
							   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_NUMCHILD));
	GdbVarObjectTreeItem * t = static_cast<GdbVarObjectTreeItem *>(parent.internalPointer());
	if (varobjectParentModelIndexes.count(n))
		*(int*)0=0;
	varobjectParentModelIndexes.operator[](n) = parent;

	sendDataToGdbProcess((QString("%1-var-list-children --all-values ").arg(n) + t->miName + "\n"));
}

void MainWindow::showSourceCode(QTreeWidgetItem *item)
{
	QVariant v = item->data(0, SourceFileData::FILE_NAME);
	if (v.type() != QMetaType::QString)
		/* If the source code location details seem to be not set for this tree widget item
		 * do not attempt to navigate to a source code location */
		return;
	QString sourceFileName = item->data(0, SourceFileData::FILE_NAME).toString();
	int lineNumber = item->data(0, SourceFileData::LINE_NUMBER).toInt();
	if (lineNumber == 0)
		lineNumber = 1;
	displaySourceCodeFile(SourceCodeLocation(sourceFileName, lineNumber));
}

QString MainWindow::getExecutableFilename()
{
	QSettings s("qgbd.rc", QSettings::IniFormat);
	QFileInfo f(s.value("last-loaded-executable-file", QString()).toString());
	return QFileDialog::getOpenFileName(0, "Load executable for debugging", f.canonicalPath());
}

void MainWindow::breakpointsContextMenuRequested(QPoint p)
{
	QTreeWidgetItem * w = ui->treeWidgetBreakpoints->itemAt(p);
	if (w)
	{
		QMenu menu(this);
		GdbBreakpointData * breakpoint = static_cast<GdbBreakpointData *>(w->data(0, SourceFileData::BREAKPOINT_DATA_POINTER).value<void *>());
		QHash<void *, int> menuSelections;
		menuSelections.operator [](menu.addAction("Delete breakpoint")) = 1;
		menuSelections.operator [](menu.addAction(breakpoint->enabled ? "Disable breakpoint" : "Enable breakpoint")) = 2;
		menu.addAction("Cancel");
		/* Because of the header of the tree widget, it looks more natural to set the
		 * menu position on the screen at point translated from the tree widget viewport,
		 * not from the tree widget itself. */
		QAction * selection = menu.exec(ui->treeWidgetBreakpoints->viewport()->mapToGlobal(p));
		QString breakpointMiCommand;
		switch (menuSelections.operator []((void *) selection))
		{
		case 1:
			/* Delete breakpoint. */
			breakpointMiCommand = "delete"; if (0)
		case 2:
			/* Toggle breakpoint. */
			breakpointMiCommand = (breakpoint->enabled ? "disable" : "enable");
			{
				unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
										   GdbTokenContext::GdbResponseContext::GDB_REQUEST_BREAKPOINT_LIST_UPDATE));
				sendDataToGdbProcess(QString("%1-break-%2 %3\n").arg(t).arg(breakpointMiCommand).arg(breakpoint->gdbReportedNumberString));
			}
			break;
		default:
			break;
		}
	}
}

void MainWindow::svdContextMenuRequested(QPoint p)
{
	QTreeWidgetItem * w = ui->treeWidgetSvd->itemAt(p);
	if (w)
	{
		if (w->data(0, SVD_REGISTER_POINTER).isNull())
			return;
		QMenu menu(this);
		QAction * readRegisterAction = menu.addAction("Read register");
		QAction * createViewAction = menu.addAction("Create register view");
		menu.addAction("Cancel");
		/* Because of the header of the tree widget, it looks more natural to set the
		 * menu position on the screen at point translated from the tree widget viewport,
		 * not from the tree widget itself. */
		QAction * selection = menu.exec(ui->treeWidgetSvd->viewport()->mapToGlobal(p));
		if (selection)
		{
			if (selection == readRegisterAction)
			{
				uint32_t address = w->data(0, SVD_REGISTER_ADDRESS).toUInt();
				sendDataToGdbProcess(QString("-data-evaluate-expression \"*(unsigned int*)0x%1\"\n").arg(address,0, 16, QChar('0')));
			}
			else if (selection == createViewAction)
			{
				createSvdRegisterView(w, 0);
			}
		}
	}
}

void MainWindow::bookmarksContextMenuRequested(QPoint p)
{
	QTreeWidgetItem * w = ui->treeWidgetBookmarks->itemAt(p);
	if (!w)
		return;
	int bookmarkIndex = ui->treeWidgetBookmarks->indexOfTopLevelItem(w);
	QMenu menu(this);
	QHash<void *, int> menuSelections;
	menuSelections.operator [](menu.addAction("Delete bookmark")) = 1;
	menu.addAction("Cancel");
	/* Because of the header of the tree widget, it looks more natural to set the
	 * menu position on the screen at point translated from the tree widget viewport,
	 * not from the tree widget itself. */
	QAction * selection = menu.exec(ui->treeWidgetBookmarks->viewport()->mapToGlobal(p));

	switch (menuSelections.operator []((void *) selection))
	{
	case 1:
		/* Delete bookmark. */
		bookmarks.removeAt(bookmarkIndex);
		updateBookmarksView();
		refreshSourceCodeView();
		break;
	default:
		break;
	}
}

void MainWindow::breakpointViewItemChanged(QTreeWidgetItem * item, int column)
{
	if (column != GdbBreakpointData::TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER)
		return;
	GdbBreakpointData * breakpoint = static_cast<GdbBreakpointData *>(item->data(0, SourceFileData::BREAKPOINT_DATA_POINTER).value<void *>());
	if (breakpoint->enabled == item->checkState(GdbBreakpointData::TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER))
		/* Nothing to do, return. */
		return;
	unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
								   GdbTokenContext::GdbResponseContext::GDB_REQUEST_BREAKPOINT_LIST_UPDATE));
	sendDataToGdbProcess(QString("%1-break-%2 %3\n")
			     .arg(t)
			     .arg(breakpoint->enabled ? "disable" : "enable")
			     .arg(breakpoint->gdbReportedNumberString));
}

void MainWindow::stringSearchReady(const QString pattern, QSharedPointer<QVector<StringFinder::SearchResult>> results, bool resultsTruncated)
{
	ui->treeWidgetSearchResults->clear();
	ui->lineEditSearchFilesForText->setText(pattern);
	for (const auto & result : * results)
	{
		QTreeWidgetItem * w = new QTreeWidgetItem(QStringList() << QFileInfo(result.fullFileName).fileName()
							  << QString("%1").arg(result.lineNumber) << result.sourceCodeLineText);
		w->setData(0, SourceFileData::FILE_NAME, result.fullFileName);
		w->setData(0, SourceFileData::LINE_NUMBER, result.lineNumber);
		ui->treeWidgetSearchResults->addTopLevelItem(w);
	}
	ui->treeWidgetSearchResults->sortByColumn(0, Qt::AscendingOrder);
	if (resultsTruncated)
		ui->treeWidgetSearchResults->addTopLevelItem(new QTreeWidgetItem(QStringList() << "" << "xxx" << "Too many results - search results truncated"));
}

void MainWindow::updateSourceListView()
{
bool showOnlySourcesWithMachineCode = ui->checkBoxShowOnlySourcesWithMachineCode->isChecked();
	ui->treeWidgetSourceFiles->clear();
	for (const auto & f : sourceFiles)
		if (!showOnlySourcesWithMachineCode || !f.isSourceLinesFetched || f.machineCodeLineNumbers.size())
		{
			QTreeWidgetItem * t;
			ui->treeWidgetSourceFiles->addTopLevelItem(t = new QTreeWidgetItem(QStringList() << f.fileName << f.fullFileName));
			t->setData(0, SourceFileData::FILE_NAME, f.fullFileName);
			t->setData(0, SourceFileData::LINE_NUMBER, 0);
			for (const auto & s : f.subprograms)
			{
				QTreeWidgetItem * x;
				t->addChild(x = new QTreeWidgetItem(QStringList() << s.description));
				x->setData(0, SourceFileData::FILE_NAME, f.fullFileName);
				x->setData(0, SourceFileData::LINE_NUMBER, s.line);
			}
		}
	ui->treeWidgetSourceFiles->sortByColumn(0, Qt::AscendingOrder);
}

void MainWindow::updateSymbolViews()
{
	ui->treeWidgetSubprograms->clear();
	ui->treeWidgetStaticDataObjects->clear();
	for (const auto & f : sourceFiles)
	{
		for (const auto & s : f.subprograms)
		{
			QTreeWidgetItem * subprogram;
			ui->treeWidgetSubprograms->addTopLevelItem(subprogram = new QTreeWidgetItem(
				   QStringList() << s.name << f.fileName << QString("%1").arg(s.line) << s.description));

			subprogram->setData(0, SourceFileData::FILE_NAME, f.fullFileName);
			subprogram->setData(0, SourceFileData::LINE_NUMBER, s.line);
		}
		for (const auto & s : f.variables)
		{
			QTreeWidgetItem * variable;
			ui->treeWidgetStaticDataObjects->addTopLevelItem(variable = new QTreeWidgetItem(
				   QStringList() << s.name << f.fileName << QString("%1").arg(s.line) << s.description));

			variable->setData(0, SourceFileData::FILE_NAME, f.fullFileName);
			variable->setData(0, SourceFileData::LINE_NUMBER, s.line);
		}
		for (const auto & s : f.dataTypes)
		{
			QTreeWidgetItem * type;
			ui->treeWidgetDataTypes->addTopLevelItem(type = new QTreeWidgetItem(
				   QStringList() << s.name << f.fileName << QString("%1").arg(s.line)));

			type->setData(0, SourceFileData::FILE_NAME, f.fullFileName);
			type->setData(0, SourceFileData::LINE_NUMBER, s.line);
		}
	}
	ui->treeWidgetSubprograms->sortByColumn(0, Qt::AscendingOrder);
	ui->treeWidgetStaticDataObjects->sortByColumn(0, Qt::AscendingOrder);
	ui->treeWidgetDataTypes->sortByColumn(0, Qt::AscendingOrder);
}


void MainWindow::updateBreakpointsView()
{
	ui->treeWidgetBreakpoints->clear();
	for (const auto & b : breakpoints)
	{
		QTreeWidgetItem * w = new QTreeWidgetItem(QStringList()
							  << b.gdbReportedNumberString
							  << b.type
							  << b.disposition
							  << (b.enabled ? "yes" : "no")
							  << QString("0x%1").arg(b.address, 8, 16, QChar('0'))
							  << b.locationSpecifierString
							  );
		w->setCheckState(GdbBreakpointData::TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER, b.enabled ? Qt::Checked : Qt::Unchecked);
		w->setData(0, SourceFileData::BREAKPOINT_DATA_POINTER, QVariant::fromValue((void *) & b));
		if (b.sourceCodeLocation.lineNumber != -1)
		{
			w->setData(0, SourceFileData::FILE_NAME, b.sourceCodeLocation.fullFileName);
			w->setData(0, SourceFileData::LINE_NUMBER, b.sourceCodeLocation.lineNumber);
		}
		for (const auto & m : b.multipleLocationBreakpoints)
		{
			QTreeWidgetItem * t = new QTreeWidgetItem(w, QStringList()
								  << m.gdbReportedNumberString
								  << m.type
								  << m.disposition
								  << (m.enabled ? "yes" : "no")
								  << QString("0x%1").arg(m.address, 8, 16, QChar('0'))
								  << m.locationSpecifierString
								  );
			t->setCheckState(GdbBreakpointData::TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER, b.enabled ? Qt::Checked : Qt::Unchecked);
			t->setData(0, SourceFileData::BREAKPOINT_DATA_POINTER, QVariant::fromValue((void *) & m));
			if (m.sourceCodeLocation.lineNumber != -1)
			{
				t->setData(0, SourceFileData::FILE_NAME, m.sourceCodeLocation.fullFileName);
				t->setData(0, SourceFileData::LINE_NUMBER, m.sourceCodeLocation.lineNumber);
			}
		}
		ui->treeWidgetBreakpoints->addTopLevelItem(w);
	}
}

void MainWindow::updateBookmarksView()
{
	ui->treeWidgetBookmarks->clear();
	for (const auto & bookmark : bookmarks)
	{
		QTreeWidgetItem * w = new QTreeWidgetItem(QStringList() << QFileInfo(bookmark.fullFileName).fileName() << QString("%1").arg(bookmark.lineNumber));
		w->setData(0, SourceFileData::FILE_NAME, bookmark.fullFileName);
		w->setData(0, SourceFileData::LINE_NUMBER, bookmark.lineNumber);
		ui->treeWidgetBookmarks->addTopLevelItem(w);
	}
}

void MainWindow::on_lineEditGdbMiCommand_returnPressed()
{
QString s =  ui->lineEditGdbMiCommand->text();
	/* Remove any newlines. This is possible if pasting text from the clipboard. */
	s.replace('\n', ' ');
	/*! \todo	Determine what and when exactly needs to be escaped. */
#if 0
	/* Escape. */
	s.replace("\\", "/");
	s.replace("\"", "\\\"");
#endif
	ui->lineEditGdbMiCommand->clear();
	sendDataToGdbProcess(s + '\n');
}

void MainWindow::on_lineEditVarObjectExpression_returnPressed()
{
	unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
							   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_NAME,
							   ui->lineEditVarObjectExpression->text(),
							   & varObjectTreeItemModel)
				);
	sendDataToGdbProcess((QString("%1-var-create - @ ").arg(t) + ui->lineEditVarObjectExpression->text() + "\n").toLocal8Bit());
	ui->lineEditVarObjectExpression->clear();
}

void MainWindow::highlightBreakpointedLines()
{
	QTextCursor c(ui->plainTextEditSourceView->textCursor());
	sourceCodeViewHighlights.disabledBreakpointedLines.clear();
	sourceCodeViewHighlights.enabledBreakpointedLines.clear();
	/* Highlight lines with breakpoints, if any. */
	std::multimap<int /* Source code line */, const GdbBreakpointData * /* Breakpoint details. */> breakpointedLines;
	for (const auto & b : breakpoints)
		if (b.sourceCodeLocation.fullFileName == displayedSourceCodeFile)
			breakpointedLines.insert(std::pair<int /* Source code line */, const GdbBreakpointData * /* Breakpoint details. */>(b.sourceCodeLocation.lineNumber, &b));
	QTextEdit::ExtraSelection selection;
	for (const auto & b : breakpointedLines)
	{
		c.movePosition(QTextCursor::Start);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, b.first - 1);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
		selection.cursor = c;
		if (b.second->enabled)
		{
			selection.format = sourceCodeViewHighlightFormats.enabledBreakpoint;
			sourceCodeViewHighlights.enabledBreakpointedLines << selection;
		}
		else
		{
			selection.format = sourceCodeViewHighlightFormats.disabledBreakpoint;
			sourceCodeViewHighlights.disabledBreakpointedLines << selection;
		}
	}
}

void MainWindow::highlightBookmarks()
{
	QTextCursor c(ui->plainTextEditSourceView->textCursor());
	sourceCodeViewHighlights.bookmarkedLines.clear();
	QTextEdit::ExtraSelection selection;
	selection.format = sourceCodeViewHighlightFormats.bookmark;
	for (const auto & bookmark : bookmarks)
	{
		if (bookmark.fullFileName != displayedSourceCodeFile)
			continue;
		c.movePosition(QTextCursor::Start);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, bookmark.lineNumber - 1);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
		selection.cursor = c;
		sourceCodeViewHighlights.bookmarkedLines << selection;
	}
}

void MainWindow::refreshSourceCodeView()
{
	highlightBreakpointedLines();
	highlightBookmarks();
	/* Note: the ordering of the highlight formats in the list below is important - later
	 * entries in the list override settings defined by preceding entries in the list, in
	 * case there are different highlight format specifications for the same line in this list. */
	ui->plainTextEditSourceView->setExtraSelections(QList<QTextEdit::ExtraSelection>()
							<< sourceCodeViewHighlights.bookmarkedLines
							<< sourceCodeViewHighlights.navigatedSourceCodeLine
							<< sourceCodeViewHighlights.currentSourceCodeLine
							<< sourceCodeViewHighlights.disabledBreakpointedLines
							<< sourceCodeViewHighlights.enabledBreakpointedLines
							<< sourceCodeViewHighlights.searchedTextMatches
							);
}

void MainWindow::navigateToSymbolAtCursor(void)
{
	QTextCursor c = ui->plainTextEditSourceView->textCursor();
	c.select(QTextCursor::WordUnderCursor);
	QString symbolName = c.selectedText();

	QList<QTreeWidgetItem*> symbols = ui->treeWidgetSubprograms->findItems(symbolName, Qt::MatchExactly);
	symbols += ui->treeWidgetStaticDataObjects->findItems(symbolName, Qt::MatchExactly);
	symbols += ui->treeWidgetDataTypes->findItems(symbolName, Qt::MatchExactly);
	if (symbols.empty())
		return;
	if (symbols.size() != 1)
		QMessageBox::information(0, "Multiple symbols found", "Multiple symbols found for id: " + symbolName + "\nNavigating to the first item in the list");
	QString sourceFileName = symbols.at(0)->data(0, SourceFileData::FILE_NAME).toString();
	int lineNumber = symbols.at(0)->data(0, SourceFileData::LINE_NUMBER).toInt();
	displaySourceCodeFile(SourceCodeLocation(sourceFileName, lineNumber));
}

void MainWindow::navigateBack()
{
	if (navigationStack.size() > 1)
	{
		navigationStack.drop();
		displaySourceCodeFile(navigationStack.back(), false);
	}
}

bool MainWindow::searchCurrentSourceText(const QString pattern)
{
	if (pattern.length() == 0)
		return false;
	searchData.matchPositions.clear();
	QString document = ui->plainTextEditSourceView->toPlainText();
	int index = 0, position;
	while ((position = document.indexOf(pattern, index)) != -1)
		searchData.matchPositions.push_back(position), index = position + 1;

	searchData.lastSearchedText = pattern;
	sourceCodeViewHighlights.searchedTextMatches.clear();
	QTextCursor c(ui->plainTextEditSourceView->textCursor());
	int matchLength = pattern.size();
	for (const auto & matchPosition : searchData.matchPositions)
	{
		c.setPosition(matchPosition);
		c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, matchLength);
		QTextEdit::ExtraSelection s;
		s.cursor = c;
		s.format = sourceCodeViewHighlightFormats.searchedText;
		sourceCodeViewHighlights.searchedTextMatches << s;
	}
	refreshSourceCodeView();
	return true;
}

void MainWindow::moveCursorToNextMatch()
{
	/* Navigate to next search match, if any. */
	if (searchData.matchPositions.empty())
		return;
	int l = 0, h = searchData.matchPositions.size() - 1, m;
	int cursorPosition = ui->plainTextEditSourceView->textCursor().position();

	do
	{
		m = (l + h) >> 1;
		if (cursorPosition == searchData.matchPositions.at(m))
			break;
		if (cursorPosition < searchData.matchPositions.at(m))
			h = m - 1;
		else
			l = m + 1;
	}
	while (l <= h);
	if (cursorPosition >= searchData.matchPositions.at(m))
		m ++;
	if (m == searchData.matchPositions.size())
		m = 0;
	QTextCursor c(ui->plainTextEditSourceView->textCursor());
	c.setPosition(searchData.matchPositions.at(m));

	ui->plainTextEditSourceView->setTextCursor(c);
	ui->plainTextEditSourceView->ensureCursorVisible();
}

void MainWindow::moveCursorToPreviousMatch()
{
	/* Navigate to previous search match, if any. */
	if (searchData.matchPositions.empty())
		return;
	int l = 0, h = searchData.matchPositions.size() - 1, m;
	int cursorPosition = ui->plainTextEditSourceView->textCursor().position();

	do
	{
		m = (l + h) >> 1;
		if (cursorPosition == searchData.matchPositions.at(m))
			break;
		if (cursorPosition < searchData.matchPositions.at(m))
			h = m - 1;
		else
			l = m + 1;
	}
	while (l <= h);
	if (cursorPosition <= searchData.matchPositions.at(m))
		m --;
	if (m == -1)
		m = searchData.matchPositions.size() - 1;
	QTextCursor c(ui->plainTextEditSourceView->textCursor());
	c.setPosition(searchData.matchPositions.at(m));

	ui->plainTextEditSourceView->setTextCursor(c);
	ui->plainTextEditSourceView->ensureCursorVisible();
}

void MainWindow::updateHighlightedWidget()
{
	widgetFlashHighlighterData.highlightedWidget->setStyleSheet(widgetFlashHighlighterData.flashStyleSheets[(widgetFlashHighlighterData.flashCount ++) & 1]);

	if (widgetFlashHighlighterData.flashCount == widgetFlashHighlighterData.flashRepeatCount)
	{
		widgetFlashHighlighterData.timer.stop();
		widgetFlashHighlighterData.highlightedWidget->setStyleSheet(widgetFlashHighlighterData.defaultStyleSheet);
	}
}

void MainWindow::displayHelpMenu()
{
	QPoint p = QCursor::pos();
	QMenu menu(this);
	menu.addAction("The shortcuts here work OUTSIDE this menu");
	menu.addAction("(but you may select items here with the mouse)");
	for (const auto & a : highlightWidgetActions)
	{
		QAction * act = menu.addAction(a->text() + "\t(" + a->shortcut().toString() + ")");
		connect(act, SIGNAL(triggered(bool)), a, SLOT(trigger()));
	}
	QAction * selection = menu.exec((p));
}

void MainWindow::flashHighlightDockWidget(QDockWidget *w)
{
	if (widgetFlashHighlighterData.timer.isActive())
	{
		/* The highlighter timer is running, which means a widget is currently being highlighted,
		 * refuse to start another highlight run. */
		return;
	}
	/* Make sure that the widget that should be highlighted is visible, and, in case it is currently docked,
	 * make its containing dock tab active. */
	w->setVisible(true);
	QList<QTabBar *> tabBars = findChildren<QTabBar *>();

	for (const auto & t : tabBars)
		for (int tabIndex = 0; tabIndex < t->count(); tabIndex ++)
			if (t->tabText(tabIndex) == w->windowTitle())
				t->setCurrentIndex(tabIndex);

	widgetFlashHighlighterData.flashCount = 0;
	widgetFlashHighlighterData.highlightedWidget = w;
	widgetFlashHighlighterData.timer.start(widgetFlashHighlighterData.flashIntervalMs);
}

void MainWindow::displaySourceCodeFile(const SourceCodeLocation &sourceCodeLocation, bool saveCurrentLocationToNavigationStack)
{
QFile f(sourceCodeLocation.fullFileName);
QFileInfo fi(sourceCodeLocation.fullFileName);
int currentBlockNumber = ui->plainTextEditSourceView->textCursor().blockNumber();

	ui->plainTextEditSourceView->blockSignals(true);
	ui->plainTextEditSourceView->clear();

	/* Save the current source code view location in the navigation stack, if valid. */
	if (saveCurrentLocationToNavigationStack && !displayedSourceCodeFile.isEmpty())
		navigationStack.push(SourceCodeLocation(displayedSourceCodeFile, currentBlockNumber + 1));

	displayedSourceCodeFile.clear();

	if (!fi.exists())
	{
		/* Attempt to adjust the filename path on windows systems. */
		fi.setFile(Utils::filenameToWindowsFilename(sourceCodeLocation.fullFileName));
		f.setFileName(fi.absoluteFilePath());
	}
	if (!fi.exists())
		ui->plainTextEditSourceView->appendPlainText(QString("Cannot find file \"%1\"").arg(sourceCodeLocation.fullFileName));
	else if (!f.open(QFile::ReadOnly))
		ui->plainTextEditSourceView->appendPlainText(QString("Failed to open file \"%1\"").arg(sourceCodeLocation.fullFileName));
	else
	{
		/*! \todo Wrap this as a function (along with an example in the 'clex.y' scanner), and simply
		 * call that function. */
		yyscan_t scanner;
		std::string s;
		yylex_init_extra(& s, &scanner);

		yy_scan_string((f.readAll() + '\0').constData(), scanner);
		yylex(scanner);
		yylex_destroy(scanner);

		/* Prepend line numbers to the source code lines. */
		QList<QString> lines = QString::fromStdString(s).split('\n');
		int numFieldWidth = QString("%1").arg(lines.count()).length();
		QByteArray source =
				"<!DOCTYPE html>\n"
				"<html>\n"
				"<head>\n"
				"<meta charset=\"ISO-8859-1\">\n"
				"<title>Source file</title>\n"
				"<link rel=\"stylesheet\" type=\"text/css\" href=\":/resources/highlight.css\">\n"
				//"<link rel=\"stylesheet\" type=\"text/css\" href=\"highlight.css\">\n"
				"</head>\n"
				"<body class=\"hl\">\n"
				"<pre class=\"hl\">"
				;
		int i = 0;
		std::unordered_set<int> empty, * machineCodeLineNumbers = & empty;
		const auto & f = sourceFiles.find(sourceCodeLocation.fullFileName);
		if (f != sourceFiles.cend())
			machineCodeLineNumbers = & f->machineCodeLineNumbers;
		for (const auto & l : lines)
		{
			QString n(QString("%1").arg(++ i));
			source += QString(numFieldWidth - n.size(), QChar(' ')) + n + (machineCodeLineNumbers->count(i) ? '*':' ') + '|' + l + '\n';
		}

		source +=
				"</pre>\n"
				"</body>\n"
				"</html>\n"
				"<!--HTML generated by XXX: todo - put an appropriate reference here-->"
				;
		ui->plainTextEditSourceView->clear();
		ui->plainTextEditSourceView->appendHtml(source);
		QTextCursor c(ui->plainTextEditSourceView->textCursor());
		c.movePosition(QTextCursor::Start);
		if (sourceCodeLocation.lineNumber > 0)
		{
			/* Check if the source code line number is available in the displayed file,
			 * as a crude measure to detect out-of-sync source code files. */
			if (sourceCodeLocation.lineNumber - 1 >= ui->plainTextEditSourceView->document()->blockCount())
				QMessageBox::warning(0, "Source code line number is out of range", QString("Source code line number %1 is out of range.\n"
													"Please, make sure that the source code files match the debug executable.\n"
													"A clean build of the debug executable may be able to fix this warning.").arg(sourceCodeLocation.lineNumber));
			else
				c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, sourceCodeLocation.lineNumber - 1);
		}
		ui->plainTextEditSourceView->setTextCursor(c);
		ui->plainTextEditSourceView->centerCursor();

		sourceCodeViewHighlights.navigatedSourceCodeLine.clear();
		if (sourceCodeLocation.lineNumber > 0)
		{
			/* Highlight current line. */
			c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
			QTextEdit::ExtraSelection selection;
			selection.cursor = c;
			selection.format = sourceCodeViewHighlightFormats.navigatedLine;
			sourceCodeViewHighlights.navigatedSourceCodeLine << selection;
		}
		displayedSourceCodeFile = sourceCodeLocation.fullFileName;
		if (!searchCurrentSourceText(searchData.lastSearchedText))
			refreshSourceCodeView();
		navigationStack.push(sourceCodeLocation);
	}
	setWindowTitle(QString("qgdb: ") + displayedSourceCodeFile);
	ui->plainTextEditSourceView->blockSignals(false);
	sourceFileWatcher.removePaths(sourceFileWatcher.files());
	qDebug() << "Watching file: " << fi.absoluteFilePath();
	sourceFileWatcher.addPath(fi.absoluteFilePath());
}

void MainWindow::connectGdbToGdbServer(void)
{
	if (!isGdbServerConnectedToGdb)
		/*! \todo	Make it possible here that connection errors are handled. */
		sendDataToGdbProcess("-target-select extended-remote localhost:1122\n");
}


class xbtn : public QPushButton
{
public:
	xbtn(const QString & text) : QPushButton(text) {}
	~xbtn(){qDebug() << "xbtn deleted";}
};

void MainWindow::createSvdRegisterView(QTreeWidgetItem *item, int column)
{
	if (item->data(0, SVD_REGISTER_POINTER).isNull())
		return;
	SvdFileParser::SvdRegisterNode * svdRegister = static_cast<SvdFileParser::SvdRegisterNode *>(item->data(0, SVD_REGISTER_POINTER).value<void *>());

	QDialog * dialog = new QDialog();
	QGroupBox * fieldsGroupBox = new QGroupBox();
	SvdRegisterViewData view(dialog, item->data(0, SVD_REGISTER_ADDRESS).toUInt());
	view.fieldsGroupBox = fieldsGroupBox;
	dialog->setAttribute(Qt::WA_DeleteOnClose, true);
	QVBoxLayout * fieldsLayout = new QVBoxLayout();
	fieldsGroupBox->setLayout(fieldsLayout);
	for (const auto & field : svdRegister->fields)
	{
		QHBoxLayout * h = new QHBoxLayout();
		h->addWidget(new QLabel(QString("%1:@bitpos %2:%3 %4").arg(field.name)
					.arg(field.bitOffset).arg(field.bitWidth)
					.arg(field.bitWidth == 1 ? "bit" : "bits")));
		QSpinBox * s = new QSpinBox();
		s->setMinimum(0);
		s->setMaximum((1 << field.bitWidth) - 1);
		if (field.access == "read-only" || svdRegister->access == "read-only")
			s->setEnabled(false);
		h->addWidget(s);
		view.fields << SvdRegisterViewData::RegField(field.bitWidth, field.bitOffset, s);
		fieldsLayout->addLayout(h);

	}
	/* Prevent modifications to the register fields until the register value has been fetched from the target. */
	fieldsGroupBox->setEnabled(false);
	QVBoxLayout * l = new QVBoxLayout(dialog);
	l->addWidget(fieldsGroupBox);
	QHBoxLayout * hbox = new QHBoxLayout;
	QPushButton * b;
	hbox->addWidget(b = new QPushButton("Fetch"));
	connect(b, &QPushButton::clicked, [=] {
		for (const auto & view : svdViews)
			if (view.dialog == dialog)
			{
				QMessageBox::information(0, "", QString("read reg @$%1").arg(view.address, 8, 16, QChar('0')));
				view.fieldsGroupBox->setEnabled(true);
			}
	});
	hbox->addWidget(b = new QPushButton("Close"));
	connect(b, &QPushButton::clicked, [=] {
		dialog->done(QDialog::Accepted);
		int i = 0;
		for (const auto & view : svdViews)
		{
			if (view.dialog == dialog)
				break;
			i ++;
		}
		assert(i < svdViews.size());
		svdViews.removeAt(i);
		});
	l->addLayout(hbox);
	svdViews.append(view);
	dialog->show();
}

void MainWindow::on_lineEditSearchFilesForText_returnPressed()
{
	emit findString(ui->lineEditSearchFilesForText->text(),
			ui->checkBoxSearchForWholeWordsOnly->isChecked() ? StringFinder::SEARCH_FOR_WHOLE_WORDS_ONLY : 0);
	ui->lineEditSearchFilesForText->clear();
}

void MainWindow::on_lineEditObjectLocator_textChanged(const QString &arg1)
{
QString searchPattern = ui->lineEditObjectLocator->text();

	ui->treeWidgetObjectLocator->clear();
	if (searchPattern.size() < MIN_STRING_LENGTH_FOR_OBJECT_LOCATOR)
	{
		ui->treeWidgetObjectLocator->addTopLevelItem(new QTreeWidgetItem(QStringList() << "< enter more text to search for... >"));
		return;
	}
	/* Search for symbols (subprograms, data objects, data types), and file names matching the search pattern.
	 * Keep the sets of items found sorted, for each type of object (subrogram, data object, filename). */
	std::map<QString /* filename */, const SourceFileData *> sourceFileNames;
	std::map<QString /* subprogram name */, QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>> subprograms;
	std::map<QString /* subprogram name */, QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>> dataObjects;
	std::map<QString /* subprogram name */, QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>> dataTypes;
	QList<QTreeWidgetItem *> fileNameItems, dataObjectItems, subprogramItems;
	for (const auto & file : sourceFiles)
	{
		if (file.fileName.contains(searchPattern, Qt::CaseInsensitive))
			sourceFileNames.operator [](file.fileName) = & file;
		for (const auto & subprogram : file.subprograms)
			if (subprogram.name.contains(searchPattern, Qt::CaseInsensitive))
				subprograms.operator [](subprogram.name) =
					QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>(& file.fullFileName, & subprogram);
		for (const auto & dataObject : file.variables)
			if (dataObject.name.contains(searchPattern, Qt::CaseInsensitive))
				dataObjects.operator [](dataObject.name) =
					QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>(& file.fullFileName, & dataObject);
		for (const auto & dataType : file.dataTypes)
			if (dataType.name.contains(searchPattern, Qt::CaseInsensitive))
				dataTypes.operator [](dataType.name) =
					QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>(& file.fullFileName, & dataType);
	}
	/* Populate the object locator view with the items matching the search pattern. */
	for (const auto & file : sourceFileNames)
	{
		QTreeWidgetItem * w = new QTreeWidgetItem(QStringList() << file.first);
		w->setData(0, SourceFileData::FILE_NAME, file.second->fullFileName);
		w->setData(0, SourceFileData::LINE_NUMBER, 0);
		ui->treeWidgetObjectLocator->addTopLevelItem(w);
	}
	for (const auto & dataObject : dataObjects)
	{
		QTreeWidgetItem * w = new QTreeWidgetItem(QStringList() << dataObject.first);
		w->setData(0, SourceFileData::FILE_NAME, * dataObject.second.first);
		w->setData(0, SourceFileData::LINE_NUMBER, dataObject.second.second->line);
		ui->treeWidgetObjectLocator->addTopLevelItem(w);
	}
	for (const auto & subprogram : subprograms)
	{
		QTreeWidgetItem * w = new QTreeWidgetItem(QStringList() << subprogram.first);
		w->setData(0, SourceFileData::FILE_NAME, * subprogram.second.first);
		w->setData(0, SourceFileData::LINE_NUMBER, subprogram.second.second->line);
		ui->treeWidgetObjectLocator->addTopLevelItem(w);
	}
	for (const auto & dataType : dataTypes)
	{
		QTreeWidgetItem * w = new QTreeWidgetItem(QStringList() << dataType.first);
		w->setData(0, SourceFileData::FILE_NAME, * dataType.second.first);
		w->setData(0, SourceFileData::LINE_NUMBER, dataType.second.second->line);
		ui->treeWidgetObjectLocator->addTopLevelItem(w);
	}
}

void MainWindow::on_pushButtonDeleteAllBookmarks_clicked()
{
	bookmarks.clear();
	updateBookmarksView();
	refreshSourceCodeView();
}

void MainWindow::on_pushButtonConnectToBlackmagic_clicked()
{
	blackMagicProbe.connectToProbe();
}

void MainWindow::on_lineEditBlackmagicMonitorCommand_returnPressed()
{
	qDebug() << "sending user monitor packet:" << GdbRemote::monitorRequest(ui->lineEditBlackmagicMonitorCommand->text().toLocal8Bit());
	blackMagicProbe.sendRawGdbPacket(GdbRemote::monitorRequest(ui->lineEditBlackmagicMonitorCommand->text().toLocal8Bit()), true, true);
	ui->lineEditBlackmagicMonitorCommand->clear();
}


PassThroughGdbserver::PassThroughGdbserver(BlackMagicProbe * probe)
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

void PassThroughGdbserver::newConneciton(void)
{
	qDebug() << "gdb client connected";
	/*! \todo	I think there is a race with the 'gdb_client_socket' variable - if a signal arrives between the variable assignment,
	 * 		and the slot connections - fix this. */
	gdb_client_socket = gdb_tcpserver.nextPendingConnection();
	connect(gdb_client_socket, &QTcpSocket::disconnected, [&] { gdb_client_socket->disconnect(); gdb_client_socket = 0; qDebug() << "gdb client disconnected"; });
	connect(gdb_client_socket, SIGNAL(readyRead()), this, SLOT(gdbClientSocketReadyRead()));
}

void PassThroughGdbserver::gdbClientSocketReadyRead(void)
{
	blackmagicProbe->sendRawGdbPacket(gdb_client_socket->readAll());
}

void MainWindow::on_pushButtonDisconnectGdbServer_clicked()
{
	gdbserver->closeConnection();
}

void MainWindow::on_lineEditFindText_returnPressed()
{
	searchCurrentSourceText(ui->lineEditFindText->text());
	ui->plainTextEditSourceView->setFocus();
	moveCursorToNextMatch();
}

void MainWindow::on_pushButtonRequestGdbHalt_clicked()
{
	//gdbProcess->write("\x3");
	blackMagicProbe.sendRawGdbPacket("\x3");
}

void MainWindow::on_pushButtonDumpVarObjects_clicked()
{
	varObjectTreeItemModel.dumpTree();
}

void MainWindow::on_pushButtonLoadProgramToTarget_clicked()
{
	sendDataToGdbProcess("-target-download\n");
}

void MainWindow::on_comboBoxSelectLayout_activated(int index)
{
	if (!index)
		return;

	QList<QDockWidget *> dockWidgets = findChildren<QDockWidget *>();
	for (const auto & d : dockWidgets)
		d->hide();
	switch (index)
	{
	case 2:
		/* Switch to navigation layout. */
		addDockWidget(Qt::BottomDockWidgetArea, ui->dockWidgetSearchResults, Qt::Horizontal);
		ui->dockWidgetSearchResults->setFloating(false);
		ui->dockWidgetSearchResults->show();

		addDockWidget(Qt::LeftDockWidgetArea, ui->dockWidgetSourceFiles, Qt::Vertical);
		ui->dockWidgetSourceFiles->setFloating(false);
		ui->dockWidgetSourceFiles->show();

		splitDockWidget(ui->dockWidgetSourceFiles, ui->dockWidgetBookmarks, Qt::Vertical);
		ui->dockWidgetBookmarks->setFloating(false);
		ui->dockWidgetBookmarks->show();

		splitDockWidget(ui->dockWidgetBookmarks, ui->dockWidgetObjectLocator, Qt::Vertical);
		ui->dockWidgetObjectLocator->setFloating(false);
		ui->dockWidgetObjectLocator->show();
		break;
	case 3:
		/* Switch to debug layout. */
		addDockWidget(Qt::TopDockWidgetArea, ui->dockWidgetBlackmagicToolbox, Qt::Horizontal);
		ui->dockWidgetBlackmagicToolbox->setFloating(false);
		ui->dockWidgetBlackmagicToolbox->show();

		splitDockWidget(ui->dockWidgetBlackmagicToolbox, ui->dockWidgetObjectLocator, Qt::Horizontal);
		ui->dockWidgetObjectLocator->setFloating(false);
		ui->dockWidgetObjectLocator->show();

		splitDockWidget(ui->dockWidgetObjectLocator, ui->dockWidgetBacktrace, Qt::Horizontal);
		ui->dockWidgetBacktrace->setFloating(false);
		ui->dockWidgetBacktrace->show();

		splitDockWidget(ui->dockWidgetBacktrace, ui->dockWidgetBreakpoints, Qt::Horizontal);
		ui->dockWidgetBreakpoints->setFloating(false);
		ui->dockWidgetBreakpoints->show();

		addDockWidget(Qt::LeftDockWidgetArea, ui->dockWidgetDataObjects, Qt::Vertical);
		ui->dockWidgetDataObjects->setFloating(false);
		ui->dockWidgetDataObjects->show();

		splitDockWidget(ui->dockWidgetDataObjects, ui->dockWidgetSourceFiles, Qt::Vertical);
		ui->dockWidgetSourceFiles->setFloating(false);
		ui->dockWidgetSourceFiles->show();

		addDockWidget(Qt::BottomDockWidgetArea, ui->dockWidgetBookmarks, Qt::Horizontal);
		ui->dockWidgetBookmarks->setFloating(false);
		ui->dockWidgetBookmarks->show();

		splitDockWidget(ui->dockWidgetBookmarks, ui->dockWidgetSearchResults, Qt::Horizontal);
		ui->dockWidgetSearchResults->setFloating(false);
		ui->dockWidgetSearchResults->show();

		break;
	}
	ui->comboBoxSelectLayout->setCurrentIndex(0);
}

void MainWindow::on_pushButtonXmlTest_clicked()
{
	svdParser.parse("C:/src/cmsis-svd/data/STMicro/STM32F7x3.svd");
	//svdParser.parse("C:/src/cmsis-svd/data/Atmel/ATSAMD21E15L.svd");
	ui->treeWidgetSvd->clear();

	/* Note: if the device tree node is not added to the tree widget here, but at a later time instead, the
	 * tree node sorting routines below may not work. */
	QTreeWidgetItem * device = new QTreeWidgetItem(ui->treeWidgetSvd, QStringList() << svdParser.device.name << svdParser.device.cpu.name << svdParser.device.description);
	if (!svdParser.device.peripherals.size())
	{
		ui->treeWidgetSvd->addTopLevelItem(device);
		return;
	}
	QTreeWidgetItem * peripherals = new QTreeWidgetItem(device, QStringList() << "Peripherals");

	std::unordered_map<QString, const SvdFileParser::SvdPeripheralNode *> peripheralNodes;
	std::map<QString, std::vector<const SvdFileParser::SvdPeripheralNode *>> peripheralGroups;
	for (const auto & p : svdParser.device.peripherals)
	{
		peripheralNodes.operator [](p.name) = & p;
		if (p.groupName.length())
			peripheralGroups.operator [](p.groupName).push_back(& p);
	}
	/* This is used to remove any excessive whitespace in description strings. */
	QRegularExpression rx("\\s\\s+");
	auto populateRegister = [&] (QTreeWidgetItem * parent, const SvdFileParser::SvdRegisterNode & reg, uint32_t baseAddress) -> void
	{
		uint32_t address = reg.addressOffset + baseAddress;
		QTreeWidgetItem * r = new QTreeWidgetItem(parent, QStringList() << reg.name << QString("0x%1").arg(address, 8, 16, QChar('0'))
							  << QString(reg.description).replace(rx, " "));
		r->setData(0, SVD_REGISTER_POINTER, QVariant::fromValue((void *) & reg));
		r->setData(0, SVD_REGISTER_ADDRESS, address);
		for (const auto & f : reg.fields)
		{
			QStringList fieldHeaders;
			fieldHeaders << f.name;
			fieldHeaders << QString("%1").arg(f.bitOffset);
			if (f.bitWidth > 1)
				fieldHeaders.last().append(QString(":%1").arg(f.bitOffset + f.bitWidth - 1));
			fieldHeaders << QString(f.description).replace(rx, " ");
			new QTreeWidgetItem(r, fieldHeaders);
		}

	};
	auto populatePeripheral = [&] (QTreeWidgetItem * parent, const SvdFileParser::SvdPeripheralNode * peripheral) -> void
	{
		QTreeWidgetItem * p = new QTreeWidgetItem(parent, QStringList() << peripheral->name << QString("0x%1").arg(peripheral->baseAddress, 8, 16, QChar('0')) << peripheral->description);
		const std::vector<SvdFileParser::SvdRegisterNode> & registers = peripheral->registers;

		for (const auto & r : registers)
			populateRegister(p, r, peripheral->baseAddress);
	};
	/* First, populate peripheral groups. */
	for (const auto & p : peripheralGroups)
	{
		QTreeWidgetItem * group = new QTreeWidgetItem(peripherals, QStringList() << p.first);
		for (const auto & peripheral : p.second)
			populatePeripheral(group, peripheral);
		group->sortChildren(0, Qt::AscendingOrder);
	}
	peripherals->sortChildren(0, Qt::AscendingOrder);
	/* Also, add any peripherals that are not part of a peripheral group. */
	for (const auto & p : svdParser.device.peripherals)
	{
		if (p.groupName.length())
			continue;
		QTreeWidgetItem * peripheral = new QTreeWidgetItem(peripherals, QStringList() << p.name << QString("0x%1").arg(p.baseAddress, 8, 16, QChar('0')) << p.description);
		for (const auto & r : p.registers)
			populateRegister(peripheral, r, p.baseAddress);
	}
}

void MainWindow::displayHelp()
{
	QFile f(":/resources/init.txt");
	f.open(QFile::ReadOnly);
	navigationStack.push(SourceCodeLocation(f.fileName()));
	ui->plainTextEditSourceView->setPlainText(f.readAll());
}

void MainWindow::on_pushButtonSendScratchpadToGdb_clicked()
{
	sendDataToGdbProcess(ui->plainTextEditScratchpad->toPlainText());
}
