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

/* !!! TODO: do mainwindow layout saving/selection; that is similar to rts games.
 * It should be possible to be able to quickly save and restore mainwindow layout,
 * the mechanism of which should allow for keystrokes; for example,
 *'ALT + n' for saving layout, and 'CTRL + n' for restoring. */
#include "mainwindow.hxx"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QPainter>

#include <set>

#include "clex/cscanner.hxx"

using namespace ELFIO;

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	ui->setupUi(this);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

	settings = std::make_shared<QSettings>(SETTINGS_FILE_NAME, QSettings::IniFormat);
	restoreState(settings->value(SETTINGS_MAINWINDOW_STATE, QByteArray()).toByteArray());
	restoreGeometry(settings->value(SETTINGS_MAINWINDOW_GEOMETRY, QByteArray()).toByteArray());

	connect(ui->pushButtonTrigger, & QPushButton::clicked, [&] { navigationStack.dump(); });
	connect(ui->pushButtonNavigateForward, & QPushButton::clicked, [&] { if (navigationStack.canNavigateForward()) displaySourceCodeFile(navigationStack.following(), false); });
	connect(ui->pushButtonNavigateBack, & QPushButton::clicked, [&] { if (navigationStack.canNavigateBack()) displaySourceCodeFile(navigationStack.previous(), false); });

	connect(ui->pushButtonVerifyTargetMemory, & QPushButton::clicked, [&] { compareTargetMemory(); });
	connect(ui->pushButtonRequestGdbHalt, & QPushButton::clicked, [&]{ requestTargetHalt(); });
	connect(ui->pushButtonLoadSVDFile, & QPushButton::clicked, [&]{ loadSVDFile(); });

	connect(ui->pushButtonRESTART, & QPushButton::clicked, [&]{
		QApplication::closeAllWindows();
		QProcess::startDetached(QApplication::arguments()[0], QApplication::arguments());
	});

	connect(ui->pushButtonShowCurrentDisassembly, & QPushButton::clicked, [&]
	{
		sendDataToGdbProcess(QString("-data-disassemble -a $pc -- 5\n"));
	});

	connect(ui->lineEditSearchSVDTree, & QLineEdit::returnPressed, [&]
	{
		QString text = ui->lineEditSearchSVDTree->text();
		/* Special case for the empty string - show all items in the tree. */
		if (!text.length())
		{
		}
		std::vector<QTreeWidgetItem *> matchingItems, nonMatchingItems;
		std::function<void(QTreeWidgetItem * item)> scan = [&](QTreeWidgetItem * item) -> void
		{
			item->setHidden(true);
			if (item->text(0).contains(text, Qt::CaseInsensitive))
				matchingItems.push_back(item);
			else
				nonMatchingItems.push_back(item);
			for (int i = 0; i < item->childCount(); scan(item->child(i ++)))
			     ;
		};
		for (int i = 0; i < ui->treeWidgetSvd->topLevelItemCount(); scan(ui->treeWidgetSvd->topLevelItem(i ++)))
		     ;
		for (auto & i : matchingItems)
		{
			QTreeWidgetItem * w = i;
			do w->setHidden(false), w = w->parent(); while (w);
		}
	});

	connect(ui->pushButtonReadMemory, & QPushButton::clicked, [&] {
		unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
								   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_DATA_READ_MEMORY));
		sendDataToGdbProcess(QString("%1-data-read-memory-bytes %2 %3\n")
				     .arg(t)
				     .arg(ui->lineEditMemoryReadAddress->text())
				     .arg(ui->lineEditMemoryReadLength->text()));
		/* By default, enable target memory view auto update. */
		ui->checkBoxMemoryDumpAutoUpdate->setChecked(false);
	});
	targetStateDependentWidgets.enabledWidgetsWhenTargetStopped << ui->dockWidgetContentsMemoryDump;
	targetStateDependentWidgets.disabledWidgetsWhenGdbServerDisconnected << ui->dockWidgetContentsMemoryDump;
	targetStateDependentWidgets.disabledWidgetsWhenTargetDetached << ui->dockWidgetContentsMemoryDump;
	targetStateDependentWidgets.disabledWidgetsWhenTargetRunning << ui->dockWidgetContentsMemoryDump;

	/*****************************************
	 * Settings.
	 *****************************************/
	loadSessions();
	dialogEditSettings = new QDialog(this);
	settingsUi.setupUi(dialogEditSettings);
	connect(ui->pushButtonSettings, & QPushButton::clicked, [&](){
		settingsUi.lineEditGdbExecutable->setText(settings->value(SETTINGS_GDB_EXECUTABLE_FILENAME, "").toString());
		settingsUi.lineEditTargetSVDFileName->setText(targetSVDFileName);
		settingsUi.checkBoxenableNativeDebugging->setChecked(settings->value(SETTINGS_CHECKBOX_ENABLE_NATIVE_DEBUGGING_STATE, false).toBool());
		dialogEditSettings->open();
	});
	connect(settingsUi.pushButtonSelectGdbExecutableFile, & QPushButton::clicked, [&](){
		QString s = QFileDialog::getOpenFileName(0, "Select gdb executable");
		if (!s.isEmpty())
			settingsUi.lineEditGdbExecutable->setText(s);
	});
	connect(settingsUi.pushButtonSelectTargetSVDFile, & QPushButton::clicked, [&](){
		QString s = QFileDialog::getOpenFileName(0, "Select target SVD file", QFileInfo(settingsUi.lineEditTargetSVDFileName->text()).absoluteFilePath());
		if (!s.isEmpty())
			settingsUi.lineEditTargetSVDFileName->setText(s);
	});
	connect(settingsUi.pushButtonSettingsOk, & QPushButton::clicked, [&](){
		settings->setValue(SETTINGS_GDB_EXECUTABLE_FILENAME, settingsUi.lineEditGdbExecutable->text());
		targetSVDFileName = settingsUi.lineEditTargetSVDFileName->text();
		settings->setValue(SETTINGS_CHECKBOX_ENABLE_NATIVE_DEBUGGING_STATE, settingsUi.checkBoxenableNativeDebugging->isChecked());
		dialogEditSettings->hide();
	});
	connect(settingsUi.pushButtonSettingsCancel, & QPushButton::clicked, [&](){ dialogEditSettings->hide(); });
	/*****************************************
	 * End Settings.
	 *****************************************/

	ui->splitterVerticalSourceView->restoreState(settings->value(SETTINGS_SPLITTER_VERTICAL_SOURCE_VIEW_STATE, QByteArray()).toByteArray());
	ui->splitterHorizontalSourceView->restoreState(settings->value(SETTINGS_SPLITTER_HORIZONTAL_SOURCE_VIEW_STATE, QByteArray()).toByteArray());
	ui->splitterHorizontalGdbConsoles->restoreState(settings->value(SETTINGS_SPLITTER_HORIZONTAL_GDB_CONSOLES_STATE, QByteArray()).toByteArray());

	bool flag;

	ui->splitterHorizontalGdbConsoles->setVisible(flag = settings->value(SETTINGS_IS_SPLITTER_HORIZONTAL_GDB_CONSOLES_VISIBLE, true).toBool());
	ui->checkBoxShowGdbConsoles->setCheckState(flag ? Qt::Checked : Qt::Unchecked);
	connect(ui->checkBoxShowGdbConsoles, & QCheckBox::stateChanged, [&](int newState) { ui->splitterHorizontalGdbConsoles->setVisible(newState != Qt::Unchecked); });
	connect(ui->pushButtonHideGdbConsoles, & QPushButton::clicked, [&] { ui->checkBoxShowGdbConsoles->setCheckState(Qt::Unchecked); });

	ui->groupBoxDisassembly->setVisible(flag = settings->value(SETTINGS_IS_DISASSEMBLY_VIEW_VISIBLE, true).toBool());
	ui->checkBoxShowDisassembly->setCheckState(flag ? Qt::Checked : Qt::Unchecked);
	connect(ui->checkBoxShowDisassembly, & QCheckBox::stateChanged, [&](int newState) { ui->groupBoxDisassembly->setVisible(newState != Qt::Unchecked); });
	connect(ui->pushButtonHideDisassembly, & QPushButton::clicked, [&] { ui->checkBoxShowDisassembly->setCheckState(Qt::Unchecked); });

	ui->groupBoxTargetOutput->setVisible(flag = settings->value(SETTINGS_IS_TARGET_OUTPUT_VIEW_VISIBLE, true).toBool());
	ui->checkBoxShowTargetOutput->setCheckState(flag ? Qt::Checked : Qt::Unchecked);
	connect(ui->checkBoxShowTargetOutput, & QCheckBox::stateChanged, [&](int newState) { ui->groupBoxTargetOutput->setVisible(newState != Qt::Unchecked); });
	connect(ui->pushButtonHideTargetOutputView, & QPushButton::clicked, [&] { ui->checkBoxShowTargetOutput->setCheckState(Qt::Unchecked); });

	ui->plainTextEditGdbLog->setMaximumBlockCount(MAX_GDB_LINE_COUNT_IN_GDB_LIMITING_MODE);
	connect(ui->checkBoxLimitGdbLog, & QCheckBox::stateChanged, [&](int newState)
		{
			ui->plainTextEditGdbLog->setMaximumBlockCount((newState == Qt::Checked) ? MAX_GDB_LINE_COUNT_IN_GDB_LIMITING_MODE : 0);
		});
	ui->checkBoxLimitGdbLog->setChecked(settings->value(SETTINGS_CHECKBOX_GDB_OUTPUT_LIMITING_MODE_STATE, true).toBool());

	gdbProcess = std::make_shared<QProcess>();
	/*! \todo This doesn't need to live in a separate thread. */
	gdbMiReceiver = new GdbMiReceiver();
	gdbMiReceiver->moveToThread(&gdbMiReceiverThread);

	//connect(gdbProcess.get(), SIGNAL(readyReadStandardOutput()), gdbMiReceiver, SLOT(gdbInputAvailable()));
	connect(gdbProcess.get(), & QProcess::readyReadStandardOutput, [&] { emit readyReadGdbProcess(gdbProcess->readAll()); });
	connect(this, SIGNAL(readyReadGdbProcess(QByteArray)), gdbMiReceiver, SLOT(gdbInputAvailable(QByteArray)));

	connect(ui->pushButtonShowWindow, & QPushButton::clicked, [&]
		{ QContextMenuEvent event(QContextMenuEvent::Other, QPoint(0, 0)); QApplication::sendEvent(ui->mainToolBar, & event); });

	connect(gdbProcess.get(), & QProcess::readyReadStandardError,
		[=] { QTextCursor c = ui->plainTextEditStderr->textCursor(); c.movePosition(QTextCursor::End); c.insertText(gdbProcess.get()->readAllStandardError()); });

	/* It is possible that the user presses quickly the button for starting gdb, before the gdb process is started and the
	 * button is disabled, so only start the gdb process here if it is not already started or running. */
	connect(ui->pushButtonStartGdb, & QPushButton::clicked, [&] {
		if (gdbProcess->state() == QProcess::NotRunning)
		{
			gdbProcess->setProgram(settings->value(SETTINGS_GDB_EXECUTABLE_FILENAME, "").toString());
			gdbProcess->start();
		}
	});

	connect(gdbProcess.get(), & QProcess::started, [&] {
		ui->pushButtonConnectGdbToGdbServer->setEnabled(true); ui->pushButtonStartGdb->setEnabled(false);
	});
	connect(gdbProcess.get(), SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(gdbProcessFinished(int,QProcess::ExitStatus)));
	connect(gdbProcess.get(), SIGNAL(errorOccurred(QProcess::ProcessError)), this, SLOT(gdbProcessError(QProcess::ProcessError)));
	connect(gdbMiReceiver, SIGNAL(gdbMiOutputLineAvailable(QString)), this, SLOT(gdbMiLineAvailable(QString)));
	gdbMiReceiverThread.start();

	connect(&varObjectTreeItemModel, SIGNAL(readGdbVarObjectChildren(const QString)), this, SLOT(readGdbVarObjectChildren(const QString)));
	ui->treeViewDataObjects->setModel(&varObjectTreeItemModel);

	/* Forcing the rows of the tree view to be of uniform height enables some optimizations, which
	 * makes a significant difference when a large number of items is displayed. */
	ui->treeViewDataObjects->setUniformRowHeights(true);

#if KEEP_THIS_FOR_DOCUMENTATION_PURPOSES
	/* WARNING: this commented out code is kept here to warn that setting the header of a tree view to
	 * automatically resize according to the column contents can be *VERY* slow. It is actually not
	 * doing exactly what is expected. Do not use code like this! */
	ui->treeViewDataObjects->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ui->treeViewDataObjects->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->treeViewDataObjects->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	ui->treeViewDataObjects->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
#endif

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
	ui->checkBoxShowFullFileNames->setChecked(settings->value(SETTINGS_CHECKBOX_SHOW_FULL_FILE_NAME_STATE, false).toBool());

	connect(ui->checkBoxShowOnlySourcesWithMachineCode, SIGNAL(stateChanged(int)), this, SLOT(updateSourceListView()));
	connect(ui->checkBoxShowOnlyExistingSourceFiles, SIGNAL(stateChanged(int)), this, SLOT(updateSourceListView()));
	ui->checkBoxShowOnlySourcesWithMachineCode->setChecked(settings->value(SETTINGS_CHECKBOX_SHOW_ONLY_SOURCES_WITH_MACHINE_CODE_STATE, false).toBool());
	ui->checkBoxShowOnlyExistingSourceFiles->setChecked(settings->value(SETTINGS_CHECKBOX_SHOW_ONLY_EXISTING_SOURCE_FILES, false).toBool());


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
		{ if (column != TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER) showSourceCode(item); } );
	connect(ui->treeWidgetBreakpoints, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ if (column != TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER) showSourceCode(item); } );

	QFontDatabase::addApplicationFont(":/fonts/resources/Hack-Regular.ttf");
	setStyleSheet(QString(

/*! \todo !!! This is reported as a workaround to activate hover events for splitter handles !!!
 * https://bugreports.qt.io/browse/QTBUG-13768 */

"QSplitterHandle:hover {}\n"

"QSplitter::handle:horizontal {\n"
	"width: 5px;\n"
	"background: grey;\n"
"}\n"

"QSplitter::handle:horizontal:hover {\n"
	"background: cyan;\n"
"}\n"

"QSplitter::handle:vertical {\n"
	"height: 5px;\n"
	"background: orange;\n"
"}\n"

"QSplitter::handle:vertical:hover {\n"
	"background: cyan;\n"
"}\n"

"QPlainTextEdit {\n"
	"font: 10pt 'Hack';\n"
"}\n"

/* Horizontal scroll bars. */
#if 1
"QScrollBar:horizontal {\n"
    "border: 2px solid grey;\n"
    "background: #c0c0c0;\n"
    "height: 15px;\n"
    "margin: 0px 20px 0 20px;\n"
"}\n"
"QScrollBar::handle:horizontal {\n"
    "background: white;\n"
    "min-width: 20px;\n"
"}\n"
"QScrollBar::add-line:horizontal {\n"
    "border: 2px solid grey;\n"
    "background: #c0c0c0;\n"
    "width: 20px;\n"
    "subcontrol-position: right;\n"
    "subcontrol-origin: margin;\n"
"}\n"
"\n"
"QScrollBar::sub-line:horizontal {\n"
    "border: 2px solid grey;\n"
    "background: #c0c0c0;\n"
    "width: 20px;\n"
    "subcontrol-position: left;\n"
    "subcontrol-origin: margin;\n"
"}\n"
#endif
/* Vertical scroll bars. */

"QScrollBar:vertical {\n"
    "border: 2px solid grey;\n"
    "background: #c0c0c0;\n"
    "width: 15px;\n"
    "margin: 22px 0 22px 0;\n"
"}\n"
"QScrollBar::handle:vertical {\n"
    "background: white;\n"
    "min-height: 20px;\n"
"}\n"
"QScrollBar::add-line:vertical {\n"
    "border: 2px solid grey;\n"
    "background: #c0c0c0;\n"
    "height: 20px;\n"
    "subcontrol-position: bottom;\n"
    "subcontrol-origin: margin;\n"
"}\n"
"\n"
"QScrollBar::sub-line:vertical {\n"
    "border: 2px solid grey;\n"
    "background: #c0c0c0;\n"
    "height: 20px;\n"
    "subcontrol-position: top;\n"
    "subcontrol-origin: margin;\n"
"}\n"



"QMainWindow::separator {\n"
    "background: white;\n"
    "width: 5px; /* when vertical */\n"
    "height: 5px; /* when horizontal */\n"
"}\n"
"\n"
"QMainWindow::separator:hover {\n"
    "background: grey;\n"
"}\n"

#if 0
"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {\n"
    "border: 2px solid grey;\n"
    "width: 3px;\n"
    "height: 3px;\n"
    "background: white;\n"
"}\n"
"\n"
"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {\n"
    "background: none;\n"
"}\n"
#endif
			      ));

	//gdbProcess->start("arm-none-eabi-gdb.exe", QStringList() << "--interpreter=mi3");
	//gdbProcess->start("c:/src1/gdb-10.1-build/gdb/gdb.exe", QStringList() << "--interpreter=mi3");
	//gdbProcess->start("xxx", QStringList() << "--interpreter=mi3");
	//gdbProcess->start("c:/src1/gdb-10.1-build-1/gdb/gdb.exe", QStringList() << "--interpreter=mi3");
	//gdbProcess->start("c:/src1/gdb-10.1-build-2/gdb/gdb.exe", QStringList() << "--interpreter=mi3");
	ui->plainTextEditScratchpad->setPlainText(settings->value(SETTINGS_SCRATCHPAD_TEXT_CONTENTS, QString("Lorem ipsum dolor sit amet")).toString());

	ui->plainTextEditSourceView->installEventFilter(this);
	ui->plainTextEditSourceView->viewport()->installEventFilter(this);
	/*! \todo	Only execute the code below after gdb has been successfully started. */
	connect(gdbProcess.get(), & QProcess::started, [&] {
		QFileInfo f(settings->value(SETTINGS_LAST_LOADED_EXECUTABLE_FILE, QString()).toString());
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
			f = QFileInfo(settings->value(SETTINGS_LAST_LOADED_EXECUTABLE_FILE, QString()).toString());
			f = QFileInfo(QFileDialog::getOpenFileName(0, "Load executable for debugging", f.canonicalPath()));
			if (!f.canonicalFilePath().isEmpty())
			{
reopen_last_file:
				unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
										   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_EXECUTABLE_SYMBOL_FILE_LOADED,
										   f.canonicalFilePath())
									   );
				sendDataToGdbProcess("-gdb-set tcp auto-retry off\n");
				sendDataToGdbProcess("-gdb-set mem inaccessible-by-default off\n");
				sendDataToGdbProcess("-gdb-set print elements unlimited\n");
				sendDataToGdbProcess(QString("%1-file-exec-and-symbols \"%2\"\n").arg(t).arg(f.canonicalFilePath()));
			}
		}
	});
	gdbProcess->start(settings->value(SETTINGS_GDB_EXECUTABLE_FILENAME, "").toString(), QStringList() << "--interpreter=mi3");

	ui->treeWidgetBreakpoints->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetBreakpoints, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(breakpointsContextMenuRequested(QPoint)));

	ui->treeWidgetBookmarks->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetBookmarks, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(bookmarksContextMenuRequested(QPoint)));

	ui->treeWidgetSvd->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetSvd, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(svdContextMenuRequested(QPoint)));

	/* Unified custom menu processing for source items. */
	ui->treeWidgetObjectLocator->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetObjectLocator, &QTreeWidget::customContextMenuRequested, [=] (QPoint p) -> void { sourceItemContextMenuRequested(ui->treeWidgetObjectLocator, p); });
	ui->treeWidgetDataTypes->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetDataTypes, &QTreeWidget::customContextMenuRequested, [=] (QPoint p) -> void { sourceItemContextMenuRequested(ui->treeWidgetDataTypes, p); });
	ui->treeWidgetStaticDataObjects->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetStaticDataObjects, &QTreeWidget::customContextMenuRequested, [=] (QPoint p) -> void { sourceItemContextMenuRequested(ui->treeWidgetStaticDataObjects, p); });
	ui->treeWidgetSubprograms->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetSubprograms, &QTreeWidget::customContextMenuRequested, [=] (QPoint p) -> void { sourceItemContextMenuRequested(ui->treeWidgetSubprograms, p); });
	ui->treeWidgetSourceFiles->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetSourceFiles, &QTreeWidget::customContextMenuRequested, [&] (QPoint p) -> void { sourceItemContextMenuRequested(ui->treeWidgetSourceFiles, p); });

	/* Use this for handling changes to the breakpoint enable/disable checkbox modifications. */
	connect(ui->treeWidgetBreakpoints, SIGNAL(itemChanged(QTreeWidgetItem*,int)), this, SLOT(breakpointViewItemChanged(QTreeWidgetItem*,int)));

	highlightFormats.navigatedLine.setProperty(QTextFormat::FullWidthSelection, true);
	highlightFormats.navigatedLine.setBackground(QBrush(Qt::gray));
	highlightFormats.enabledBreakpoint.setProperty(QTextFormat::FullWidthSelection, true);
	highlightFormats.enabledBreakpoint.setBackground(QBrush(Qt::red));
	highlightFormats.disabledBreakpoint.setProperty(QTextFormat::FullWidthSelection, true);
	highlightFormats.disabledBreakpoint.setBackground(QBrush(Qt::darkRed));
	highlightFormats.currentLine.setProperty(QTextFormat::FullWidthSelection, true);
	highlightFormats.currentLine.setBackground(QBrush(Qt::lightGray));
	highlightFormats.bookmark.setProperty(QTextFormat::FullWidthSelection, true);
	highlightFormats.bookmark.setBackground(QBrush(Qt::darkCyan));
	highlightFormats.currentDisassemblyLine.setProperty(QTextFormat::FullWidthSelection, true);
	highlightFormats.currentDisassemblyLine.setBackground(QBrush(Qt::cyan));
	highlightFormats.searchedText.setBackground(QBrush(Qt::yellow));

	connect(ui->plainTextEditSourceView, &QPlainTextEdit::cursorPositionChanged, [=]()
		{
			QTextCursor c(ui->plainTextEditSourceView->textCursor());
			c.movePosition(QTextCursor::StartOfBlock);
			c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
			sourceCodeViewHighlights.currentSourceCodeLine.clear();
			QTextEdit::ExtraSelection s;
			s.cursor = c;
			s.format = highlightFormats.currentLine;
			sourceCodeViewHighlights.currentSourceCodeLine << s;
			refreshSourceCodeView();
		});

	QString targetFilesBaseDirectory =
			"C:/src/build-troll-Desktop_Qt_5_12_0_MinGW_64_bit-Debug/troll-test-drive-files/";
	targetCorefile = new TargetCorefile(targetFilesBaseDirectory + "flash.bin", 0x08000000,
					    targetFilesBaseDirectory + "ram.bin", 0x20000000,
					    targetFilesBaseDirectory + "registers.bin");
	////gdbserver = new GdbServer(targetCorefile);

	QStringList traceLog = settings->value(SETTINGS_TRACE_LOG, QStringList()).toStringList();
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
		ui->treeWidgetTraceLog->addTopLevelItem(createNavigationWidgetItem(t, t.at(2), lineNumber));
	}

	connect(ui->checkBoxShowFullFileNamesInTraceLog, & QCheckBox::stateChanged, [&](int newState) { ui->treeWidgetTraceLog->setColumnHidden(2, newState == 0); });
	ui->checkBoxShowFullFileNamesInTraceLog->setChecked(settings->value(SETTINGS_CHECKBOX_SHOW_FULL_FILE_NAME_IN_TRACE_LOG_STATE, false).toBool());

	connect(ui->treeWidgetTraceLog, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );
	connect(ui->treeWidgetTraceLog, & QTreeWidget::currentItemChanged, [=] (QTreeWidgetItem * current, QTreeWidgetItem * previous)
		{ showSourceCode(current); } );
	connect(ui->treeWidgetTraceLog, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ showSourceCode(item); } );

	connect(& blackMagicProbeServer, & BlackMagicProbeServer::BlackMagicProbeConnected,
		[&] {
			ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: lawngreen");
			ui->pushButtonConnectToBlackmagic->setText(tr("Blackmagic connected"));
			ui->groupBoxBlackMagicDisconnectedWidgets->setEnabled(false);
			ui->groupBoxBlackMagicConnectedWidgets->setEnabled(true);
			ui->pushButtonConnectGdbToGdbServer->click();
			});
	connect(& blackMagicProbeServer, & BlackMagicProbeServer::BlackMagicProbeDisconnected,
		[&] {
			ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: yellow");
			ui->pushButtonConnectToBlackmagic->setText(tr("Connect to blackmagic"));
			ui->groupBoxBlackMagicDisconnectedWidgets->setEnabled(true);
			ui->groupBoxBlackMagicConnectedWidgets->setEnabled(false);
			if (target_state != GDBSERVER_DISCONNECTED)
				sendDataToGdbProcess("-target-disconnect\n");
			});
	ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: yellow");
	connect(ui->pushButtonConnectToBlackmagic, & QPushButton::clicked, [&] { blackMagicProbeServer.connectToProbe(); });

	connect(ui->pushButtonDisplayHelp, SIGNAL(clicked(bool)), this, SLOT(displayHelp()));
	displayHelp();

	/***************************************
	 * Gdb and target state change handling.
	 ***************************************/
	 /*! \todo: Move this to a separate function */
	targetStateDependentWidgets.enabledWidgetsWhenGdbServerDisconnected << ui->groupBoxTargetDisconnected;
	targetStateDependentWidgets.disabledWidgetsWhenGdbServerDisconnected << ui->groupBoxTargetConnected;
	connect(this, &MainWindow::gdbServerConnected, [&] {
		////QMessageBox::information(0, "Gdb connection established", "Gdb successfully connected to remote gdb server");
		ui->pushButtonScanForTargets->click();
	});

	connect(&blackMagicProbeServer, &BlackMagicProbeServer::GdbClientDisconnected, [&]
		{ targetStateDependentWidgets.enterTargetState(target_state = GDBSERVER_DISCONNECTED);}
	);

	targetStateDependentWidgets.enabledWidgetsWhenTargetStopped << ui->groupBoxTargetHalted << ui->groupBoxTargetConnected;
	targetStateDependentWidgets.disabledWidgetsWhenTargetStopped << ui->groupBoxTargetRunning;
	connect(this, &MainWindow::targetStopped, [&] {
		if (target_state == GDBSERVER_DISCONNECTED || target_state == TARGET_DETACHED)
			compareTargetMemory();
		targetStateDependentWidgets.enterTargetState(target_state = TARGET_STOPPED);
		/*! \todo Make the frame limits configurable. */
		sendDataToGdbProcess("-stack-list-frames 0 100\n");
		if (!targetRegisterIndices.size())
			sendDataToGdbProcess("-data-list-register-names\n");
		sendDataToGdbProcess("-stack-info-frame\n");
	});

	connect(this, &MainWindow::targetCallStackFrameChanged, [&] {
		sendDataToGdbProcess("-data-list-register-values x\n");
		sendDataToGdbProcess("-var-update --all-values *\n");
		sendDataToGdbProcess("-stack-list-variables --all-values\n");

		unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
								   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_UPDATE_LAST_KNOWN_PROGRAM_COUNTER));

		sendDataToGdbProcess(QString("%1-data-evaluate-expression \"(unsigned) $pc\"\n").arg(t));
		if (ui->checkBoxAutoUpdateDisassembly->isChecked())
			ui->pushButtonShowCurrentDisassembly->click();
	});

	targetStateDependentWidgets.enabledWidgetsWhenTargetRunning << ui->groupBoxTargetRunning << ui->groupBoxTargetConnected;
	targetStateDependentWidgets.disabledWidgetsWhenTargetRunning << ui->groupBoxTargetHalted;
	connect(this, &MainWindow::targetRunning, [&] {
		targetStateDependentWidgets.enterTargetState(target_state = TARGET_RUNNING);
	});

	targetStateDependentWidgets.disabledWidgetsWhenTargetDetached << targetStateDependentWidgets.enabledWidgetsWhenTargetRunning;
	targetStateDependentWidgets.disabledWidgetsWhenTargetDetached << targetStateDependentWidgets.enabledWidgetsWhenTargetStopped;
	connect(this, &MainWindow::targetDetached, [&]
		{ targetStateDependentWidgets.enterTargetState(target_state = TARGET_DETACHED);}
	);


	targetStateDependentWidgets.enterTargetState(target_state = GDBSERVER_DISCONNECTED);
	/***************************************
	 ***************************************
	 ***************************************/

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

	connect(ui->lineEditGdbCommand1, & QLineEdit::returnPressed, [&] { sendCommandsToGdb(ui->lineEditGdbCommand1); });
	connect(ui->pushButtonSendCommandToGdb1, & QPushButton::clicked, [&] { sendCommandsToGdb(ui->lineEditGdbCommand1); });
	connect(ui->lineEditGdbCommand2, & QLineEdit::returnPressed, [&] { sendCommandsToGdb(ui->lineEditGdbCommand2); });
}

void MainWindow::loadSessions()
{
	QList<QVariant> v = settings->value(SETTINGS_SAVED_SESSIONS, QList<QVariant>()).toList();
	for (const auto & s : v)
		sessions << SessionState::fromQVariant(s);
}

void MainWindow::restoreSession(const QString &executableFileName)
{
	for (const auto & session : sessions)
	{
		if (session.executableFileName != executableFileName)
			continue;
		targetSVDFileName = session.targetSVDFileName;
		/* Load bookmarks. */
		for (const auto & bookmark : session.bookmarks)
		{
			QStringList bookmarkData = bookmark.split('\n');
			if (bookmarkData.size() != 2)
				continue;
			bookmarks << SourceCodeLocation(bookmarkData.at(0), bookmarkData.at(1).toInt());
		}
		updateBookmarksView();
		/* Attempt to restore breakpoints. */
		for (const auto & b : session.breakpoints)
			if (!b.isEmpty())
				sendDataToGdbProcess(QString("b %1\n").arg(b));

		break;
	}
	isSessionRestored = true;
}

void MainWindow::saveSessions()
{
	if (!isSessionRestored)
		/* Only update the list of sessions if a session has been previoulsly restored. Otherwise, sessions may get wiped out. */
		return;
	/* Maintain the list of sessions in a least-recently-used order. */
	struct SessionState s;
	s.executableFileName = settings->value(SETTINGS_LAST_LOADED_EXECUTABLE_FILE, QString()).toString();
	s.targetSVDFileName = targetSVDFileName;
	int i;
	for (i = 0; i < ui->treeWidgetBreakpoints->topLevelItemCount(); i ++)
		s.breakpoints << ui->treeWidgetBreakpoints->topLevelItem(i)->text(5);
	for (const auto & bookmark : bookmarks)
		s.bookmarks << QString("%1\n%2").arg(bookmark.fullFileName).arg(bookmark.lineNumber);
	/* Override the session information for the currently loaded executable file, if it exists in the list of saved sessions. */
	sessions.removeAll(s);
	sessions.prepend(s);
	/* Trim the oldest sessions in the list, if too many sessions are already in the session list. */
	while (sessions.length() > MAX_KEPT_SESSIONS)
		sessions.removeLast();
	QList<QVariant> v;
	for (const auto & s : sessions)
		v << s.toVariant();
	settings->setValue(SETTINGS_SAVED_SESSIONS, v);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (navigatorModeActivated)
	{
		QWidget::closeEvent(event);
		/*! \todo	This should not be needed, remove it after checking. */
		return;
	}

	/* Close any svd register view dialogs. */
	for (const auto & v : svdViews)
		v.dialog->done(QDialog::Accepted);

	saveSessions();

	settings->setValue(SETTINGS_MAINWINDOW_STATE, saveState());
	settings->setValue(SETTINGS_MAINWINDOW_GEOMETRY, saveGeometry());
	settings->setValue(SETTINGS_CHECKBOX_SHOW_FULL_FILE_NAME_STATE, ui->checkBoxShowFullFileNames->isChecked());
	settings->setValue(SETTINGS_CHECKBOX_SHOW_FULL_FILE_NAME_IN_TRACE_LOG_STATE, ui->checkBoxShowFullFileNamesInTraceLog->isChecked());
	settings->setValue(SETTINGS_CHECKBOX_SHOW_ONLY_SOURCES_WITH_MACHINE_CODE_STATE, ui->checkBoxShowOnlySourcesWithMachineCode->isChecked());
	settings->setValue(SETTINGS_CHECKBOX_SHOW_ONLY_EXISTING_SOURCE_FILES, ui->checkBoxShowOnlyExistingSourceFiles->isChecked());
	settings->setValue(SETTINGS_SCRATCHPAD_TEXT_CONTENTS, ui->plainTextEditScratchpad->document()->toPlainText());

	settings->setValue(SETTINGS_SPLITTER_VERTICAL_SOURCE_VIEW_STATE, ui->splitterVerticalSourceView->saveState());
	settings->setValue(SETTINGS_SPLITTER_HORIZONTAL_GDB_CONSOLES_STATE, ui->splitterHorizontalGdbConsoles->saveState());
	settings->setValue(SETTINGS_SPLITTER_HORIZONTAL_SOURCE_VIEW_STATE, ui->splitterHorizontalSourceView->saveState());

	settings->setValue(SETTINGS_IS_SPLITTER_HORIZONTAL_GDB_CONSOLES_VISIBLE, ui->splitterHorizontalGdbConsoles->isVisible());
	settings->setValue(SETTINGS_IS_DISASSEMBLY_VIEW_VISIBLE, ui->groupBoxDisassembly->isVisible());
	settings->setValue(SETTINGS_IS_TARGET_OUTPUT_VIEW_VISIBLE, ui->groupBoxTargetOutput->isVisible());

	settings->setValue(SETTINGS_CHECKBOX_GDB_OUTPUT_LIMITING_MODE_STATE, ui->checkBoxLimitGdbLog->isChecked());

	QStringList traceLog;
	for (int i = 0; i < ui->treeWidgetTraceLog->topLevelItemCount(); i ++)
	{
		const QTreeWidgetItem * l = ui->treeWidgetTraceLog->topLevelItem(i);
		traceLog << l->text(0) + '|' + l->text(1) + '|' + l->text(2);
	}
	settings->setValue(SETTINGS_TRACE_LOG, traceLog);
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
				ui->pushButtonNavigateBack->click();
				result = true;
			}
			break;
		case Qt::Key_Right:
			if (e->modifiers() & Qt::AltModifier)
			{
				ui->pushButtonNavigateForward->click();
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
				sendDataToGdbProcess("-exec-next\n");
			result = true;
			break;
		case Qt::Key_C:
			if (target_state == TARGET_STOPPED)
				sendDataToGdbProcess("c\n");
			else if (e->modifiers() == Qt::ControlModifier)
				requestTargetHalt();
			result = true;
			break;
		case Qt::Key_Space:
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
				}
				/* Reread the list of breakpoints. */
				sendDataToGdbProcess("-break-list\n");
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
	blackMagicProbeServer.disconnect();
	gdbMiReceiverThread.quit();
	gdbProcess->disconnect();
	gdbProcess->kill();
	gdbMiReceiverThread.wait();
	gdbProcess->waitForFinished();

	fileSearchThread.quit();
	fileSearchThread.wait();

	delete ui;
}

/*! \todo	The 'line' argument should be handled better; it makes a difference for large input sizes; as a minimum,
 * 		it should be 'const' */
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
		/* Console stream output. */
			appendLineToGdbLog(normalizeGdbString(line.right(line.length() - 1)));
			break;
		case '&':
		/* Log stream output. */
			ui->plainTextEditLogStreamOutput->appendPlainText(normalizeGdbString(line.right(line.length() - 1)));
			break;
		case '*':
		/* Exec-async output. */
		case '^':
		/* Result record. */
			appendLineToGdbLog((tokenNumber ? QString("%1").arg(tokenNumber) : QString()) + line);
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
				if (handleTargetScanResponse(result, results, tokenNumber))
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
				if (handleValueResponse(result, results, tokenNumber))
					break;
				if (handleVerifyTargetMemoryContentsSeqPoint(result, results, tokenNumber))
					break;
				if (handleMemoryResponse(result, results, tokenNumber))
					break;
				switch (result)
				{
					case GdbMiParser::DONE:
						break;
					case GdbMiParser::ERROR:
				{
					/* \todo	Remove the 'gdbTokenContext' for the current token number, if not already removed above. */
					QMessageBox::critical(0, "Gdb error", QString("Gdb error:\n%1").arg(gdbErrorString(result, results)));
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
		case '@':
		/* Target stream output - put it along with the console output, capturing it for later
		 * processing, if necessary. */
			targetDataCapture.captureLine(line.right(line.length() - 1));
			/* FALLTHROUGH */
		default:
			appendLineToGdbLog(line);
			break;
		case '=':
			appendLineToGdbLog(line);
			/* Handle gdb 'notify-async-output' records. */
			/*! \todo	If the number of cases here grows too much,
				 *		extract this into a separate function. */
			if (line.startsWith("=breakpoint-created") || line.startsWith("=breakpoint-modified")
					|| line.startsWith("=breakpoint-deleted"))
				sendDataToGdbProcess("-break-list\n");
			else if (line.startsWith("=thread-group-started"))
			{
				QRegularExpression rx("=thread-group-started,id=\"(.+)\",pid=\"(.+)\"");
				QRegularExpressionMatch match = rx.match(line);
				if (!match.hasMatch())
					QMessageBox::critical(0, "Error parsing gdb notify async response",
							      "Failed to parse gdb '=thread-group-started' response");
				else
					debugProcessId = match.captured(2).toULong(0, 0);
			}
			else if (line.startsWith("=thread-group-exited"))
			{
				emit targetDetached();
				QMessageBox::information(0, "Target detached", "Gdb has detached from the target");
			}
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

void MainWindow::appendLineToGdbLog(const QString &data)
{
	if (ui->checkBoxLimitGdbLog->isChecked() && data.length() > (MAX_LINE_LENGTH_IN_GDB_LOG_LIMITING_MODE << 1))
	{
		ui->plainTextEditGdbLog->appendPlainText(data.left(MAX_LINE_LENGTH_IN_GDB_LOG_LIMITING_MODE));
		ui->plainTextEditGdbLog->appendPlainText("... <truncated, gdb log limiting active>");
		return;
	}
	ui->plainTextEditGdbLog->appendPlainText(data);
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

	struct GdbTokenContext::GdbResponseContext context = gdbTokenContext.readAndRemoveContext(tokenNumber);
	QModelIndex index = varObjectTreeItemModel.indexForMiVariableName(context.s);
	if (!index.isValid())
	{
		/* If this case is reached, this means that a gdb "-var-list-children" machine interface
		 * command was issued to gdb, to list the children of some variable object, but when
		 * the response is received, and processed here, the variable object, for which the
		 * "-var-list-children" request was issued, no longer exists. Not a very common case,
		 * but possible. */
		return true;
	}

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
	varObjectTreeItemModel.childrenFetched(index, children);
	return true;
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
					/* Symbols found for a file, which was not reported by gdb in the list of source code files
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
					sourceFiles.operator [](fullFileName).subprograms.insert(symbols.cbegin(), symbols.cend());
				else if (context.gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_VARIABLE_SYMBOLS)
					sourceFiles.operator [](fullFileName).variables.insert(symbols.cbegin(), symbols.cend());
				else
					sourceFiles.operator [](fullFileName).dataTypes.insert(symbols.cbegin(), symbols.cend());
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

	settings->setValue(SETTINGS_LAST_LOADED_EXECUTABLE_FILE, context.s);
	elfReader = std::make_shared<elfio>();
	if (!elfReader->load(context.s.toStdString()))
		elfReader.reset();
	restoreSession(context.s);
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
		breakpointCache.rebuildCache(breakpoints);
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
		ui->treeWidgetBacktrace->addTopLevelItem(createNavigationWidgetItem(
			 QStringList()
				 << QString("%1").arg(frame.level)
				 << frame.subprogramName
				 << frame.fileName
				 << QString("%1").arg(frame.lineNumber)
				 << QString("$%1").arg(frame.pcAddress, 8, 16, QChar('0')),
			 frame.fullFileName,
			 frame.lineNumber));

	if (ui->checkBoxEnableTraceLogging->isChecked() && backtrace.size())
	{
		StackFrameData frame = backtrace.at(0);
		ui->treeWidgetTraceLog->addTopLevelItem(createNavigationWidgetItem(
				QStringList() << frame.fileName << QString("%1").arg(frame.lineNumber) << frame.fullFileName,
				frame.fullFileName,
				frame.lineNumber));
	}
	return true;
}

bool MainWindow::handleRegisterNamesResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MIList * registerNames;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "register-names" || !(registerNames = results.at(0).value->asList()))
		return false;
	targetRegisterIndices.clear();
	ui->treeWidgetRegisters->clear();
	int index = 0;
	for (const auto & r : registerNames->values)
	{
		const GdbMiParser::MIConstant * t;
		if ((t = r->asConstant()))
		{
			targetRegisterIndices << index;
			QString registerName = QString::fromStdString(t->constant());
			if (registerName.length())
			{
				ui->treeWidgetRegisters->addTopLevelItem(new QTreeWidgetItem(QStringList() << registerName));
				index ++;
			}
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
		if (registerNumber < targetRegisterIndices.length())
		{
			int index = targetRegisterIndices.at(registerNumber);
			enum Qt::GlobalColor foregroundColor = Qt::black;
			/* Highlight registers whose value changed since they were last updated. */
			if (ui->treeWidgetRegisters->topLevelItem(index)->text(1) != registerValue)
				foregroundColor = Qt::red;
			ui->treeWidgetRegisters->topLevelItem(index)->setText(1, registerValue);
			ui->treeWidgetRegisters->topLevelItem(index)->setForeground(1, foregroundColor);
		}
	}
	return false;
}

bool MainWindow::handleChangelistResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const GdbMiParser::MIList * changelist;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "changelist" || !(changelist = results.at(0).value->asList()))
		return false;

	std::unordered_set<const GdbVarObjectTreeItem *> highlightedItems;
	varObjectTreeItemModel.clearHighlightedVarObjectNames();

	struct varObjectUpdate
	{
		QString miName, value, newType;
		unsigned newNumChildren = 0;
		bool isInScope = false, isTypeChanged = true;
	};
	std::unordered_map<QString, struct varObjectUpdate> changedVarObjects;

	for (const auto & c : changelist->values)
	{
		const GdbMiParser::MITuple * changeDetails;
		if (!(changeDetails = c->asTuple()))
			continue;
		/*! \todo	There is some strange behavior in gdb - if you create a varobject for an expression
		 * 		that has child items (e.g., an array), and then run the program, and then halt the
		 * 		program at a context in which the varobject is no longer in scope, you are still
		 * 		able to list the varobject child items, and gdb does not return an error, but instead
		 * 		replies with empty strings for the values of leaf varobject items. */
		varObjectUpdate v;
		for (const auto & t : changeDetails->map)
		{
			if (t.first == "name")
				v.miName = QString::fromStdString(t.second->asConstant()->constant());
			else if (t.first == "value")
				v.value = QString::fromStdString(t.second->asConstant()->constant());
			else if (t.first == "new_type")
				v.newType = QString::fromStdString(t.second->asConstant()->constant());
			else if (t.first == "new_num_children")
				v.newNumChildren = QString::fromStdString(t.second->asConstant()->constant()).toUInt();
			else if (t.first == "in_scope")
				v.isInScope = (t.second->asConstant()->constant() == "true" ? true : false);
			else if (t.first == "type_changed")
				v.isTypeChanged = (t.second->asConstant()->constant() == "true" ? true : false);
		}
		changedVarObjects.insert({v.miName, v});
	}

	std::function<void(QModelIndex & index)> updateNode = [&](QModelIndex & index)
	{
		GdbVarObjectTreeItem * node = static_cast<GdbVarObjectTreeItem*>(index.internalPointer());
		auto it = changedVarObjects.find(node->miName);
		if (it == changedVarObjects.end())
			return;
		struct varObjectUpdate & v = it->second;
		if ((!v.isInScope || v.isTypeChanged) && node->childCount())
		{
			sendDataToGdbProcess(QString("-var-delete -c %1\n").arg(node->miName).toLocal8Bit());
			/* The varobject item will be marked as out of scope below. If the item is currently
			 * expanded, but at a later time again gets into scope, the displaying of the item's
			 * expand indicator may be shown incorrectly. To avoid such a scenario, collapse
			 * the item here. */
			ui->treeViewDataObjects->collapse(index);
		}
		v.isInScope ? varObjectTreeItemModel.markNodeAsInsideScope(index) : varObjectTreeItemModel.markNodeAsOutOfScope(index);
		if (v.isInScope)
		{
			if (!v.isTypeChanged)
				varObjectTreeItemModel.updateNodeValue(index, v.value);
			else
				varObjectTreeItemModel.updateNodeType(index, v.newType, v.value, v.newNumChildren);
		}
		highlightedItems.insert(node);
	};

	/* Mark all items in the data object tree view for updating */
	std::function<QModelIndex(QModelIndex & root)> scan = [&](QModelIndex & root) -> QModelIndex
	{
		assert(root.isValid());
		updateNode(root);
		ui->treeViewDataObjects->update(root);
		ui->treeViewDataObjects->update(root.siblingAtColumn(1));
		ui->treeViewDataObjects->update(root.siblingAtColumn(2));
		int i = 0;
		while (true)
		{
			QModelIndex index = varObjectTreeItemModel.index(i ++, 0, root);
			if (!index.isValid())
				break;
			scan(index);
		}
	};

	int i = 0;
	QModelIndex root = QModelIndex();
	while (true)
	{
		QModelIndex index = varObjectTreeItemModel.index(i ++, 0, root);
		if (!index.isValid())
			break;
		scan(index);
	}
	ui->treeViewDataObjects->update();

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
			if (!showSourceCode(frameItem))
			{
				ui->plainTextEditSourceView->clear();
				ui->plainTextEditSourceView->setPlainText(QString("Cannot show source code file containing function '%1()' at address %2")
									  .arg(frameItem->text(1))
									  .arg(frameItem->text(4)));
			}
			emit targetCallStackFrameChanged();
		}
	}
	return true;
}

bool MainWindow::handleDisassemblyResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	/*! \todo	Make the format of the source code and disassembly lines parameterizable. */
	const GdbMiParser::MIList * disassembly;
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "asm_insns" || !(disassembly = results.at(0).value->asList()))
		return false;
	int currentPCLineNumber = -1, currentLine = 0;

	QString html;

	html += ("<!DOCTYPE html>"
			  "<html>"
			  "<body>");
	std::function<void(const GdbMiParser::MITuple & asmRecord)> processAsmRecord = [&](const GdbMiParser::MITuple & asmRecord) -> void
	{
		std::string address, opcodes, mnemonics, funcName, offset;
		for (const auto & line_details : asmRecord.map)
		{
			if (line_details.first == "address")
				address = line_details.second->asConstant()->constant();
			else if (line_details.first == "func-name")
				funcName = line_details.second->asConstant()->constant();
			else if (line_details.first == "offset")
				offset = line_details.second->asConstant()->constant();
			else if (line_details.first == "opcodes")
				opcodes = line_details.second->asConstant()->constant();
			else if (line_details.first == "inst")
				mnemonics = line_details.second->asConstant()->constant();
		}
		QString s = QString::fromStdString(address + '\t' + opcodes + '\t' + mnemonics);
		if (funcName.length() && offset.length())
			s += QString::fromStdString("\t; " + funcName + "+" + offset);
		QString backgroundColor = "Silver";
		bool ok;
		uint64_t t;
		t = QString::fromStdString(address).toULongLong(& ok, 0);
		if (ok)
		{
			if (breakpointCache.hasDisabledBreakpointAtAddress(t))
				backgroundColor = "MistyRose";
			if (breakpointCache.hasEnabledBreakpointAtAddress(t))
				backgroundColor = "Red";
		}
		html += QString("<p style=\"background-color:%1;\"><pre>%2</pre></p>")
				.arg(backgroundColor)
				.arg(s);
		currentLine ++;
		if (ok && t == lastKnownProgramCounter)
			currentPCLineNumber = currentLine - 1;
	};
	for (const auto & d : disassembly->results)
	{
		if (d.variable == "src_and_asm_line")
		{
			const GdbMiParser::MITuple * t = d.value->asTuple();
			bool ok = false;
			int lineNumber = -1;
			QString fullFileName;
			auto i = t->map.find("line");
			if (i != t->map.cend())
				lineNumber = QString::fromStdString(i->second->asConstant()->constant()).toInt(& ok, 0);
			i = t->map.find("fullname");
			if (i != t->map.cend())
				fullFileName = QString::fromStdString(i->second->asConstant()->constant());
			i = t->map.find("line_asm_insn");
			{
				if (ok && lineNumber > 0 && !fullFileName.isEmpty())
				{
					QString errorMessage;
					auto sourceData = sourceFilesCache.getSourceFileData(fullFileName, errorMessage);
					QString backgroundColor = "Olive";
					if (breakpointCache.hasDisabledBreakpointAtLineNumber(fullFileName, lineNumber))
						backgroundColor = "MistyRose";
					if (breakpointCache.hasEnabledBreakpointAtLineNumber(fullFileName, lineNumber))
						backgroundColor = "Red";
					if (sourceData && lineNumber - 1 < sourceData->sourceCodeTextlines.length())
					{
						html += QString("<p style=\"background-color:%1;\"><pre>%2: %3</pre></p>")
								.arg(backgroundColor)
								.arg(lineNumber).arg(sourceData->sourceCodeTextlines.at(lineNumber - 1));
						currentLine ++;
					}
					else
					{
						html += QString("<p style=\"background-color:%1;\"><pre>%2: %3</pre></p>")
								.arg(backgroundColor)
								.arg(lineNumber).arg(fullFileName);
						currentLine ++;
					}
				}
				for (const auto & asmRecord : i->second->asList()->values)
					processAsmRecord(* asmRecord->asTuple());
			}
		}
		else
			*(int*)0=0;
	}
	/* If this is a disassembly of code, for which there is no debug information available, the reply from
	 * gdb will be a list of tuples, which will be stored as a list of values, not as a list of results, as handled above. */
	for (const auto & d : disassembly->values)
		if (d->asTuple())
			processAsmRecord(* d->asTuple());
	html += ("</body></html>");
	/*! \todo	Only update the disassembly view document if it is different than the currently generated one. Otherwise
	 * 		it feels unnatural and irritating to re-center the disassembly view after each stepping operation. */
	ui->plainTextEditDisassembly->clear();
	ui->plainTextEditDisassembly->appendHtml(html);
	/* Highlight the current program counter line, if detected. */
	if (currentPCLineNumber != -1)
	{
		QTextCursor c(ui->plainTextEditDisassembly->textCursor());
		c.movePosition(QTextCursor::Start);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, currentPCLineNumber);

		QTextEdit::ExtraSelection s;
		s.cursor = c;
		s.format = highlightFormats.currentDisassemblyLine;
		ui->plainTextEditDisassembly->setExtraSelections(QList<QTextEdit::ExtraSelection>() << s);
		ui->plainTextEditDisassembly->setTextCursor(c);
		ui->plainTextEditDisassembly->centerCursor();
	}
	return true;
}

bool MainWindow::handleValueResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "value" || !results.at(0).value->asConstant())
		return false;
	if (gdbTokenContext.hasContextForToken(tokenNumber))
	if (gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode
		&& gdbTokenContext.readAndRemoveContext(tokenNumber).gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_UPDATE_LAST_KNOWN_PROGRAM_COUNTER)
	{
		bool ok;
		uint64_t pc;
		pc = QString::fromStdString(results.at(0).value->asConstant()->constant()).toULongLong(& ok, 0);
		if (ok)
			lastKnownProgramCounter = pc;
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

bool MainWindow::handleVerifyTargetMemoryContentsSeqPoint(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	if (!gdbTokenContext.hasContextForToken(tokenNumber))
		return false;
	if (gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_SEQUENCE_POINT_CHECK_MEMORY_CONTENTS)
		return false;
	gdbTokenContext.readAndRemoveContext(tokenNumber);
	bool match = true;
	int i = 0;
	for (const auto & f : targetMemorySectionsTempFileNames)
	{
		if (!QFileInfo(f).exists())
		{
			match = false;
			QMessageBox::critical(0, "Error reading target memory", "Failed to read target memory, for verifying the target memory contents");
			break;
		}
		QFile tf(f);
		if (!tf.open(QFile::ReadOnly))
		{
			match = false;
			QMessageBox::critical(0, "Error verifying target memory",
					      QString("Failed to open temporary file\n\n"
						      "%1\n\nwhen verifying target memory contents").arg(f));
			break;
		}
		if (tf.readAll() != QByteArray(elfReader->segments[i]->get_data(), elfReader->segments[i]->get_file_size()))
		{
			match = false;
			QMessageBox::critical(0, "Target memory contents mismatch",
					      QString("The target memory contents are different from the memory contents of file:\n\n"
						      "%1").arg(settings->value(SETTINGS_LAST_LOADED_EXECUTABLE_FILE, "???").toString()));
			break;
		}
		i ++;
	}
	for (const auto & f : targetMemorySectionsTempFileNames)
	{
		QFile tf(f);
		tf.remove();
	}
	if (match)
		QMessageBox::information(0, "Target memory contents match", "Target memory contents match");
	return true;
}

bool MainWindow::handleTargetScanResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (gdbTokenContext.hasContextForToken(tokenNumber)
		&& gdbTokenContext.contextForTokenNumber(tokenNumber).gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_TARGET_SCAN_COMPLETE)
	{
		targetDataCapture.stopCapture();
		gdbTokenContext.readAndRemoveContext(tokenNumber);
		if (parseResult == GdbMiParser::ERROR)
		{
			QMessageBox::critical(0, "Target scan failed", QString("Target scan command failed, error:\n%1").arg(gdbErrorString(parseResult, results)));
			return true;
		}
		/* Try to parse any stream output from the target. */
		const QStringList & output(targetDataCapture.capturedLines());
		QStringList detectedTargets;
		QRegularExpression rx("^\"\\s*(\\d+)\\s+");
		for (const auto & l : output)
		{
			qDebug() << "processing line:" << l;
			if (l.contains("scan failed"))
			{
				QMessageBox::critical(0, "Target scan failed", QString("Target scan command failed, error:\n%1").arg(l));
				return true;
			}
			QRegularExpressionMatch match = rx.match(l);
			if (match.hasMatch())
				detectedTargets << l;
		}
		assert(detectedTargets.length() != 0);
		bool ok;
		/*! \todo: handle the case for one connected target only, and do not show the dialog
		 * in that case. I am deliberately not doing this now, because I have no
		 * hardware for testing right now. */
		QString targetNumber = QInputDialog::getItem(0, "Select target to connect to",
							     ""
							     "Select the target to connect to:",
							     detectedTargets, 0, false, &ok);
		if (!ok)
		{
			QMessageBox::information(0, "No target selected", "No target selected, abborting target connection.");
			return true;
		}
		QRegularExpressionMatch match = rx.match(targetNumber);
		sendDataToGdbProcess(QString("-target-attach %1\n").arg(match.captured(1)));
		return true;
	}
	return false;
}

bool MainWindow::handleMemoryResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult == GdbMiParser::ERROR)
	{
		/* If there is an error reading the target memory, disable the auto update of the memory view. */
		if (gdbTokenContext.hasContextForToken(tokenNumber) && gdbTokenContext.contextForTokenNumber(tokenNumber)
				.gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_DATA_READ_MEMORY)
		{
			gdbTokenContext.readAndRemoveContext(tokenNumber);
			ui->checkBoxMemoryDumpAutoUpdate->setChecked(false);
		}
		return false;
	}
	const GdbMiParser::MIList * l;
	const GdbMiParser::MITuple * t;
	if (parseResult != GdbMiParser::DONE || !results.size() || results.at(0).variable != "memory"
		|| !results.at(0).value || !(l = results.at(0).value->asList()) || !l->values.size() || !(t = l->values.at(0)->asTuple()))
		return false;
	uint32_t address = 0;
	QByteArray data;
	for (const auto & x : t->map)
	{
		if (x.first == "begin")
			address += QString::fromStdString(x.second->asConstant()->constant()).toUInt(0, 0);
		if (x.first == "offset")
			address += QString::fromStdString(x.second->asConstant()->constant()).toUInt(0, 0);
		if (x.first == "contents")
			data += QByteArray::fromHex(QString::fromStdString(x.second->asConstant()->constant()).toLocal8Bit());
	}
	if (data.length() == 4)
	{
		uint32_t d = data.at(0) | (data.at(1) << 8) | (data.at(2) << 16) | (data.at(3) << 24);
		/* Update any svd register views. */
		for (const auto & r : svdViews)
			if (r.address == address)
				for (auto & f : r.fields)
					f.spinbox->setValue((d >> f.bitoffset) & ((1 << f.bitwidth) - 1));
	}
	/* Check if the memory dump view should be updated. */
	if (gdbTokenContext.hasContextForToken(tokenNumber) && gdbTokenContext.contextForTokenNumber(tokenNumber)
			.gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_DATA_READ_MEMORY)
	{
		gdbTokenContext.readAndRemoveContext(tokenNumber);
		ui->plainTextEditMemoryDump->clear();
		ui->plainTextEditMemoryDump->appendPlainText(data.toHex());
	}

	return true;
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
	ui->pushButtonConnectGdbToGdbServer->setEnabled(false);
	ui->pushButtonStartGdb->setEnabled(true);
}

void MainWindow::gdbProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	qDebug() << gdbProcess->readAllStandardError();
	qDebug() << gdbProcess->readAllStandardOutput();
	qDebug() << "gdb process finished";
	ui->pushButtonStartGdb->setEnabled(true);
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

void MainWindow::sendDataToGdbProcess(const QString & data)
{
	appendLineToGdbLog(">>> " + data);
	gdbProcess->write(data.toLocal8Bit());
	gdbProcess->waitForBytesWritten();
}
void MainWindow::readGdbVarObjectChildren(const QString varObjectName)
{
	unsigned n = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
							   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_NUMCHILD, varObjectName));
	sendDataToGdbProcess((QString("%1-var-list-children --all-values ").arg(n) + varObjectName + "\n"));
}

bool MainWindow::showSourceCode(const QTreeWidgetItem *item)
{
	if (!item->data(0, SourceFileData::DISABLE_SOURCE_CODE_NAVIGATION).isNull() && item->data(0, SourceFileData::DISABLE_SOURCE_CODE_NAVIGATION).toBool())
		return false;
	QVariant v = item->data(0, SourceFileData::FILE_NAME);
	bool ok;
	if (v.type() != QMetaType::QString)
		return false;
	QString sourceFileName = item->data(0, SourceFileData::FILE_NAME).toString();
	int lineNumber = item->data(0, SourceFileData::LINE_NUMBER).toInt(& ok);
	if (!ok)
		return false;
	if (lineNumber == 0)
		lineNumber = 1;
	return displaySourceCodeFile(SourceCodeLocation(sourceFileName, lineNumber), true, true);
}

void MainWindow::breakpointsContextMenuRequested(QPoint p)
{
	QTreeWidgetItem * w = ui->treeWidgetBreakpoints->itemAt(p);
	if (w)
	{
		QMenu menu(this);
		GdbBreakpointData * breakpoint = static_cast<GdbBreakpointData *>(w->data(0, SourceFileData::BREAKPOINT_DATA_POINTER).value<void *>());
		QHash<void *, int> menuSelections;
		/* Special case for breakpoints with multiple locations - only allow to delete the top level breakpoint,
		 * because gdb does not allow derived breakpoints to be deleted. */
		if (!w->parent())
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
			sendDataToGdbProcess(QString("-break-%2 %3\n").arg(breakpointMiCommand).arg(breakpoint->gdbReportedNumberString));
			/* Reread the list of breakpoints. */
			sendDataToGdbProcess("-break-list\n");
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

void MainWindow::sourceItemContextMenuRequested(const QTreeWidget *treeWidget, QPoint p)
{
	QTreeWidgetItem * w = treeWidget->itemAt(p);

	if (w)
	{
		if (w->data(0, SourceFileData::ITEM_KIND).isNull())
		{
			qDebug() << "warning: unset source item type, aborting context menu request";
			return;
		}
		if (!w->data(0, SourceFileData::DISABLE_CONTEXT_MENU).isNull() && w->data(0, SourceFileData::DISABLE_CONTEXT_MENU).toBool())
			return;
		QList<QTreeWidgetItem *> items = treeWidget->findItems(w->text(0), Qt::MatchExactly);
		SourceFileData::SymbolData::SymbolKind itemKind = (SourceFileData::SymbolData::SymbolKind) w->data(0, SourceFileData::ITEM_KIND).toUInt();
		/*! \todo	At this time, the case for multiple symbols of the same kind is not handled well.
		 *		As a minimum, warn the user about this. */
		int t = 0;
		for (const auto & s : items)
		{
			if (s->data(0, SourceFileData::ITEM_KIND).isNull())
				continue;
			if ((SourceFileData::SymbolData::SymbolKind) s->data(0, SourceFileData::ITEM_KIND).toUInt() == itemKind)
				t ++;
		}
		if (t > 1)
			QMessageBox::warning(0, "Multiple symbols of the same kind",
					     QString("Multiple symbols found for id:\n\n%1\n\n"
						     "Be warned that this case is not handled properly at this time.\n"
						     "You may experience incorrect behavior from the frontend!").arg(w->text(0)));
		QMenu menu(this);
		QAction * disassembleFile = 0, * disassembleSuprogram = 0;
		/* Because of the header of the tree widget, it looks more natural to set the
		 * menu position on the screen at point translated from the tree widget viewport,
		 * not from the tree widget itself. */
		switch (itemKind)
		{
			case SourceFileData::SymbolData::SOURCE_FILE_NAME:
				disassembleFile = menu.addAction("Disassemble");
				break;
			case SourceFileData::SymbolData::SUBPROGRAM:
				disassembleSuprogram = menu.addAction("Disassemble");
				break;
			case SourceFileData::SymbolData::DATA_OBJECT:
			case SourceFileData::SymbolData::DATA_TYPE:
				return;
			default:
				qDebug() << QString("warning: unknown source item type: %1, aborting context menu request").arg(itemKind);
				return;
		}

		menu.addAction("Cancel");
		QAction * selection = menu.exec(treeWidget->viewport()->mapToGlobal(p));
		if (selection)
		{
			if (selection == disassembleFile)
				/*! \todo	This does not work for disassembling whole files. It just disassembles the very first
				 *		function in the file, if any. */
				sendDataToGdbProcess(QString("-data-disassemble -f \"%1\" -l 1 -- 5\n").arg(w->text(0)));
			else if (selection == disassembleSuprogram)
				sendDataToGdbProcess(QString("-data-disassemble -a \"%1\" -- 5\n").arg(w->text(0)));
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

void MainWindow::breakpointViewItemChanged(QTreeWidgetItem *item, int column)
{
	if (column != TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER)
		return;
	GdbBreakpointData * breakpoint = static_cast<GdbBreakpointData *>(item->data(0, SourceFileData::BREAKPOINT_DATA_POINTER).value<void *>());
	if (breakpoint->enabled == item->checkState(TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER))
		/* Nothing to do, return. */
		return;
	sendDataToGdbProcess(QString("-break-%2 %3\n")
			     .arg(breakpoint->enabled ? "disable" : "enable")
			     .arg(breakpoint->gdbReportedNumberString));
	/* Reread the list of breakpoints. */
	sendDataToGdbProcess("-break-list\n");
}

void MainWindow::stringSearchReady(const QString pattern, QSharedPointer<QVector<StringFinder::SearchResult>> results, bool resultsTruncated)
{
	ui->treeWidgetSearchResults->clear();
	ui->lineEditSearchFilesForText->setText(pattern);
	for (const auto & result : * results)
		ui->treeWidgetSearchResults->addTopLevelItem(createNavigationWidgetItem(
			     QStringList() << QFileInfo(result.fullFileName).fileName() << QString("%1").arg(result.lineNumber) << result.sourceCodeLineText,
			     result.fullFileName,
			     result.lineNumber
			     ));
	ui->treeWidgetSearchResults->sortByColumn(0, Qt::AscendingOrder);
	if (resultsTruncated)
		ui->treeWidgetSearchResults->addTopLevelItem(new QTreeWidgetItem(QStringList() << "" << "xxx" << "Too many results - search results truncated"));
}

void MainWindow::updateSourceListView()
{
bool showOnlySourcesWithMachineCode = ui->checkBoxShowOnlySourcesWithMachineCode->isChecked();
bool showOnlyExistingSourceFiles = ui->checkBoxShowOnlyExistingSourceFiles->isChecked();

	std::function<bool(const SourceFileData & fileData)> shouldFileBeListed = [&] (const SourceFileData & fileData) -> bool
	{
		if (showOnlyExistingSourceFiles)
		{
			/* WARNING: this is potentially expensive! */
			QFileInfo f(fileData.fullFileName);
			if (!f.exists())
				return false;
		}
		return !showOnlySourcesWithMachineCode || /* This is a safe-catch. */ !fileData.isSourceLinesFetched || fileData.machineCodeLineNumbers.size();
	};
	ui->treeWidgetSourceFiles->clear();
	for (const auto & f : sourceFiles)
		if (shouldFileBeListed(f))
		{
			QTreeWidgetItem * t = createNavigationWidgetItem(QStringList() << f.fileName << f.fullFileName, f.fullFileName,
									 0, SourceFileData::SymbolData::SOURCE_FILE_NAME, false, true);
			ui->treeWidgetSourceFiles->addTopLevelItem(t);
			for (const auto & s : f.subprograms)
				t->addChild(createNavigationWidgetItem(QStringList() << s.description, f.fullFileName, s.line, SourceFileData::SymbolData::SUBPROGRAM));
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
			ui->treeWidgetSubprograms->addTopLevelItem(createNavigationWidgetItem(
				   QStringList() << s.name << f.fileName << QString("%1").arg(s.line) << s.description,
				   f.fullFileName,
				   s.line,
				   SourceFileData::SymbolData::SUBPROGRAM));

		for (const auto & s : f.variables)
			ui->treeWidgetStaticDataObjects->addTopLevelItem(createNavigationWidgetItem(
				   QStringList() << s.name << f.fileName << QString("%1").arg(s.line) << s.description,
				   f.fullFileName,
				   s.line,
				   SourceFileData::SymbolData::DATA_OBJECT));
		for (const auto & s : f.dataTypes)
			ui->treeWidgetDataTypes->addTopLevelItem(createNavigationWidgetItem(
				   QStringList() << s.name << f.fileName << QString("%1").arg(s.line),
				   f.fullFileName,
				   s.line,
				   SourceFileData::SymbolData::DATA_TYPE));
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
		QTreeWidgetItem * w = 0;
		if (!b.multipleLocationBreakpoints.size())
		{
			w = createNavigationWidgetItem(
						QStringList()
						<< b.gdbReportedNumberString
						<< b.type
						<< b.disposition
						<< (b.enabled ? "yes" : "no")
						<< QString("0x%1").arg(b.address, 8, 16, QChar('0'))
						<< b.locationSpecifierString,
						b.sourceCodeLocation.fullFileName,
						b.sourceCodeLocation.lineNumber
						);
		}
		else
		{
			/* Special case - for breakpoints with multiple locations, gdb does not report a source code
			 * location for the primary breakpoints. Instead, source code locations are reported for
			 * the list of derived breakpoints. Only enable navigation for this breakpoint if all of the
			 * derived breakpoints have the same source code location. */
			bool disableNavigation = false;
			for (int i = 0; i < b.multipleLocationBreakpoints.size() - 1; i ++)
				if (b.multipleLocationBreakpoints.at(i + 1).sourceCodeLocation != b.multipleLocationBreakpoints.at(i).sourceCodeLocation)
				{
					disableNavigation = true;
					break;
				}
			w = createNavigationWidgetItem(
						QStringList()
						<< b.gdbReportedNumberString
						<< b.type
						<< b.disposition
						<< (b.enabled ? "yes" : "no")
						<< QString("0x%1").arg(b.address, 8, 16, QChar('0'))
						<< b.locationSpecifierString,
						b.multipleLocationBreakpoints.at(0).sourceCodeLocation.fullFileName,
						b.multipleLocationBreakpoints.at(0).sourceCodeLocation.lineNumber,
						SourceFileData::SymbolData::INVALID,
						disableNavigation
						);
		}
		w->setCheckState(TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER, b.enabled ? Qt::Checked : Qt::Unchecked);
		w->setData(0, SourceFileData::BREAKPOINT_DATA_POINTER, QVariant::fromValue((void *) & b));
		for (const auto & m : b.multipleLocationBreakpoints)
		{
			QTreeWidgetItem * t = createNavigationWidgetItem(
					QStringList()
						<< m.gdbReportedNumberString
						<< m.type
						<< m.disposition
						<< (m.enabled ? "yes" : "no")
						<< QString("0x%1").arg(m.address, 8, 16, QChar('0'))
						<< m.locationSpecifierString,
					m.sourceCodeLocation.fullFileName,
					m.sourceCodeLocation.lineNumber
					);
			t->setCheckState(TREE_WIDGET_BREAKPOINT_ENABLE_STATUS_COLUMN_NUMBER, m.enabled ? Qt::Checked : Qt::Unchecked);
			t->setData(0, SourceFileData::BREAKPOINT_DATA_POINTER, QVariant::fromValue((void *) & m));
			w->addChild(t);
		}
		ui->treeWidgetBreakpoints->addTopLevelItem(w);
	}
}

void MainWindow::updateBookmarksView()
{
	ui->treeWidgetBookmarks->clear();
	for (const auto & bookmark : bookmarks)
		ui->treeWidgetBookmarks->addTopLevelItem(createNavigationWidgetItem(
			 QStringList() << QFileInfo(bookmark.fullFileName).fileName() << QString("%1").arg(bookmark.lineNumber),
			 bookmark.fullFileName,
			 bookmark.lineNumber
			 ));
}

void MainWindow::sendCommandsToGdb(QLineEdit * lineEdit)
{
/* Newlines from the lineEdit are possible if pasting text from the clipboard. */
QStringList l = lineEdit->text().split('\n');
	lineEdit->clear();
	for (const auto & s : l)
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
	/* Highlight lines with breakpoints, if any. */

	QTextCursor c(ui->plainTextEditSourceView->textCursor());
	sourceCodeViewHighlights.disabledBreakpointedLines.clear();
	sourceCodeViewHighlights.enabledBreakpointedLines.clear();

	const QSet<int /* line numbers */> & enabledLines = breakpointCache.enabledBreakpointLinesForFile(displayedSourceCodeFile);
	const QSet<int /* line numbers */> & disabledLines = breakpointCache.disabledBreakpointLinesForFile(displayedSourceCodeFile);

	QTextEdit::ExtraSelection selection;

	for (const auto & line : enabledLines)
	{
		c.movePosition(QTextCursor::Start);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, line - 1);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
		selection.cursor = c;
		selection.format = highlightFormats.enabledBreakpoint;
		sourceCodeViewHighlights.enabledBreakpointedLines << selection;
	}
	for (const auto & line : disabledLines)
	{
		c.movePosition(QTextCursor::Start);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, line - 1);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
		selection.cursor = c;
		selection.format = highlightFormats.disabledBreakpoint;
		sourceCodeViewHighlights.disabledBreakpointedLines << selection;
	}
}

void MainWindow::highlightBookmarks()
{
	QTextCursor c(ui->plainTextEditSourceView->textCursor());
	sourceCodeViewHighlights.bookmarkedLines.clear();
	QTextEdit::ExtraSelection selection;
	selection.format = highlightFormats.bookmark;
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
	displaySourceCodeFile(SourceCodeLocation(sourceFileName, lineNumber), true, true);
}

void MainWindow::searchCurrentSourceText(const QString & pattern)
{
	searchData.lastSearchedText = pattern;
	searchData.matchPositions.clear();
	sourceCodeViewHighlights.searchedTextMatches.clear();
	if (pattern.length() != 0)
	{
		QString document = ui->plainTextEditSourceView->toPlainText();
		int index = 0, position;
		while ((position = document.indexOf(pattern, index)) != -1)
			searchData.matchPositions.push_back(position), index = position + 1;

		QTextCursor c(ui->plainTextEditSourceView->textCursor());
		int matchLength = pattern.length();
		for (const auto & matchPosition : searchData.matchPositions)
		{
			c.setPosition(matchPosition);
			c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, matchLength);
			QTextEdit::ExtraSelection s;
			s.cursor = c;
			s.format = highlightFormats.searchedText;
			sourceCodeViewHighlights.searchedTextMatches << s;
		}
	}
	refreshSourceCodeView();
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

QTreeWidgetItem * MainWindow::createNavigationWidgetItem(const QStringList &columnTexts, const QString & fullFileName, int lineNumber, MainWindow::SourceFileData::SymbolData::SymbolKind itemKind, bool disableNavigation, bool disableContextMenu)
{
	QTreeWidgetItem * item = new QTreeWidgetItem(columnTexts);
	item->setData(0, SourceFileData::FILE_NAME, fullFileName);
	item->setData(0, SourceFileData::LINE_NUMBER, lineNumber);
	item->setData(0, SourceFileData::ITEM_KIND, itemKind);
	if (disableNavigation)
		item->setData(0, SourceFileData::DISABLE_SOURCE_CODE_NAVIGATION, true);
	if (disableContextMenu)
		item->setData(0, SourceFileData::DISABLE_CONTEXT_MENU, true);
	return item;
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

bool MainWindow::displaySourceCodeFile(const SourceCodeLocation &sourceCodeLocation, bool saveCurrentLocationToNavigationStack,
				       bool saveNewLocationToNavigationStack)
{
QTime ttt;
ttt.start();
#if KEEP_THIS_FOR_BENCHMARKING_PURPOSES
QFile f(sourceCodeLocation.fullFileName);
QFileInfo fi(sourceCodeLocation.fullFileName);
int currentBlockNumber = ui->plainTextEditSourceView->textCursor().blockNumber();
bool result = false;

	/*! \todo	Check if this is still needed. */
	ui->plainTextEditSourceView->blockSignals(true);
	ui->plainTextEditSourceView->clear();
	ui->plainTextEditSourceView->setCurrentCharFormat(QTextCharFormat());

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
	else if (sourceCodeLocation.fullFileName == internalHelpFileName)
	{
		/* Special case for the internal help file - do not attempt to apply syntax highlighting on it. */
		sourceCodeViewHighlights.navigatedSourceCodeLine.clear();
		ui->plainTextEditSourceView->appendPlainText(f.readAll());
		QTextCursor c = ui->plainTextEditSourceView->textCursor();
		c.movePosition(QTextCursor::Start);
		if (sourceCodeLocation.lineNumber > 0)
			c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, sourceCodeLocation.lineNumber - 1);
		ui->plainTextEditSourceView->setTextCursor(c);
		ui->plainTextEditSourceView->centerCursor();
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
		refreshSourceCodeView();
	}
	else
	{
		/*! \todo Wrap this as a function (along with an example in the 'clex.y' scanner), and simply call that function. */
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
		searchCurrentSourceText(searchData.lastSearchedText);
		if (saveNewLocationToNavigationStack)
			navigationStack.push(sourceCodeLocation);

		result = true;
	}
	setWindowTitle("turbo: " + displayedSourceCodeFile);
	ui->plainTextEditSourceView->blockSignals(false);
	if (!sourceFileWatcher.files().isEmpty())
		sourceFileWatcher.removePaths(sourceFileWatcher.files());
	if (!fi.absoluteFilePath().isEmpty())
		sourceFileWatcher.addPath(fi.absoluteFilePath());
	/* Update navigation buttons. */
	ui->pushButtonNavigateBack->setEnabled(navigationStack.canNavigateBack());
	ui->pushButtonNavigateForward->setEnabled(navigationStack.canNavigateForward());

	qDebug() << "Source file render time:" << ttt.elapsed();
	return result;
#else
int currentBlockNumber = ui->plainTextEditSourceView->textCursor().blockNumber();
bool result = false;

	/*! \todo	Check if this is still needed. */
	ui->plainTextEditSourceView->blockSignals(true);
	ui->plainTextEditSourceView->clear();
	ui->plainTextEditSourceView->setCurrentCharFormat(QTextCharFormat());

	/* Save the current source code view location in the navigation stack, if valid. */
	if (saveCurrentLocationToNavigationStack && !displayedSourceCodeFile.isEmpty())
		navigationStack.push(SourceCodeLocation(displayedSourceCodeFile, currentBlockNumber + 1));

	displayedSourceCodeFile.clear();

	/* Special case for the internal help file - do not attempt to apply syntax highlighting on it. */
	if (sourceCodeLocation.fullFileName == internalHelpFileName)
	{
		QFile f(internalHelpFileName);
		f.open(QFile::ReadOnly);
		sourceCodeViewHighlights.navigatedSourceCodeLine.clear();
		ui->plainTextEditSourceView->appendPlainText(f.readAll());
		QTextCursor c = ui->plainTextEditSourceView->textCursor();
		c.movePosition(QTextCursor::Start);
		if (sourceCodeLocation.lineNumber > 0)
			c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, sourceCodeLocation.lineNumber - 1);
		ui->plainTextEditSourceView->setTextCursor(c);
		ui->plainTextEditSourceView->centerCursor();
		if (sourceCodeLocation.lineNumber > 0)
		{
			/* Highlight current line. */
			c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
			QTextEdit::ExtraSelection selection;
			selection.cursor = c;
			selection.format = highlightFormats.navigatedLine;
			sourceCodeViewHighlights.navigatedSourceCodeLine << selection;
		}
		displayedSourceCodeFile = sourceCodeLocation.fullFileName;
		refreshSourceCodeView();
	}
	else
	{
		QString errorMessage;
		auto sourceData = sourceFilesCache.getSourceFileData(sourceCodeLocation.fullFileName, errorMessage);
		if (!sourceData)
		{
			ui->plainTextEditSourceView->setPlainText(errorMessage);
			goto out;
		}

		ui->plainTextEditSourceView->appendHtml(sourceData.operator *().htmlDocument.operator *());

		/* Mark which source code lines have corresponding machine code generated for them,
		 * and so can be used for setting breakpoints. */
		/*! \todo	WARNING: tHIS TOTALLY DEFEATS HAVING A CACHE FOR THE SOURCE CODE FILES.
		 * 		This actually dominates the rendering time for the source code view.
		 *		It is best that this marking is done when generating the html document
		 *		for the source code file, instead of doing it here. */
		const auto & f = sourceFiles.find(sourceCodeLocation.fullFileName);
		if (f != sourceFiles.cend() && f->machineCodeLineNumbers.size())
		{
			QTextCursor c(ui->plainTextEditSourceView->textCursor());
			c.movePosition(QTextCursor::Start);
			c.beginEditBlock();
			while (1)
			{
				if (f->machineCodeLineNumbers.count(c.blockNumber() + 1))
					c.insertText("*");
				else
					c.insertText(" ");
				if (!c.movePosition(QTextCursor::NextBlock))
					break;
			}
			c.endEditBlock();
		}
#if 0
		for (const auto & l : lines)
		{
			QString n(QString("%1").arg(++ i));
			source += QString(numFieldWidth - n.size(), QChar(' ')) + n + (machineCodeLineNumbers->count(i) ? '*':' ') + '|' + l + '\n';
		}
#endif

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
			selection.format = highlightFormats.navigatedLine;
			sourceCodeViewHighlights.navigatedSourceCodeLine << selection;
		}
		displayedSourceCodeFile = sourceCodeLocation.fullFileName;
		searchCurrentSourceText(searchData.lastSearchedText);
		if (saveNewLocationToNavigationStack)
			navigationStack.push(sourceCodeLocation);

		if (!sourceFileWatcher.files().isEmpty())
			sourceFileWatcher.removePaths(sourceFileWatcher.files());
		sourceFileWatcher.addPath(sourceData.operator *().filesystemFileName);

		result = true;
	}
out:
	setWindowTitle("turbo: " + displayedSourceCodeFile);
	ui->plainTextEditSourceView->blockSignals(false);
	/* Update navigation buttons. */
	ui->pushButtonNavigateBack->setEnabled(navigationStack.canNavigateBack());
	ui->pushButtonNavigateForward->setEnabled(navigationStack.canNavigateForward());

	qDebug() << "Source file render time:" << ttt.elapsed();
	return result;
#endif
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
	SvdFileParser::SvdRegisterOrClusterNode * svdRegister = static_cast<SvdFileParser::SvdRegisterOrClusterNode *>(item->data(0, SVD_REGISTER_POINTER).value<void *>());

	QDialog * dialog = new QDialog(0, Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint);
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
		view.fields << SvdRegisterViewData::RegField(field.bitOffset, field.bitWidth, s);
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
				if (target_state == TARGET_STOPPED)
				{
					sendDataToGdbProcess(QString("-data-read-memory-bytes 0x%1 4\n").arg(view.address, 8, 16, QChar('0')));
					view.fieldsGroupBox->setEnabled(true);
				}
				else
					QMessageBox::information(0, "", QString("Cannot read register @$%1,\ntarget must be connected and halted.").arg(view.address, 8, 16, QChar('0')));
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
	QList<QTreeWidgetItem *> sourceFileNames, subprograms, dataObjects, dataTypes;
	sourceFileNames = ui->treeWidgetSourceFiles->findItems(searchPattern, Qt::MatchContains);
	subprograms = ui->treeWidgetSubprograms->findItems(searchPattern, Qt::MatchContains);
	dataObjects = ui->treeWidgetStaticDataObjects->findItems(searchPattern, Qt::MatchContains);
	dataTypes = ui->treeWidgetDataTypes->findItems(searchPattern, Qt::MatchContains);
	/* Make sure that the items are sorted alphabetically. */
	static auto compare = [](const QTreeWidgetItem * a, const QTreeWidgetItem * b) -> bool { return a->text(0) < b->text(0); };
	/* Make sure to *copy* the tree widget items, as the locator tree widget will take
	 * ownership of the items added. */
	QTreeWidgetItem * headerItem;
	if (sourceFileNames.size())
	{
		std::sort(sourceFileNames.begin(), sourceFileNames.end(), compare);
		ui->treeWidgetObjectLocator->addTopLevelItem(headerItem = new QTreeWidgetItem(QStringList() << "--- File Names ---"));
		headerItem->setBackground(0, Qt::lightGray);
		for (const auto & s : sourceFileNames)
			ui->treeWidgetObjectLocator->addTopLevelItem(new QTreeWidgetItem(* s));
	}
	if (subprograms.size())
	{
		std::sort(subprograms.begin(), subprograms.end(), compare);
		ui->treeWidgetObjectLocator->addTopLevelItem(headerItem = new QTreeWidgetItem(QStringList() << "--- Subprograms ---"));
		headerItem->setBackground(0, Qt::lightGray);
		for (const auto & s : subprograms)
			ui->treeWidgetObjectLocator->addTopLevelItem(new QTreeWidgetItem(* s));
	}
	if (dataObjects.size())
	{
		std::sort(dataObjects.begin(), dataObjects.end());
		ui->treeWidgetObjectLocator->addTopLevelItem(headerItem = new QTreeWidgetItem(QStringList() << "--- Data Objects ---"));
		headerItem->setBackground(0, Qt::lightGray);
		for (const auto & s : dataObjects)
			ui->treeWidgetObjectLocator->addTopLevelItem(new QTreeWidgetItem(* s));
	}
	if (dataTypes.size())
	{
		std::sort(dataTypes.begin(), dataTypes.end());
		ui->treeWidgetObjectLocator->addTopLevelItem(headerItem = new QTreeWidgetItem(QStringList() << "--- Data Types ---"));
		headerItem->setBackground(0, Qt::lightGray);
		for (const auto & s : dataTypes)
			ui->treeWidgetObjectLocator->addTopLevelItem(new QTreeWidgetItem(* s));
	}
	/*! \todo	Remove this once it is clear the new code works fine. */
#if 0
	/* Search for symbols (subprograms, data objects, data types), and file names matching the search pattern.
	 * Keep the sets of items found sorted, for each type of object (subrogram, data object, filename). */
	std::multimap<QString /* filename */, const SourceFileData *> sourceFileNames;
	std::multimap<QString /* subprogram name */, QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>> subprograms;
	std::multimap<QString /* data object name */, QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>> dataObjects;
	std::multimap<QString /* data type name */, QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>> dataTypes;
	for (const auto & file : sourceFiles)
	{
		if (file.fileName.contains(searchPattern, Qt::CaseInsensitive))
			sourceFileNames.insert({file.fileName, & file});
		for (const auto & subprogram : file.subprograms)
			if (subprogram.name.contains(searchPattern, Qt::CaseInsensitive))
				subprograms.insert({subprogram.name,
					QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>(& file.fullFileName, & subprogram)});
		for (const auto & dataObject : file.variables)
			if (dataObject.name.contains(searchPattern, Qt::CaseInsensitive))
				dataObjects.insert({dataObject.name,
					QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>(& file.fullFileName, & dataObject)});
		for (const auto & dataType : file.dataTypes)
			if (dataType.name.contains(searchPattern, Qt::CaseInsensitive))
				dataTypes.insert({dataType.name,
					QPair<const QString * /* full file name */, const struct SourceFileData::SymbolData *>(& file.fullFileName, & dataType)});
	}
	/* Populate the object locator view with the items matching the search pattern. */
	for (const auto & file : sourceFileNames)
		ui->treeWidgetObjectLocator->addTopLevelItem(createNavigationWidgetItem(
								     QStringList() << file.first,
								     file.second->fullFileName,
								     0,
								     SourceFileData::SymbolData::SOURCE_FILE_NAME));
	for (const auto & dataObject : dataObjects)
		ui->treeWidgetObjectLocator->addTopLevelItem(createNavigationWidgetItem(
								     QStringList() << dataObject.first,
								     * dataObject.second.first,
								     dataObject.second.second->line,
								     SourceFileData::SymbolData::DATA_OBJECT));
	for (const auto & subprogram : subprograms)
		ui->treeWidgetObjectLocator->addTopLevelItem(createNavigationWidgetItem(
								     QStringList() << subprogram.first,
								     * subprogram.second.first,
								     subprogram.second.second->line,
								     SourceFileData::SymbolData::SUBPROGRAM));
	for (const auto & dataType : dataTypes)
		ui->treeWidgetObjectLocator->addTopLevelItem(createNavigationWidgetItem(
								     QStringList() << dataType.first,
								     * dataType.second.first,
								     dataType.second.second->line,
								     SourceFileData::SymbolData::DATA_TYPE));
#endif
}

void MainWindow::on_pushButtonDeleteAllBookmarks_clicked()
{
	bookmarks.clear();
	updateBookmarksView();
	refreshSourceCodeView();
}

void MainWindow::on_pushButtonDisconnectGdbServer_clicked()
{
	blackMagicProbeServer.closeConnections();
}

void MainWindow::on_lineEditFindText_returnPressed()
{
	searchCurrentSourceText(ui->lineEditFindText->text());
	ui->plainTextEditSourceView->setFocus();
	moveCursorToNextMatch();
}


#ifdef Q_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincon.h>
#include <debugapi.h>
#endif
void MainWindow::requestTargetHalt(void)
{
	blackMagicProbeServer.sendRawGdbPacket("\x3");
	if (settings->value(SETTINGS_CHECKBOX_ENABLE_NATIVE_DEBUGGING_STATE, false).toBool())
	{
#ifdef Q_OS_WINDOWS
		if (debugProcessId != -1)
		{
			HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, debugProcessId);
			if (!DebugBreakProcess(process))
				QMessageBox::critical(0, "Error interrupting the debugged process",
						      "Failed to interrupt the process that is debugged");
			CloseHandle(process);
		}
#endif
	}
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

void MainWindow::loadSVDFile(void)
{
	if (!QFileInfo(targetSVDFileName).exists())
	{
		QMessageBox::critical(0, "Target SVD file not found", "No valid SVD file specified.\nYou can specify the target SVD file in the settings.");
		ui->pushButtonSettings->click();
		return;
	}
	svdParser.parse(targetSVDFileName);
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
	std::function<void(QTreeWidgetItem * parent, const SvdFileParser::SvdRegisterOrClusterNode & rc, uint32_t baseAddress)> populateRegisterOrCluster =
		[&] (QTreeWidgetItem * parent, const SvdFileParser::SvdRegisterOrClusterNode & rc, uint32_t baseAddress) -> void
	{
		if (rc.isRegisterNode)
		{
			/* Create a register node. */
			uint32_t address = rc.addressOffset + baseAddress;
			QTreeWidgetItem * r = new QTreeWidgetItem(parent, QStringList() << rc.name << QString("0x%1").arg(address, 8, 16, QChar('0'))
								  << QString(rc.description).replace(rx, " "));
			r->setData(0, SVD_REGISTER_POINTER, QVariant::fromValue((void *) & rc));
			r->setData(0, SVD_REGISTER_ADDRESS, address);
			for (const auto & f : rc.fields)
			{
				QStringList fieldHeaders;
				fieldHeaders << f.name;
				fieldHeaders << QString("%1").arg(f.bitOffset);
				if (f.bitWidth > 1)
					fieldHeaders.last().append(QString(":%1").arg(f.bitOffset + f.bitWidth - 1));
				fieldHeaders << QString(f.description).replace(rx, " ");
				new QTreeWidgetItem(r, fieldHeaders);
			}
		}
		else
		{
			/* Create a cluster node. */
			QTreeWidgetItem * cluster = new QTreeWidgetItem(parent, QStringList() << rc.name << "<cluster lorem ipsum>" << rc.description);
			for (const auto & r : rc.children)
				populateRegisterOrCluster(cluster, r, baseAddress + rc.addressOffset);
		}

	};
	auto populatePeripheral = [&] (QTreeWidgetItem * parent, const SvdFileParser::SvdPeripheralNode * peripheral) -> void
	{
		QTreeWidgetItem * p = new QTreeWidgetItem(parent, QStringList() << peripheral->name << QString("0x%1").arg(peripheral->baseAddress, 8, 16, QChar('0')) << peripheral->description);
		const std::list<SvdFileParser::SvdRegisterOrClusterNode> & registers = peripheral->registersAndClusters;

		for (const auto & r : registers)
			populateRegisterOrCluster(p, r, peripheral->baseAddress);
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
		for (const auto & r : p.registersAndClusters)
			populateRegisterOrCluster(peripheral, r, p.baseAddress);
	}
}

void MainWindow::displayHelp()
{
	displaySourceCodeFile(SourceCodeLocation(internalHelpFileName));
	QString plainText = ui->plainTextEditSourceView->toPlainText();
	ui->plainTextEditSourceView->clear();
	ui->plainTextEditSourceView->setPlainText(plainText);
}

void MainWindow::on_pushButtonSendScratchpadToGdb_clicked()
{
	sendDataToGdbProcess(ui->plainTextEditScratchpad->toPlainText());
}

void MainWindow::on_pushButtonScanForTargets_clicked()
{
	unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(GdbTokenContext::GdbResponseContext::GDB_RESPONSE_TARGET_SCAN_COMPLETE));
	targetDataCapture.startCapture();
	sendDataToGdbProcess(QString("%1monitor swdp_scan\n").arg(t));
}

void MainWindow::on_pushButtonConnectGdbToGdbServer_clicked()
{
	/*! \todo	do not hardcode the gdb server listening port */
	sendDataToGdbProcess("-target-select extended-remote :1122\n");
}

void MainWindow::compareTargetMemory()
{
	targetMemorySectionsTempFileNames.clear();
	if (!elfReader)
	{
		QMessageBox::critical(0, "ELF file unavailable", "ELF file unavailable, cannot perform target memory verification");
		return;
	}
	/*! \todo	Create proper temporary files, this is just a quick hack. */
	auto x = QDateTime::currentMSecsSinceEpoch();
	QString gdbRequest;
	int i = 0;
	for (const auto & segment : elfReader->segments)
	{
		targetMemorySectionsTempFileNames << QString("section-%1-%2.bin").arg(i).arg(x);
		/* There is no machine interface command for dumping target memory to files, so use the regular gdb commands. */
		gdbRequest += QString("dump binary memory %1 0x%2 0x%3\n")
				.arg(targetMemorySectionsTempFileNames.back())
				.arg(segment->get_physical_address(), 8, 16, QChar('0'))
				.arg(segment->get_physical_address() + segment->get_file_size(), 8, 16, QChar('0'));
		i ++;
	}
	/* As regular gdb commands are being used, insert a sequence point to know when to check the retrieved target memory areas. */
	unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
			   GdbTokenContext::GdbResponseContext::GDB_SEQUENCE_POINT_CHECK_MEMORY_CONTENTS));
	gdbRequest += QString("%1\n").arg(t);
	sendDataToGdbProcess(gdbRequest);
}

void MainWindow::gdbStarted()
{
	{ *(int*)0=0; }
}
