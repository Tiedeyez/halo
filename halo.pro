QT += core gui qml quick quickcontrols2 multimedia dbus concurrent

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += taglib
LIBS += -pthread
TARGET = halo
TEMPLATE = app

HEADERS += \
    src/ring.h \
    src/dsp.h \
    src/stream.h \
    src/engine.h \
    src/analysis.h \
    src/library.h \
    src/player.h \
    src/visual.h \
    src/remote.h \
    src/audiooutput.h \
    src/cli.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/stream.cpp \
    src/engine.cpp \
    src/analysis.cpp \
    src/library.cpp \
    src/player.cpp \
    src/visual.cpp \
    src/remote.cpp \
    src/audiooutput.cpp \
    src/cli.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
