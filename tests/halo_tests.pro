QT += core gui qml quick quickcontrols2 multimedia dbus concurrent network testlib
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += taglib
LIBS += -pthread
TARGET = halo_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/ring.h ../src/dsp.h ../src/stream.h ../src/engine.h ../src/analysis.h ../src/library.h \
           ../src/player.h ../src/visual.h ../src/visualizers.h ../src/remote.h ../src/theme.h ../src/translator.h ../src/lyrics.h ../src/kaleidopilot.h ../src/cymatics.h ../src/headphones.h ../src/doctor.h ../src/playlists.h
SOURCES += halo_tests.cpp ../src/stream.cpp ../src/engine.cpp ../src/analysis.cpp ../src/library.cpp \
           ../src/player.cpp ../src/visual.cpp ../src/visualizers.cpp ../src/remote.cpp ../src/theme.cpp ../src/translator.cpp ../src/lyrics.cpp ../src/kaleidopilot.cpp ../src/cymatics.cpp ../src/headphones.cpp ../src/doctor.cpp ../src/playlists.cpp
