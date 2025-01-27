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
#include <QTextBlock>

#include "clex/cscanner.hxx"

using namespace ELFIO;

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	/* Force a windows style for the user interface.
	 * It looks like the most compact user interface. */
	QApplication::setStyle("windows");

	ui->setupUi(this);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

	/* Move some widgets to the main toolbar. This makes the user interface not so cluttered. */
	ui->mainToolBar->addWidget(ui->pushButtonShowWindow);
	ui->mainToolBar->addWidget(ui->pushButtonLocateWindow);
	ui->mainToolBar->addWidget(ui->comboBoxSelectLayout);
	ui->mainToolBar->addWidget(ui->pushButtonSettings);
	ui->mainToolBar->addWidget(ui->pushButtonDisplayHelp);
	ui->mainToolBar->addWidget(ui->pushButtonNavigateBack);
	ui->mainToolBar->addWidget(ui->pushButtonNavigateForward);
	ui->mainToolBar->addWidget(ui->pushButtonRESTART);
	ui->mainToolBar->addWidget(ui->pushButtonConnectToBlackmagic);

	QWidget * toolbarSpacers[1];
	for (auto & s : toolbarSpacers)
	{
		s = new QWidget();
		s->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	}
	ui->mainToolBar->addWidget(ui->toolButtonActions);
	ui->mainToolBar->addWidget(toolbarSpacers[0]);

	ui->mainToolBar->addWidget(ui->labelSystemState);

	/* For the moment, hide the short status widget. */
	//ui->mainToolBar->addWidget(ui->pushButtonShortState);
	ui->pushButtonShortState->setVisible(false);

	ui->toolButtonActions->addAction(ui->actionVerifyTargetFlash);
	ui->toolButtonActions->addAction(ui->actionLoadProgramIntoTarget);
	ui->toolButtonActions->addAction(ui->actionDisconnectGdbServer);
	ui->toolButtonActions->addAction(ui->actionShowTargetOutput);
	ui->toolButtonActions->addAction(ui->actionactionScanForTargets);
	connect(ui->mainToolBar, & QToolBar::visibilityChanged, [&] { if (!ui->mainToolBar->isVisible()) ui->mainToolBar->setVisible(true); });

	/* If this flag is set, then provide a default user interface layout. */
	bool isDefaultConfigRequested = !QFile::exists(SETTINGS_FILE_NAME);

	/* Clearing the titles of the group boxes makes the user interface look more tidy. Don't just delete them in Qt Designer - it is helpful to have the titles when
	 * tweaking the user interface there. */
	ui->groupBoxTargetConnected->setTitle("");
	ui->groupBoxTargetHalted->setTitle("");
	ui->groupBoxTargetRunning->setTitle("");

	settings = std::make_shared<QSettings>(SETTINGS_FILE_NAME, QSettings::IniFormat);
	restoreState(settings->value(SETTINGS_MAINWINDOW_STATE, QByteArray()).toByteArray());
	restoreGeometry(settings->value(SETTINGS_MAINWINDOW_GEOMETRY, QByteArray()).toByteArray());

	connect(ui->pushButtonNavigateForward, & QPushButton::clicked, [&] { if (navigationStack.canNavigateForward()) displaySourceCodeFile(navigationStack.following(), false); });
	connect(ui->pushButtonNavigateBack, & QPushButton::clicked, [&] { if (navigationStack.canNavigateBack()) displaySourceCodeFile(navigationStack.previous(), false); });
	connect(ui->pushButtonLocateWindow, & QPushButton::clicked, [&] { displayHelpMenu(); });

	connect(ui->pushButtonScanForTargets, & QPushButton::clicked, [&] { scanForTargets(); });

	connect(ui->pushButtonVerifyTargetMemory, & QPushButton::clicked, [&] { compareTargetMemory(); });
	connect(ui->actionVerifyTargetFlash, & QAction::triggered, [&] { compareTargetMemory(); });
	connect(ui->actionLoadProgramIntoTarget, & QAction::triggered, [&] { sendDataToGdbProcess("-target-download\n"); });
	connect(ui->actionDisconnectGdbServer, & QAction::triggered, [&] { sendDataToGdbProcess("-target-disconnect\n"); });
	connect(ui->actionactionScanForTargets, & QAction::triggered, [&] { scanForTargets(); });

	/*! \todo	This is too verbose, should be improved. This should be moved to a separate function. */
	targetStateDependentWidgets.enabledActionsWhenTargetStopped << ui->actionVerifyTargetFlash << ui->actionLoadProgramIntoTarget << ui->actionDisconnectGdbServer << ui->actionactionScanForTargets;
	targetStateDependentWidgets.disabledActionsWhenTargetRunning << ui->actionVerifyTargetFlash << ui->actionLoadProgramIntoTarget << ui->actionDisconnectGdbServer << ui->actionactionScanForTargets;
	targetStateDependentWidgets.disabledActionsWhenTargetDetached << ui->actionVerifyTargetFlash << ui->actionLoadProgramIntoTarget;
	targetStateDependentWidgets.enabledActionsWhenTargetDetached << ui->actionDisconnectGdbServer << ui->actionactionScanForTargets;
	targetStateDependentWidgets.disabledActionsWhenGdbServerDisconnected << ui->actionVerifyTargetFlash << ui->actionLoadProgramIntoTarget << ui->actionDisconnectGdbServer << ui->actionactionScanForTargets;

	targetStateDependentWidgets.enabledWidgetsWhenTargetStopped << ui->dockWidgetContentsMemoryDump << ui->groupBoxTargetHalted << ui->groupBoxTargetConnected;
	targetStateDependentWidgets.disabledWidgetsWhenTargetStopped << ui->groupBoxTargetRunning;
	targetStateDependentWidgets.enabledWidgetsWhenTargetRunning << ui->groupBoxTargetRunning << ui->groupBoxTargetConnected;
	targetStateDependentWidgets.disabledWidgetsWhenTargetRunning << ui->dockWidgetContentsMemoryDump << ui->groupBoxTargetHalted;

	targetStateDependentWidgets.disabledWidgetsWhenGdbServerDisconnected << ui->dockWidgetContentsMemoryDump << ui->groupBoxTargetConnected << ui->pushButtonScanForTargets;

	targetStateDependentWidgets.disabledWidgetsWhenTargetDetached << targetStateDependentWidgets.enabledWidgetsWhenTargetRunning;
	targetStateDependentWidgets.disabledWidgetsWhenTargetDetached << targetStateDependentWidgets.enabledWidgetsWhenTargetStopped;
	/* The button for scanning for targets connected to the blackmagic probe is a bit special. */
	targetStateDependentWidgets.enabledWidgetsWhenTargetDetached << ui->pushButtonScanForTargets;
	targetStateDependentWidgets.enabledWidgetsWhenTargetStopped << ui->pushButtonScanForTargets;
	targetStateDependentWidgets.disabledWidgetsWhenTargetRunning << ui->pushButtonScanForTargets;

	connect(ui->pushButtonRequestGdbHalt, & QPushButton::clicked, [&]{ requestTargetHalt(); });
	connect(ui->pushButtonStepInto, & QPushButton::clicked, [&]{ if (target_state == TARGET_STOPPED) sendDataToGdbProcess("-exec-step\n"); });
	connect(ui->pushButtonStepOver, & QPushButton::clicked, [&]{ if (target_state == TARGET_STOPPED) sendDataToGdbProcess("-exec-next\n"); });
	connect(ui->pushButtonDisconnectGdb, & QPushButton::clicked, [&]{ sendDataToGdbProcess("-target-disconnect\n"); });

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
		/* Special case for the empty string - show all items in the tree. No need to do anything special. */
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
		std::function<void(QTreeWidgetItem * item)> makeSubtreeVisible = [&](QTreeWidgetItem * root) -> void
		{
			root->setHidden(false);
			for (int i = 0; i < root->childCount(); makeSubtreeVisible(root->child(i ++)))
			     ;
		};
		for (int i = 0; i < ui->treeWidgetSvd->topLevelItemCount(); scan(ui->treeWidgetSvd->topLevelItem(i ++)))
			;
		/* For matching items - make sure its parent items, upto the tree root, are visible.
		 * Also, make sure that all items in subtrees with, roots amongst the matching items, are visible. */
		for (auto & i : matchingItems)
		{
			QTreeWidgetItem * w = i->parent();
			while (w) w->setHidden(false), w = w->parent();
			makeSubtreeVisible(i);
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

	/*****************************************
	 * Configure the 'Settings' dialog.
	 *****************************************/
	loadSessions();
	dialogEditSettings = new QDialog(this);
	uiSettings.setupUi(dialogEditSettings);
	uiSettings.groupBoxAdvancedSettings->setVisible(false);
	connect(ui->pushButtonSettings, & QPushButton::clicked, [&](){
		populateSettingsDialog();
		/* Execute the settings dialog asynchronously. */
		dialogEditSettings->open();
		if ((QGuiApplication::queryKeyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == (Qt::ControlModifier | Qt::ShiftModifier))
			uiSettings.groupBoxAdvancedSettings->setVisible(true);
	});
	connect(uiSettings.pushButtonSelectGdbExecutableFile, & QPushButton::clicked, [&](){
		QString s = QFileDialog::getOpenFileName(0, "Select gdb executable");
		if (!s.isEmpty())
			uiSettings.lineEditGdbExecutable->setText(s);
	});
	connect(uiSettings.pushButtonSelectTargetSVDFile, & QPushButton::clicked, [&](){
		QString s = QFileDialog::getOpenFileName(0, "Select target SVD file", QFileInfo(uiSettings.lineEditTargetSVDFileName->text()).absoluteFilePath());
		if (!s.isEmpty())
			uiSettings.lineEditTargetSVDFileName->setText(s);
	});
	connect(uiSettings.pushButtonSelectExternalEditor, & QPushButton::clicked, [&](){
		QString s = QFileDialog::getOpenFileName(0, "Select external editor", QFileInfo(uiSettings.lineEditExternalEditorOptions->text()).absoluteFilePath());
		if (!s.isEmpty())
			uiSettings.lineEditExternalEditorProgram->setText(s);
	});
	connect(uiSettings.pushButtonSettingsOk, & QPushButton::clicked, [&](){
		settings->setValue(SETTINGS_GDB_EXECUTABLE_FILENAME, uiSettings.lineEditGdbExecutable->text());
		settings->setValue(SETTINGS_EXTERNAL_EDITOR_PROGRAM, uiSettings.lineEditExternalEditorProgram->text());
		settings->setValue(SETTINGS_EXTERNAL_EDITOR_COMMAND_LINE_OPTIONS, uiSettings.lineEditExternalEditorOptions->text());
		targetSVDFileName = uiSettings.lineEditTargetSVDFileName->text();
		settings->setValue(SETTINGS_CHECKBOX_ENABLE_NATIVE_DEBUGGING_STATE, uiSettings.checkBoxEnableNativeDebugging->isChecked());
		settings->setValue(SETTINGS_CHECKBOX_HIDE_LESS_USED_UI_ITEMS, uiSettings.checkBoxHideLessUsedUiItems->isChecked());
		dialogEditSettings->hide();
	});
	connect(uiSettings.pushButtonSettingsCancel, & QPushButton::clicked, [&]()
		{
			uiSettings.checkBoxHideLessUsedUiItems->setChecked(settings->value(SETTINGS_CHECKBOX_HIDE_LESS_USED_UI_ITEMS, false).toBool());
			dialogEditSettings->hide();
		});
	connect(dialogEditSettings, & QDialog::rejected, [&] { uiSettings.pushButtonSettingsCancel->click(); });
	/*****************************************
	 * End 'Settings' dialog configuration.
	 *****************************************/

	/********************************************************
	 * Configure the 'Choose file for debugging' dialog.
	 ********************************************************/
	dialogChooseFileForDebugging = new QDialog(this);
	uiChooseFileForDebugging.setupUi(dialogChooseFileForDebugging);
	for (const auto & s : sessions)
	{
		QFileInfo fi(s.executableFileName);
		uiChooseFileForDebugging.treeWidgetRecentDebugExecutables->addTopLevelItem(new QTreeWidgetItem(QStringList() << fi.fileName() << fi.filePath()));
	}
	uiChooseFileForDebugging.lineEditDebugExecutable->setText(settings->value(SETTINGS_LAST_LOADED_EXECUTABLE_FILE, "").toString());

	uiChooseFileForDebugging.treeWidgetRecentDebugExecutables->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	uiChooseFileForDebugging.treeWidgetRecentDebugExecutables->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

	/*! \todo	Also connect the 'itemActivated()' signal here. */
	connect(uiChooseFileForDebugging.treeWidgetRecentDebugExecutables, & QTreeWidget::itemClicked, [&](QTreeWidgetItem * item, int column) -> void
		{
			uiChooseFileForDebugging.lineEditDebugExecutable->setText(item->text(1));
		});
	connect(uiChooseFileForDebugging.pushButtonSelectFile, & QPushButton::clicked, [&]
		{
			QString fileName = QFileDialog::getOpenFileName(0, "Choose executable file for debugging");
			if (fileName.size())
				uiChooseFileForDebugging.lineEditDebugExecutable->setText(fileName);
		});
	connect(uiChooseFileForDebugging.pushButtonOk, & QPushButton::clicked, [&] { dialogChooseFileForDebugging->accept(); });
	connect(uiChooseFileForDebugging.pushButtonCancel, & QPushButton::clicked, [&] { dialogChooseFileForDebugging->reject(); });

	/********************************************************
	 * End 'Choose file for debugging' dialog configuration.
	 ********************************************************/

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
	ui->actionShowTargetOutput->setChecked(flag);
	connect(ui->checkBoxShowTargetOutput, & QCheckBox::stateChanged, [&](int newState) { ui->groupBoxTargetOutput->setVisible(newState != Qt::Unchecked); ui->actionShowTargetOutput->setChecked(newState); });
	connect(ui->actionShowTargetOutput, & QAction::toggled, [&](bool newState) { ui->checkBoxShowTargetOutput->setChecked(newState); });
	connect(ui->pushButtonHideTargetOutputView, & QPushButton::clicked, [&] { ui->checkBoxShowTargetOutput->setCheckState(Qt::Unchecked); });

	ui->plainTextEditGdbLog->setMaximumBlockCount(MAX_GDB_LINE_COUNT_IN_GDB_LIMITING_MODE);
	connect(ui->checkBoxLimitGdbLog, & QCheckBox::stateChanged, [&](int newState)
		{
			ui->plainTextEditGdbLog->setMaximumBlockCount((newState == Qt::Checked) ? MAX_GDB_LINE_COUNT_IN_GDB_LIMITING_MODE : 0);
		});

	lessUsedUiItems << ui->pushButtonVerifyTargetMemory << ui->pushButtonLoadProgramToTarget << ui->pushButtonDisconnectGdb << ui->checkBoxShowTargetOutput;
	connect(uiSettings.checkBoxHideLessUsedUiItems, & QCheckBox::stateChanged, [&](int newState)
		{
			for (auto & w : lessUsedUiItems) w->setHidden(newState);
		});
	ui->checkBoxLimitGdbLog->setChecked(settings->value(SETTINGS_CHECKBOX_GDB_OUTPUT_LIMITING_MODE_STATE, true).toBool());
	ui->checkBoxHideGdbMIData->setChecked(settings->value(SETTINGS_CHECKBOX_HIDE_GDB_MI_DATA_STATE, true).toBool());

	uiSettings.checkBoxHideLessUsedUiItems->setChecked(settings->value(SETTINGS_CHECKBOX_HIDE_LESS_USED_UI_ITEMS, false).toBool());

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
			QString gdbExecutableFileName = settings->value(SETTINGS_GDB_EXECUTABLE_FILENAME, "").toString();
			/* If the gdb executable is not set, ask the user to specify a valid gdb executable. */
			if (gdbExecutableFileName.isEmpty())
			{
				QMessageBox::critical(0, "Gdb executable not set", "Gdb executable not specified. Please, specify a valid gdb executable.");
				populateSettingsDialog();
				uiSettings.lineEditGdbExecutable->setFocus();
				/* Execute the settings dialog synchronously. */
				dialogEditSettings->exec();
				/* Do not check again if the gdb executable is available. */
				gdbExecutableFileName = settings->value(SETTINGS_GDB_EXECUTABLE_FILENAME, "").toString();
			}
			gdbProcess->setProgram(gdbExecutableFileName);
			gdbProcess->start();
		}
	});

	connect(gdbProcess.get(), & QProcess::started, [&] {
		targetStateDependentWidgets.enterTargetState(target_state = GDBSERVER_DISCONNECTED, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);
		ui->pushButtonStartGdb->setEnabled(false);
		ui->pushButtonConnectToBlackmagic->setEnabled(true);
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

	connect(ui->actionSourceFilesViewShowFullFileNames, & QAction::toggled, [&](bool checked) { ui->treeWidgetSourceFiles->setColumnHidden(1, !checked); });
	ui->actionSourceFilesViewShowFullFileNames->setChecked(settings->value(SETTINGS_BOOL_SHOW_FULL_FILE_NAME_STATE, false).toBool());
	ui->toolButtonSourceFilesViewOptions->addAction(ui->actionSourceFilesViewShowFullFileNames);
	/* Force updating of the full file name column in the source files widget. */
	ui->actionSourceFilesViewShowFullFileNames->toggled(ui->actionSourceFilesViewShowFullFileNames->isChecked());

	connect(ui->actionSourceFilesShowOnlyFilesWithMachineCode, & QAction::toggled, [&] { updateSourceListView(); });
	connect(ui->actionSourceFilesShowOnlyExistingFiles, & QAction::toggled, [&] { updateSourceListView(); });
	ui->actionSourceFilesShowOnlyFilesWithMachineCode->setChecked(settings->value(SETTINGS_BOOL_SHOW_ONLY_SOURCES_WITH_MACHINE_CODE_STATE, false).toBool());
	ui->actionSourceFilesShowOnlyExistingFiles->setChecked(settings->value(SETTINGS_BOOL_SHOW_ONLY_EXISTING_SOURCE_FILES, false).toBool());
	ui->toolButtonSourceFilesViewOptions->addAction(ui->actionSourceFilesShowOnlyFilesWithMachineCode);
	ui->toolButtonSourceFilesViewOptions->addAction(ui->actionSourceFilesShowOnlyExistingFiles);


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

	connect(ui->treeWidgetBacktrace, & QTreeWidget::itemActivated, [=] (QTreeWidgetItem * item, int column)
		{ selectStackFrame(item); } );
	connect(ui->treeWidgetBacktrace, & QTreeWidget::itemClicked, [=] (QTreeWidgetItem * item, int column)
		{ selectStackFrame(item); } );

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
			      ) +
"QPlainTextEdit {\n"
	+ DEFAULT_PLAINTEXTEDIT_STYLESHEET +
"}\n"
"QLineEdit{\n"
    "font: 10pt 'Hack';\n"
"}\n"
		      );

	ui->plainTextEditScratchpad->setPlainText(settings->value(SETTINGS_SCRATCHPAD_TEXT_CONTENTS, QString("Lorem ipsum dolor sit amet")).toString());

	ui->plainTextEditSourceView->installEventFilter(this);
	ui->plainTextEditDisassembly->installEventFilter(this);
	ui->plainTextEditSourceView->viewport()->installEventFilter(this);

	/* Only execute the code below after gdb has been successfully started. */
	connect(gdbProcess.get(), & QProcess::started, [&] {
		sendDataToGdbProcess("-gdb-set tcp auto-retry off\n");
		sendDataToGdbProcess("-gdb-set mem inaccessible-by-default off\n");
		sendDataToGdbProcess("-gdb-set print elements unlimited\n");

		int result;
		QFileInfo fi;
		do (result = dialogChooseFileForDebugging->exec()); while (result == QDialog::Accepted && !(fi = QFileInfo(uiChooseFileForDebugging.lineEditDebugExecutable->text())).exists());
		if (result == QDialog::Accepted)
		{
			unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(
									   GdbTokenContext::GdbResponseContext::GDB_RESPONSE_EXECUTABLE_SYMBOL_FILE_LOADED,
									   fi.canonicalFilePath())
								   );
			sendDataToGdbProcess(QString("%1-file-exec-and-symbols \"%2\"\n").arg(t).arg(fi.canonicalFilePath()));
		}

	});
	gdbProcess->setArguments(QStringList() << "--interpreter=mi3");
	/* Make sure to first update the target state, and only then start the gdb process. */
	targetStateDependentWidgets.enterTargetState(target_state = GDB_NOT_RUNNING, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);
	ui->pushButtonStartGdb->click();

	ui->treeWidgetBreakpoints->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetBreakpoints, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(breakpointsContextMenuRequested(QPoint)));

	ui->treeWidgetBookmarks->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetBookmarks, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(bookmarksContextMenuRequested(QPoint)));

	ui->treeWidgetSvd->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeWidgetSvd, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(svdContextMenuRequested(QPoint)));

	ui->treeViewDataObjects->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->treeViewDataObjects, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(varObjectContextMenuRequested(QPoint)));

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
        targetCorefile = std::make_shared<TargetCorefile>(targetFilesBaseDirectory + "flash.bin", 0x08000000,
					    targetFilesBaseDirectory + "ram.bin", 0x20000000,
					    targetFilesBaseDirectory + "registers.bin");
	/* Use this to inspect coredumps instead of connecting to a real remote target. */
	////gdbserver = new GdbServer(targetCorefile);

	connect(& blackMagicProbeServer, & BlackMagicProbeServer::BlackMagicProbeConnected,
		[&] {
			ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: SpringGreen");
			ui->pushButtonConnectToBlackmagic->setText(tr("Blackmagic connected"));
			ui->pushButtonConnectToBlackmagic->setEnabled(false);
			/*! \todo	do not hardcode the gdb server listening port */
			sendDataToGdbProcess("-target-select extended-remote :1122\n");
			isBlackmagicProbeConnected = true;
			});
	connect(& blackMagicProbeServer, & BlackMagicProbeServer::BlackMagicProbeDisconnected,
		[&] {
			ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: Yellow");
			ui->pushButtonConnectToBlackmagic->setText(tr("Connect to blackmagic"));
			ui->pushButtonConnectToBlackmagic->setEnabled(true);
			isBlackmagicProbeConnected = false;
			if (target_state != GDBSERVER_DISCONNECTED && target_state != GDB_NOT_RUNNING)
				sendDataToGdbProcess("-target-disconnect\n");
			});
	ui->pushButtonConnectToBlackmagic->setStyleSheet("background-color: yellow");
	connect(ui->pushButtonConnectToBlackmagic, & QPushButton::clicked, [&] { blackMagicProbeServer.connectToProbe(); });

	connect(ui->pushButtonDisplayHelp, SIGNAL(clicked(bool)), this, SLOT(displayHelp()));
	displayHelp();

	/***************************************
	 * Gdb and target state change handling.
	 ***************************************/
	connect(this, &MainWindow::gdbServerConnected, [&] {
		/* A connection to the gdbserver has been established, but a connection to a target is not yet established. */
		////QMessageBox::information(0, "Gdb connection established", "Gdb successfully connected to remote gdb server");
		targetStateDependentWidgets.enterTargetState(target_state = TARGET_DETACHED, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);
		ui->pushButtonScanForTargets->click();
	});

	connect(&blackMagicProbeServer, &BlackMagicProbeServer::GdbClientDisconnected, [&]
		{ targetStateDependentWidgets.enterTargetState(target_state = GDBSERVER_DISCONNECTED, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);}
	);

	connect(this, &MainWindow::targetStopped, [&] {
		if (target_state == GDBSERVER_DISCONNECTED || target_state == TARGET_DETACHED)
			compareTargetMemory();
		targetStateDependentWidgets.enterTargetState(target_state = TARGET_STOPPED, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);
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

	connect(this, &MainWindow::targetRunning, [&] {
		targetStateDependentWidgets.enterTargetState(target_state = TARGET_RUNNING, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);
	});

	connect(this, &MainWindow::targetDetached, [&]
		/*! \todo	This is getting too complicated... It is problematic to distinguish between a gdbserver detach and a gdbserver disconnect event.
		 *		The target state handling needs to be improved and simplified.
		 *		For the moment, try to do some special case handling - if the target state is GDBSERVER_DISCONNECTED, then stay in the disconnected state. */
		{ if (target_state != GDBSERVER_DISCONNECTED) targetStateDependentWidgets.enterTargetState(target_state = TARGET_DETACHED, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);}
	);

	/***************************************
	 ***************************************
	 ***************************************/

	connect(ui->pushButtonResetAndRunTarget, & QPushButton::clicked, [&] { sendDataToGdbProcess("-exec-run\n"); });
	connect(ui->pushButtonContinue, & QPushButton::clicked, [&] { sendDataToGdbProcess("-exec-continue\n"); });

	stringFinder = new StringFinder();
	stringFinder->moveToThread(&fileSearchThread);
	connect(this, SIGNAL(findString(QString,uint)), stringFinder, SLOT(findString(QString,uint)));
	connect(this, SIGNAL(addFilesToSearchSet(QStringList)), stringFinder, SLOT(addFilesToSearchSet(QStringList)));

	widgetFlashHighlighterData.timer.setSingleShot(true);
	widgetFlashHighlighterData.timer.setInterval(widgetFlashHighlighterData.flashIntervalMs);
	connect(& widgetFlashHighlighterData.timer, SIGNAL(timeout()), this, SLOT(updateHighlightedWidget()));

	auto makeHighlightAction = [&, this] (const QString & actionText, const QString & shortcut, QDockWidget * w) -> void
	{
		QAction * act;
		act = new QAction(actionText, this);
		if (!shortcut.isEmpty())
			act->setShortcut(shortcut);
		connect(act, &QAction::triggered, [=] { flashHighlightDockWidget(w); });
		highlightWidgetActions << act;
	};
	makeHighlightAction(ui->dockWidgetBacktrace->windowTitle(), "Ctrl+t", ui->dockWidgetBacktrace);
	makeHighlightAction(ui->dockWidgetBookmarks->windowTitle(), "Ctrl+b", ui->dockWidgetBookmarks);
	makeHighlightAction(ui->dockWidgetBreakpoints->windowTitle(), "Ctrl+r", ui->dockWidgetBreakpoints);
	makeHighlightAction(ui->dockWidgetDataObjects->windowTitle(), "Ctrl+d", ui->dockWidgetDataObjects);
	makeHighlightAction(ui->dockWidgetSearchResults->windowTitle(), "Ctrl+s", ui->dockWidgetSearchResults);
	makeHighlightAction(ui->dockWidgetSourceFiles->windowTitle(), "Ctrl+l", ui->dockWidgetSourceFiles);
	makeHighlightAction(ui->dockWidgetSubprograms->windowTitle(), "Ctrl+u", ui->dockWidgetSubprograms);
	makeHighlightAction(ui->dockWidgetScratchpad->windowTitle(), "", ui->dockWidgetScratchpad);
	makeHighlightAction(ui->dockWidgetStaticDataObjects->windowTitle(), "", ui->dockWidgetStaticDataObjects);
	makeHighlightAction(ui->dockWidgetObjectLocator->windowTitle(), "", ui->dockWidgetObjectLocator);
	makeHighlightAction(ui->dockWidgetRegisters->windowTitle(), "", ui->dockWidgetRegisters);
	makeHighlightAction(ui->dockWidgetDataTypes->windowTitle(), "", ui->dockWidgetDataTypes);
	makeHighlightAction(ui->dockWidgetSvdView->windowTitle(), "", ui->dockWidgetSvdView);
	makeHighlightAction(ui->dockWidgetMemoryDump->windowTitle(), "", ui->dockWidgetMemoryDump);
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

	if (isDefaultConfigRequested)
	{
		/* Provide a sensible default user interface layout. This is pretty arbitrary. */
		ui->comboBoxSelectLayout->activated(defaultLayoutIndex);
		ui->pushButtonHideGdbConsoles->click();
		ui->pushButtonHideDisassembly->click();
		ui->pushButtonHideTargetOutputView->click();
	}

	ui->plainTextEditSourceView->setTabStopDistance(8 * ui->plainTextEditSourceView->fontMetrics().width(' '));
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

void MainWindow::selectStackFrame(QTreeWidgetItem *item)
{
	sendDataToGdbProcess(QString("-stack-select-frame %1\n").arg(ui->treeWidgetBacktrace->indexOfTopLevelItem(item)));
	sendDataToGdbProcess("-stack-info-frame\n");
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
	settings->setValue(SETTINGS_BOOL_SHOW_FULL_FILE_NAME_STATE, ui->actionSourceFilesViewShowFullFileNames->isChecked());
	settings->setValue(SETTINGS_BOOL_SHOW_ONLY_SOURCES_WITH_MACHINE_CODE_STATE, ui->actionSourceFilesShowOnlyFilesWithMachineCode->isChecked());
	settings->setValue(SETTINGS_BOOL_SHOW_ONLY_EXISTING_SOURCE_FILES, ui->actionSourceFilesShowOnlyExistingFiles->isChecked());
	settings->setValue(SETTINGS_SCRATCHPAD_TEXT_CONTENTS, ui->plainTextEditScratchpad->document()->toPlainText());

	settings->setValue(SETTINGS_SPLITTER_VERTICAL_SOURCE_VIEW_STATE, ui->splitterVerticalSourceView->saveState());
	settings->setValue(SETTINGS_SPLITTER_HORIZONTAL_GDB_CONSOLES_STATE, ui->splitterHorizontalGdbConsoles->saveState());
	settings->setValue(SETTINGS_SPLITTER_HORIZONTAL_SOURCE_VIEW_STATE, ui->splitterHorizontalSourceView->saveState());

	settings->setValue(SETTINGS_IS_SPLITTER_HORIZONTAL_GDB_CONSOLES_VISIBLE, ui->splitterHorizontalGdbConsoles->isVisible());
	settings->setValue(SETTINGS_IS_DISASSEMBLY_VIEW_VISIBLE, ui->groupBoxDisassembly->isVisible());
	settings->setValue(SETTINGS_IS_TARGET_OUTPUT_VIEW_VISIBLE, ui->groupBoxTargetOutput->isVisible());

	settings->setValue(SETTINGS_CHECKBOX_GDB_OUTPUT_LIMITING_MODE_STATE, ui->checkBoxLimitGdbLog->isChecked());
	settings->setValue(SETTINGS_CHECKBOX_HIDE_GDB_MI_DATA_STATE, ui->checkBoxHideGdbMIData->isChecked());

	if ((QGuiApplication::queryKeyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == (Qt::ControlModifier | Qt::ShiftModifier))
		if (QMessageBox::question(0, "Delete configuration data?", "Really delete configuration data?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
			if (QFile::remove(SETTINGS_FILE_NAME))
				QMessageBox::information(0, "Configuration settings deleted", "The configuration settings have been deleted.\nThe frontend will next time start in the default configuration.");

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
		}
	}
	return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == ui->plainTextEditSourceView->viewport() && event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
		bool result = false;
		if (mouseEvent->button() == Qt::LeftButton)
		{
			if (mouseEvent->modifiers() == Qt::ControlModifier)
				result = true, navigateToSymbolAtCursor();
			else if (mouseEvent->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
			{
				result = true;
				QTextCursor c = ui->plainTextEditSourceView->textCursor();
				c.select(QTextCursor::WordUnderCursor);
				QString s = c.selectedText();
				if (!s.isEmpty())
				{
					if (mouseEvent->modifiers() == Qt::ShiftModifier)
						emit findString(s, ui->checkBoxSearchForWholeWordsOnly->isChecked() ? StringFinder::SEARCH_FOR_WHOLE_WORDS_ONLY : 0);
					if (mouseEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
						ui->lineEditObjectLocator->setText(s);
				}
			}
		}
		return result;
	}

	if (watched == ui->plainTextEditDisassembly && event->type() == QEvent::KeyPress)
	{
		QKeyEvent * e = static_cast<QKeyEvent *>(event);
		if (e->key() == Qt::Key_Space)
		{
			/* Toggle breakpoint at current disassembly view block. */
			const DisassemblyCache::DisassemblyBlock & t =
				disassemblyCache.disassemblyBlockForTextBlockNumber(ui->plainTextEditDisassembly->textCursor().blockNumber());
			if (t.kind != DisassemblyCache::DisassemblyBlock::INVALID)
			{
				std::vector<const GdbBreakpointData *> b;
				if (t.kind == DisassemblyCache::DisassemblyBlock::SOURCE_LINE)
				{
					GdbBreakpointData::breakpointsForSourceCodeLineNumber(SourceCodeLocation(t.fullFileName, t.lineNumber), breakpoints, b);
					if (b.empty())
						/* Breakpoint not found for the source code line - insert one. */
						sendDataToGdbProcess(QString("-break-insert --source \"%1\" --line %2\n")
								     .arg(escapeString(t.fullFileName))
								     .arg(t.lineNumber));
					else
					{
						/* Breakpoint(s) found for the source code line - remove them. */
						for (const auto & t : b)
							sendDataToGdbProcess(QString("-break-delete %1\n").arg(t->gdbReportedNumberString));
					}
				}
				else if (t.kind == DisassemblyCache::DisassemblyBlock::DISASSEMBLY_LINE)
				{
					GdbBreakpointData::breakpointsForAddress(t.address, breakpoints, b);
					if (b.empty())
						/* Breakpoint not found at address - insert one. */
						sendDataToGdbProcess(QString("-break-insert *0x%1\n")
								     .arg(t.address, 0, 16));
					else
					{
						/* Breakpoint(s) found for address - remove them. */
						for (const auto & t : b)
							sendDataToGdbProcess(QString("-break-delete %1\n").arg(t->gdbReportedNumberString));
					}
				}
				/* Reread the list of breakpoints. */
				sendDataToGdbProcess("-break-list\n");
			}
			return true;
		}
		return false;
	}

	bool result = false;
	if (watched == ui->plainTextEditSourceView && event->type() == QEvent::KeyPress)
	{
		QKeyEvent * e = static_cast<QKeyEvent *>(event);
		switch (e->key())
		{
		case Qt::Key_Escape:
			/* Emulate Qt Creator's behavior of decluttering the views by pressing the ESCape key. */
			if (ui->checkBoxShowTargetOutput->isChecked())
				ui->checkBoxShowTargetOutput->setChecked(false);
			else if (ui->checkBoxShowGdbConsoles->isChecked())
				ui->checkBoxShowGdbConsoles->setChecked(false);
			else if (ui->checkBoxShowDisassembly->isChecked())
				ui->checkBoxShowDisassembly->setChecked(false);
			result = true;
			break;
		case Qt::Key_S:
			if (target_state == TARGET_STOPPED)
				sendDataToGdbProcess("-exec-step\n");
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
				result = true;
				QString sourceFilename = displayedSourceCodeFile;
				/* Special case for internal files - do not attempt to open them in an external editor. */
				if (sourceFilename.startsWith(":/"))
					break;
				if (!QFileInfo(sourceFilename).exists())
					sourceFilename = Utils::filenameToWindowsFilename(sourceFilename);
				QString editor = settings->value(SETTINGS_EXTERNAL_EDITOR_PROGRAM, "").toString();
				if (!QFileInfo(editor).exists())
					editor = Utils::filenameToWindowsFilename(editor);
				QStringList editorCommandOptions = settings->value(SETTINGS_EXTERNAL_EDITOR_COMMAND_LINE_OPTIONS, "").toString().split(QRegularExpression("\\s+"));
				for (auto & t : editorCommandOptions)
					t.replace("%FILE", sourceFilename).replace("%LINE", QString("%1").arg(ui->plainTextEditSourceView->textCursor().blockNumber() + 1));
				if (!QProcess::startDetached(editor,
							editorCommandOptions,
							sourceFilename.isEmpty() ? QApplication::applicationDirPath() : QFileInfo(sourceFilename).canonicalPath()))
				{
					QMessageBox::critical(0, "Error starting external editor", "Starting the external editor failed.\nPlease, review the external editor settings.");
					uiSettings.lineEditExternalEditorProgram->setFocus();
					ui->pushButtonSettings->click();
				}
			}

			break;
		case Qt::Key_F3:
		case Qt::Key_N:
			result = true;
			/*! \todo	Remove this, it is no longer needed. */
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
	QRegularExpression rxGdbPrompt("\\(gdb\\)s*");
	bool isGdbPromptRecord =  rxGdbPrompt.match(line).hasMatch();
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
			if (!ui->checkBoxHideGdbMIData->isChecked())
				appendLineToGdbLog((tokenNumber ? QString("%1").arg(tokenNumber) : QString()) + line);
			{
				std::vector<GdbMiParser::MIResult> results;
				GdbMiParser parser;
				enum GdbMiParser::RESULT_CLASS_ENUM result = parser.parse(line.toStdString(), results);
				/* Try all command handlers, until some of them handles the response. */
				if (
				handleFilesResponse(result, results, tokenNumber) ||
				handleLinesResponse(result, results, tokenNumber) ||
				handleNameResponse(result, results, tokenNumber) ||
				handleNumchildResponse(result, results, tokenNumber) ||
				handleFileExecAndSymbolsResponse(result, results, tokenNumber) ||
				handleSequencePoints(result, results, tokenNumber) ||
				handleTargetScanResponse(result, results, tokenNumber) ||
				handleSymbolsResponse(result, results, tokenNumber) ||
				handleBreakpointTableResponse(result, results, tokenNumber) ||
				handleStackResponse(result, results, tokenNumber) ||
				handleRegisterNamesResponse(result, results, tokenNumber) ||
				handleRegisterValuesResponse(result, results, tokenNumber) ||
				handleChangelistResponse(result, results, tokenNumber) ||
				handleVariablesResponse(result, results, tokenNumber) ||
				handleFrameResponse(result, results, tokenNumber) ||
				handleDisassemblyResponse(result, results, tokenNumber) ||
				handleValueResponse(result, results, tokenNumber) ||
				handleVerifyTargetMemoryContentsSeqPoint(result, results, tokenNumber) ||
				handleMemoryResponse(result, results, tokenNumber) ||
				false)
					break;
				switch (result)
				{
					case GdbMiParser::DONE:
						break;
					case GdbMiParser::ERROR:
						handleGdbError(result, results, tokenNumber);
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
					qDebug() << "Failed to parse gdb reply:" << line;
					QMessageBox::critical(0, "Internal frontend error",
							      "This frontend has failed to parse a reply from gdb\n\n"
							      "This can happen on some obscure occasions (such as trying to parse\n"
							      "a 'script' field entry in a 'BreakpointTable' response for tracepoints)\n"
							      "where gdb violates its own documented response grammar\n\n"
							      "Please, report the debug output of this frontend, so that I may improve it!\n\n"
							      "At this point, it is recommended that you RESTART this frontend. It can no longer be trusted\n\n"
							      "Thank you!");
				}
			}
			break;
		case '@':
		/* Target stream output - put it along with the console output, capturing it for later
		 * processing, if necessary. */
			targetDataCapture.captureLine(line.right(line.length() - 1));
			/* FALLTHROUGH */
		default:
			/* Special case - filter out duplicate consecutive'(gdb)' prompt responses, if needed, to make the gdb log more pretty. */
			if (!isGdbPromptRecord || !ui->checkBoxHideGdbMIData->isChecked()
					|| !rxGdbPrompt.match(ui->plainTextEditGdbLog->document()->lastBlock().text()).hasMatch())
				appendLineToGdbLog(line);
			break;
		case '=':
			if (!ui->checkBoxHideGdbMIData->isChecked())
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
			/* Note - it is problematic to precisely distinguish between a gdb 'detach' and 'disconnect' responses, they are, in fact
			 * almost identical:
				>>> -target-detach

				=thread-exited,id="1",group-id="i1"
				=thread-group-exited,id="i1"
				[Inferior 1 (Remote target) detached]
				^done
				(gdb)
				...
				>>> -target-disconnect

				=thread-exited,id="1",group-id="i1"
				=thread-group-exited,id="i1"
				^done
				(gdb)
			 */
			else if (line.startsWith("=thread-group-exited"))
			{
				emit targetDetached();
				/* Do not print an information message on target detach. It is getting tedious, and the target status is already visualized anyway. */
				//QMessageBox::information(0, "Target detached", "Gdb has detached from the target");
			}
			break;
	}
	/* Remove the 'gdbTokenContext' for the current token number, if not already removed above. */
	gdbTokenContext.removeContext(tokenNumber);
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
	QTextCursor c = ui->plainTextEditGdbLog->textCursor();
	c.movePosition(QTextCursor::End);
	ui->plainTextEditGdbLog->setTextCursor(c);
}

bool MainWindow::handleNameResponse(enum GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (!context || context->gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_NAME)
		return false;
	struct GdbVarObjectTreeItem * node = new GdbVarObjectTreeItem;

	node->name = context->s;
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
	static_cast<GdbVarObjectTreeItemModel *>(context->p)->appendRootItem(node);
	return true;
}

bool MainWindow::handleNumchildResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (!context || context->gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_NUMCHILD)
		return false;

	QModelIndex index = varObjectTreeItemModel.indexForMiVariableName(context->s);
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
	sourceFiles->clear();
	for (const auto & t : results.at(0).value->asList()->values)
	{
		if (!t->asTuple())
		{
			QMessageBox::critical(0, "Internal frontend error", "Internal frontend error - failed to parse gdb response. Please, report this");
			return false;
		}
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
		sourceFiles->operator [](s.fullFileName) = s;
	}
	updateSourceListView();

	/* Retrieve source line addresses for all source code files reported. */
	for (const auto & f : sourceFiles.operator *())
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
	for (const auto & f : sourceFiles.operator *())
		sourceCodeFilenames << f.fullFileName;

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
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (!context || context->gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_LINES)
		return false;
	if (results.size() != 1 || results.at(0).variable != "lines" || !results.at(0).value->asList())
		return false;
	if (!sourceFiles->count(context->s))
		return false;

	SourceFileData & sourceFile(sourceFiles.operator *().operator [](context->s));
	for (const auto & t : results.at(0).value->asList()->values)
	{
		if (!t->asTuple())
		{
			QMessageBox::critical(0, "Internal frontend error", "Internal frontend error - failed to parse gdb response. Please, report this");
			return false;
		}
		int lineNumber = -1;
		for (const auto & v : t->asTuple()->map)
		{
			if (v.first == "line")
				lineNumber = QString::fromStdString(v.second->asConstant()->constant().c_str()).toULong(0, 0);
		}
		sourceFile.machineCodeLineNumbers.insert(lineNumber);
	}
	sourceFilesCache.setSourceFileData(sourceFiles);
	sourceFile.isSourceLinesFetched = true;
	return true;
}

bool MainWindow::handleSymbolsResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (!context)
		return false;
	if (context->gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_FUNCTION_SYMBOLS
			&& context->gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_VARIABLE_SYMBOLS
			&& context->gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_TYPE_SYMBOLS)
		return false;
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
				if (!sourceFiles.operator *().count(fullFileName))
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
					sourceFiles->operator[](fullFileName) = s;
				}
				if (context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_FUNCTION_SYMBOLS)
					sourceFiles->operator [](fullFileName).subprograms.insert(symbols.cbegin(), symbols.cend());
				else if (context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_VARIABLE_SYMBOLS)
					sourceFiles->operator [](fullFileName).variables.insert(symbols.cbegin(), symbols.cend());
				else
					sourceFiles->operator [](fullFileName).dataTypes.insert(symbols.cbegin(), symbols.cend());
			}
		}
	}

	/* Update the list of source code files that are searched. */
	QStringList sourceCodeFilenames;
	for (const auto & f : sourceFiles.operator *())
		sourceCodeFilenames << f.fullFileName;

	emit addFilesToSearchSet(sourceCodeFilenames);
	return true;
}

bool MainWindow::handleFileExecAndSymbolsResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (!context || context->gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_RESPONSE_EXECUTABLE_SYMBOL_FILE_LOADED)
		return false;
	if (parseResult == GdbMiParser::ERROR)
	{
		QString errorMessage = QString("Failed to load executable file in gdb, could not load file:\n%1").arg(context->s);
		if (results.size() && results.at(0).variable == "msg" && results.at(0).value->asConstant())
			errorMessage += QString("\n\n%1").arg(QString::fromStdString(results.at(0).value->asConstant()->constant()));
		errorMessage += "\n\n\nThe frontend will now restart, so that you may reliably select a valid executable file for debugging";
		QMessageBox::critical(0, "Error loading executable file in gdb",
				      errorMessage);
		ui->pushButtonRESTART->click();
		/* Should never */ return true;
	}

	settings->setValue(SETTINGS_LAST_LOADED_EXECUTABLE_FILE, context->s);
	elfReader = std::make_shared<elfio>();
	if (!elfReader->load(context->s.toStdString()))
		elfReader.reset();
	restoreSession(context->s);
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
		disassemblyCache.highlightLines(ui->plainTextEditDisassembly, breakpoints, lastKnownProgramCounter);
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

	/* If the disassembly view is currently non-visible, make it visible. */
	ui->checkBoxShowDisassembly->setChecked(true);
	QString disassemblyDocument;
	disassemblyCache.generateDisassemblyDocument(disassembly, sourceFilesCache, disassemblyDocument);
	ui->plainTextEditDisassembly->clear();
	ui->plainTextEditDisassembly->appendHtml(disassemblyDocument);

	QTextCursor c(ui->plainTextEditDisassembly->textCursor());
	c.movePosition(QTextCursor::Start);
	ui->plainTextEditDisassembly->setTextCursor(c);
	ui->plainTextEditDisassembly->centerCursor();
	disassemblyCache.highlightLines(ui->plainTextEditDisassembly, breakpoints, lastKnownProgramCounter, true);
	return true;
}

bool MainWindow::handleValueResponse(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE || results.size() != 1 || results.at(0).variable != "value" || !results.at(0).value->asConstant())
		return false;
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (context && context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_UPDATE_LAST_KNOWN_PROGRAM_COUNTER)
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
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (!context)
		return false;
	switch (context->gdbResponseCode)
	{
	case GdbTokenContext::GdbResponseContext::GDB_SEQUENCE_POINT_SOURCE_CODE_ADDRESSES_RETRIEVED:
		updateSourceListView();
		updateSymbolViews();
		return true;
	default:
		break;
	}
	return false;
}

bool MainWindow::handleVerifyTargetMemoryContentsSeqPoint(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::DONE)
		return false;
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (!context || context->gdbResponseCode != GdbTokenContext::GdbResponseContext::GDB_SEQUENCE_POINT_CHECK_MEMORY_CONTENTS)
		return false;
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
			auto choice = QMessageBox::question(0, "Target memory contents mismatch",
					      QString("The target memory contents are different from the memory contents of file:\n\n"
						      "%1\n\n"
						      "It is recommended that you update (reflash) the target memory.\n"
						      "Do you want to update (reflash) the target now?"
						      ).arg(settings->value(SETTINGS_LAST_LOADED_EXECUTABLE_FILE, "???").toString()),
							    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
			if (choice == QMessageBox::Yes)
				ui->actionLoadProgramIntoTarget->trigger();
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
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (context && context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_TARGET_SCAN_COMPLETE)
	{
		targetDataCapture.stopCapture();
		if (parseResult == GdbMiParser::ERROR)
		{
			QMessageBox::critical(0, "Target scan failed", QString("Target scan command failed, error:\n%1").arg(gdbErrorString(parseResult, results)));
			return true;
		}
		/* Try to parse any stream output from the target. */
		const QStringList & output(targetDataCapture.capturedLines());
		QStringList detectedTargets;
                QRegularExpression rx("^\\s*(\\d+)\\s+(.+)");
                for (auto l : output)
		{
                        /* Clean up the string a bit. */
                        l.replace('"', "").replace("\\n","");
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
			QMessageBox::information(0, "No target selected", "No target selected, aborting target connection.");
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
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (parseResult == GdbMiParser::ERROR)
	{
		/* If there is an error reading the target memory, disable the auto update of the memory view. */
		if (context && context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_DATA_READ_MEMORY)
			ui->checkBoxMemoryDumpAutoUpdate->setChecked(false);
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
	if (context && context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_DATA_READ_MEMORY)
	{
		ui->plainTextEditMemoryDump->clear();
		ui->plainTextEditMemoryDump->appendPlainText(data.toHex());
	}

	return true;
}

void MainWindow::handleGdbError(GdbMiParser::RESULT_CLASS_ENUM parseResult, const std::vector<GdbMiParser::MIResult> &results, unsigned tokenNumber)
{
	if (parseResult != GdbMiParser::ERROR)
		return;
	const struct GdbTokenContext::GdbResponseContext * context = gdbTokenContext.contextForTokenNumber(tokenNumber);
	if (context)
	{
		if (context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_FUNCTION_SYMBOLS
				|| context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_VARIABLE_SYMBOLS
				|| context->gdbResponseCode == GdbTokenContext::GdbResponseContext::GDB_RESPONSE_TYPE_SYMBOLS)
		{
			static bool symbolAccessMiErrrorPrinted = false;
			/* It is likely that the gdb executable available is not a recent one, as the machine interface commands for querying function, variable, and type symbols
			 * have been introduced in gdb version 10. */
			if (!symbolAccessMiErrrorPrinted)
			{
				symbolAccessMiErrrorPrinted = true;
				QMessageBox::critical(0, "Gdb version used is possibly out of date",
						      "A gdb symbol query machine interface command has failed.\n\n"
						      "Such gdb machine interface commands have only been introduced in recent gdb versions\n"
						      "(gdb versions 10.x and above).\n\n"
						      "Please, make sure you are running a recent gdb version.\n"
						      "Otherwise, the behaviour of the frontend will be suboptimal.\n"
						      "(This message shall not be printed again during this debug session."
						      );
			}
			return;
		}
	}
	QMessageBox::critical(0, "Gdb error", QString("Gdb error:\n%1").arg(gdbErrorString(parseResult, results)));
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
	targetStateDependentWidgets.enterTargetState(target_state = GDB_NOT_RUNNING, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);
	ui->pushButtonStartGdb->setEnabled(true);
	varObjectTreeItemModel.removeAllTopLevelItems();
	ui->pushButtonConnectToBlackmagic->setEnabled(false);
}

void MainWindow::gdbProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	qDebug() << gdbProcess->readAllStandardError();
	qDebug() << gdbProcess->readAllStandardOutput();
	qDebug() << "gdb process finished";
	targetStateDependentWidgets.enterTargetState(target_state = GDB_NOT_RUNNING, isBlackmagicProbeConnected, ui->labelSystemState, ui->pushButtonShortState);
	ui->pushButtonStartGdb->setEnabled(true);
	varObjectTreeItemModel.removeAllTopLevelItems();
	ui->pushButtonConnectToBlackmagic->setEnabled(false);
	if (exitStatus == QProcess::CrashExit)
	{
		if (QMessageBox::critical(0, "The gdb process crashed", "Gdb crashed\n\nDo you want to restart the gdb process?", "Restart gdb", "Abort")
				== 0)
			ui->pushButtonStartGdb->click();
	}
	else if (exitCode != 0)
	{
		if (QMessageBox::critical(0, "The gdb process exited with error", QString("Gdb exited with error code: %1\n\nDo you want to restart the gdb process?").arg(exitCode), "Restart gdb", "Abort")
				== 0)
			ui->pushButtonStartGdb->click();
	}
	else
	{
		if (QMessageBox::information(0, "The gdb process exited normally", "Gdb exited normally.\n\nDo you want to restart the gdb process?", "Restart gdb", "Abort")
				== 0)
			ui->pushButtonStartGdb->click();
	}
}

void MainWindow::sendDataToGdbProcess(const QString & data, bool isFrontendIssuedCommand)
{
	if (isFrontendIssuedCommand && !ui->checkBoxHideGdbMIData->isChecked())
		appendLineToGdbLog(">>> " + data);
	else if (!isFrontendIssuedCommand)
	{
		QTextCursor c = ui->plainTextEditGdbLog->textCursor();
		c.movePosition(QTextCursor::End);
		c.insertText(data);
		c.movePosition(QTextCursor::End);
		ui->plainTextEditGdbLog->setTextCursor(c);
	}
	/*! \todo	WARNING. This needs to be investigated, but attempting to send data to the gdb process, when
	 *		it is dead, of course - does not work, and this error is reported by Qt;
	 *
	 *		QWindowsPipeWriter::write failed. (The handle is invalid.)
	 *
	 * 		If the gdb process is then restarted, attempting to send data to it will continue NOT to work, with the same error.
	 *
	 *		However. Once dead, if the gdb process is restarted, but without attempting to send data to it
	 *		(and, therefore, without triggering the code displaying the error message above), once running, sending data
	 *		to the gdb process works without issues.
	 *
	 *		It looks like there is some additional handling of a QProcess being sent data, when it is dead, but as a
	 *		workaround here - just check if the gdb process is running, before sending data to it. This is not a solution,
	 *		it is a workaround, but it works very well for me. */
	if (gdbProcess->state() == QProcess::Running)
	{
		gdbProcess->write(data.toLocal8Bit());
		gdbProcess->waitForBytesWritten();
	}
	else
		appendLineToGdbLog("gdb process not running!!! Cannot send data to gdb");
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
		QAction * disassembleFile = 0, * disassembleSuprogram = 0, * insertBreakpoint = 0;
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
				insertBreakpoint = menu.addAction("Insert breakpoint");
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
			{
				QString disassemblyTarget = QString("-a \"%1\" -- 5").arg(w->text(0));
				QVariant v = w->data(0, SourceFileData::DISASSEMBLY_TARGET_COORDINATES);
				if (v.isValid())
					disassemblyTarget = v.toString();
				sendDataToGdbProcess(QString("-data-disassemble %1\n").arg(disassemblyTarget));
			}
			else if (selection == insertBreakpoint)
			{
				QString breakpointTarget = QString("--function \"%1\"").arg(w->text(0));
				QVariant v = w->data(0, SourceFileData::BREAKPOINT_TARGET_COORDINATES);
				if (v.isValid())
					breakpointTarget = v.toString();
				sendDataToGdbProcess(QString("-break-insert %1\n").arg(breakpointTarget));
				sendDataToGdbProcess("-break-list\n");
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

void MainWindow::varObjectContextMenuRequested(QPoint p)
{
	QModelIndex index = ui->treeViewDataObjects->indexAt(p);
	if (!index.isValid())
		return;
	const GdbVarObjectTreeItem * varObject = varObjectTreeItemModel.varoObjectTreeItemForIndex(index);
	if (varObject && /* Only allow deleting top-level varObjects. This looks like a sane behaviour. */ !index.parent().isValid())
	{
		qDebug() << varObject->miName;
		QMenu menu(this);
		QHash<void *, int> menuSelections;
		menuSelections.operator [](menu.addAction("Delete")) = 1;
		menu.addAction("Cancel");
		/* Because of the header of the tree view, it looks more natural to set the
		 * menu position on the screen at point translated from the tree view viewport,
		 * not from the tree view itself. */
		QAction * selection = menu.exec(ui->treeViewDataObjects->viewport()->mapToGlobal(p));

		switch (menuSelections.operator []((void *) selection))
		{
		case 1:
			/* Delete varObject. */
			varObjectTreeItemModel.removeTopLevelItem(index);
			sendDataToGdbProcess(QString("-var-delete %1\n").arg(varObject->miName));
			break;
		default:
			break;
		}
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
bool showOnlySourcesWithMachineCode = ui->actionSourceFilesShowOnlyFilesWithMachineCode->isChecked();
bool showOnlyExistingSourceFiles = ui->actionSourceFilesShowOnlyExistingFiles->isChecked();

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
	for (const auto & f : sourceFiles.operator *())
		if (shouldFileBeListed(f))
		{
			QTreeWidgetItem * t = createNavigationWidgetItem(QStringList() << f.fileName << f.fullFileName, f.fullFileName,
									 0, SourceFileData::SymbolData::SOURCE_FILE_NAME, false, true);
			ui->treeWidgetSourceFiles->addTopLevelItem(t);
			for (const auto & s : f.subprograms)
			{
				QTreeWidgetItem * w;
				t->addChild(w = createNavigationWidgetItem(QStringList() << s.description, f.fullFileName, s.line, SourceFileData::SymbolData::SUBPROGRAM));
				/* Note - it is important that the '--function' argument is placed in quotation marks, because
				 * gdb can report some function names as, e.g., 'foo(int, int)', and the spaces in such names
				 * confuse gdb. */
				w->setData(0, SourceFileData::DISASSEMBLY_TARGET_COORDINATES, QString(" -f \"%1\" -l %2 -n -1 -- 5")
					   .arg(escapeString(f.fullFileName)).arg(s.line));
				w->setData(0, SourceFileData::BREAKPOINT_TARGET_COORDINATES, QString(" --source \"%1\" --function \"%2\"")
					   .arg(escapeString(f.fullFileName)).arg(s.name));
			}
		}
	ui->treeWidgetSourceFiles->sortByColumn(0, Qt::AscendingOrder);
}

void MainWindow::updateSymbolViews()
{
	ui->treeWidgetSubprograms->clear();
	ui->treeWidgetStaticDataObjects->clear();
	ui->treeWidgetDataTypes->clear();
	for (const auto & f : sourceFiles.operator *())
	{
		for (const auto & s : f.subprograms)
		{
			QTreeWidgetItem * w;
			ui->treeWidgetSubprograms->addTopLevelItem(w = createNavigationWidgetItem(
				   QStringList() << s.name << f.fileName << QString("%1").arg(s.line) << s.description,
				   f.fullFileName,
				   s.line,
				   SourceFileData::SymbolData::SUBPROGRAM));
			/* Note - it is important that the '--function' argument is placed in quotation marks, because
			 * gdb can report some function names as, e.g., 'foo(int, int)', and the spaces in such names
			 * confuse gdb. */
			w->setData(0, SourceFileData::DISASSEMBLY_TARGET_COORDINATES, QString(" -f \"%1\" -l %2 -n -1 -- 5")
				   .arg(escapeString(f.fullFileName)).arg(s.line));
			w->setData(0, SourceFileData::BREAKPOINT_TARGET_COORDINATES, QString(" --source \"%1\" --function \"%2\"")
				   .arg(escapeString(f.fullFileName)).arg(s.name));
		}

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
		sendDataToGdbProcess(s + '\n', false);

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

	const QSet<int /* line number */> & enabledLines = breakpointCache.enabledBreakpointLinesForFile(displayedSourceCodeFile);
	const QSet<int /* line number */> & disabledLines = breakpointCache.disabledBreakpointLinesForFile(displayedSourceCodeFile);

	QTextEdit::ExtraSelection selection;

	std::function<void(int /* lineNumber*/)> rewindCursor = [&] (int line) -> void
	{
		c.movePosition(QTextCursor::Start);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, line - 1);
		c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
		selection.cursor = c;
	};
	for (const auto & line : enabledLines)
	{
		rewindCursor(line);
		selection.format = highlightFormats.enabledBreakpoint;
		sourceCodeViewHighlights.enabledBreakpointedLines << selection;
	}
	for (const auto & line : disabledLines)
	{
		rewindCursor(line);
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

QTreeWidgetItem * MainWindow::createNavigationWidgetItem(const QStringList &columnTexts, const QString & fullFileName, int lineNumber, SourceFileData::SymbolData::SymbolKind itemKind, bool disableNavigation, bool disableContextMenu)
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
	widgetFlashHighlighterData.highlightedWidget->setStyleSheet(widgetFlashHighlighterData.flashStyleSheets[(++ widgetFlashHighlighterData.flashCount) & 1]);

	if (widgetFlashHighlighterData.flashCount == widgetFlashHighlighterData.flashRepeatCount
			/* Guard against highlights that are taking too much time. */
			|| widgetFlashHighlighterData.profilingTimer.elapsed() > 2 * widgetFlashHighlighterData.flashIntervalMs)
	{
		widgetFlashHighlighterData.timer.stop();
		widgetFlashHighlighterData.highlightedWidget->setStyleSheet(widgetFlashHighlighterData.defaultStyleSheet);
	}
	else
	{
		widgetFlashHighlighterData.timer.start();
		widgetFlashHighlighterData.profilingTimer.start();
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
		QString actionText = a->text();
		if (!a->shortcut().toString().isEmpty())
			actionText += "\t(" + a->shortcut().toString() + ")";
		QAction * act = menu.addAction(actionText);
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
	widgetFlashHighlighterData.timer.start();
	widgetFlashHighlighterData.profilingTimer.start();
	widgetFlashHighlighterData.highlightedWidget->setStyleSheet(widgetFlashHighlighterData.flashStyleSheets[0]);
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

	/* Special case for internal files (e.g., the internal help file) - do not attempt to apply syntax highlighting. */
	if (sourceCodeLocation.fullFileName.startsWith(":/"))
	{
		ui->plainTextEditSourceView->setStyleSheet(HELPVIEW_PLAINTEXTEDIT_STYLESHEET);
		QFile f(sourceCodeLocation.fullFileName);
		//ui->plainTextEditSourceView->setStyleSheet("");
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
		ui->plainTextEditSourceView->setStyleSheet(DEFAULT_PLAINTEXTEDIT_STYLESHEET);
		auto sourceData = sourceFilesCache.getSourceFileCacheData(sourceCodeLocation.fullFileName, errorMessage);
		if (!sourceData)
		{
			ui->plainTextEditSourceView->setPlainText(errorMessage);
			goto out;
		}

		QTextDocument * d = (sourceData->textDocument.operator ->()->clone());
		QString savedStyleSheet = ui->plainTextEditSourceView->styleSheet();
		d->setDocumentLayout(new QPlainTextDocumentLayout(d));
		ui->plainTextEditSourceView->setDocument(d);
		ui->plainTextEditSourceView->setStyleSheet(savedStyleSheet);

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
	unsigned address = item->data(0, SVD_REGISTER_ADDRESS).toUInt();
	dialog->setWindowTitle(QString("%1 @ 0x%2").arg(svdRegister->name).arg(address, 8, 16, QChar('0')));
	QGroupBox * fieldsGroupBox = new QGroupBox();
	SvdRegisterViewData view(dialog, address);
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

	if (sourceFileNames.size() + subprograms.size() + dataObjects.size() + dataTypes.size() == 0)
	{
		ui->treeWidgetObjectLocator->addTopLevelItem(headerItem = new QTreeWidgetItem(QStringList() << "--- No items found ---"));
		headerItem->setBackground(0, Qt::lightGray);
		return;
	}

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
}

void MainWindow::on_pushButtonDeleteAllBookmarks_clicked()
{
	bookmarks.clear();
	updateBookmarksView();
	refreshSourceCodeView();
	disassemblyCache.highlightLines(ui->plainTextEditDisassembly, breakpoints, lastKnownProgramCounter);
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
		addDockWidget(Qt::TopDockWidgetArea, ui->dockWidgetObjectLocator, Qt::Horizontal);
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
	case 4:
		for (const auto & d : dockWidgets)
			d->show();
		break;
	}
	ui->comboBoxSelectLayout->setCurrentIndex(0);
}

void MainWindow::loadSVDFile(void)
{
	if (!QFileInfo(targetSVDFileName).exists())
	{
		QMessageBox::critical(0, "Target SVD file not found", "No valid SVD file specified.\nYou can specify the target SVD file in the settings.");
		uiSettings.lineEditTargetSVDFileName->setFocus();
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
	displaySourceCodeFile(SourceCodeLocation(":/resources/init.txt"));
	QString plainText = ui->plainTextEditSourceView->toPlainText();
	ui->plainTextEditSourceView->clear();
	ui->plainTextEditSourceView->setPlainText(plainText);
}

void MainWindow::on_pushButtonSendScratchpadToGdb_clicked()
{
	sendDataToGdbProcess(ui->plainTextEditScratchpad->toPlainText());
}

void MainWindow::scanForTargets(void)
{
	unsigned t = gdbTokenContext.insertContext(GdbTokenContext::GdbResponseContext(GdbTokenContext::GdbResponseContext::GDB_RESPONSE_TARGET_SCAN_COMPLETE));
	targetDataCapture.startCapture();
	sendDataToGdbProcess(QString("%1monitor swdp_scan\n").arg(t));
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
		if (!segment->get_file_size())
			continue;
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
