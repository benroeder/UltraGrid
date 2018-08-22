######################################################################
# Automatically generated by qmake (3.0) Wed Jun 21 13:05:24 2017
######################################################################

TEMPLATE = app
TARGET = uv-qt
INCLUDEPATH += .
INCLUDEPATH += ../../tools/
INCLUDEPATH += ../../src

DEFINES += GUI_BUILD

QT += widgets

CONFIG += c++11

LIBS += ../../tools/astat.a
macx {
	LIBS += -framework CoreFoundation
} win32 {
	LIBS += -lWs2_32
}

astat.target = astat_lib
astat.commands = cd ../../tools && make -f Makefile.astat lib

QMAKE_EXTRA_TARGETS += astat
PRE_TARGETDEPS += astat_lib


# Input
HEADERS += ultragrid_window.hpp \
ultragrid_option.hpp \
v4l2.hpp \
previewWidget.hpp \
log_window.hpp \
../../tools/astat.h \
../../src/shared_mem_frame.hpp \
vuMeterWidget.hpp \
settings_window.hpp \

FORMS += ultragrid_window.ui \
log_window.ui \
settings.ui

SOURCES += ultragrid_window.cpp \
	ultragrid_option.cpp \
	v4l2.cpp \
	previewWidget.cpp \
	log_window.cpp \
	vuMeterWidget.cpp \
	settings_window.cpp \
	../../src/shared_mem_frame.cpp \
	main.cpp
