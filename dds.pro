#-------------------------------------------------
#
# Project created by QtCreator 2020-03-09T16:23:04
#
#-------------------------------------------------

QT       += core gui network serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = qbmp
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

INCLUDEPATH += ./clex/ ./troll/
INCLUDEPATH += ./external-sources/ELFIO/

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

LIBS += -lsetupapi

CONFIG += c++11

SOURCES += \
	   bmpdetect.cxx \
	   clex/cscanner.cxx \
	   mainwindow.cxx \
	   main.cxx \
	   ./troll/gdbserver.cxx \
	   ./troll/target-corefile.cxx \
	   svdfileparser.cxx

HEADERS += \
	   bmpdetect.hxx \
	   clex/cscanner.hxx \
	   gdb-mi-parser.hxx \
	   mainwindow.hxx \
	   gdbmireceiver.hxx \
	   ./troll/gdbserver.hxx \
	   ./troll/target-corefile.hxx \
	   ./troll/target.hxx \
	   svdfileparser.hxx \
	   troll/gdb-remote.hxx

FORMS += \
        mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc
