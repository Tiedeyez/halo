QT += core gui qml quick quickcontrols2 multimedia dbus concurrent network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += taglib
LIBS += -pthread
TARGET = halo
TEMPLATE = app

HEADERS += \
    src/pointer.h \
    src/doctor.h \
    src/headphones.h \
    src/kaleidopilot.h \
    src/ring.h \
    src/dsp.h \
    src/stream.h \
    src/engine.h \
    src/analysis.h \
    src/library.h \
    src/player.h \
    src/visual.h \
    src/visualizers.h \
    src/remote.h \
    src/audiooutput.h \
    src/cli.h \
    src/theme.h \
    src/translator.h \
    src/lyrics.h \
    src/playlists.h

SOURCES += \
    src/doctor.cpp \
    src/headphones.cpp \
    src/kaleidopilot.cpp \
    src/main.cpp \
    src/stream.cpp \
    src/engine.cpp \
    src/analysis.cpp \
    src/library.cpp \
    src/player.cpp \
    src/visual.cpp \
    src/visualizers.cpp \
    src/remote.cpp \
    src/audiooutput.cpp \
    src/cli.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/lyrics.cpp \
    src/playlists.cpp

RESOURCES += src/resources.qrc
# The Turntable visual's layers (tools/turntable-layers writes them).
RESOURCES += src/turntable/turntable.qrc
# The Hologram visual's light layers (tools/hologram-layers writes them).
RESOURCES += src/hologram/hologram.qrc

# Shaders are compiled to Qt's portable .qsb form when qmake runs (bin/build
# always runs it) and packed into the binary through a generated resource file.
QSB = $$[QT_HOST_BINS]/qsb
!exists($$QSB): error("qsb is missing. Install it with: omarchy-pkg-add qt6-shadertools")
SHADERS = glitch.frag pixelfield.frag pixelbar.frag kaleido.frag pixelgrid.frag hologram.frag turntable.frag halolight.frag underglow.frag ledgrid.frag barsfx.frag spot.frag
SHADER_FILES =
# Commas would split qmake function arguments, so the list lives in a variable.
QSB_GLSL = "100 es,120,150"
for(shader, SHADERS) {
    system(mkdir -p $$shell_quote($$OUT_PWD/shaders))
    QSB_COMMAND = $$QSB --glsl $$shell_quote($$QSB_GLSL) --hlsl 50 --msl 12 -o $$shell_quote($$OUT_PWD/shaders/$${shader}.qsb) $$shell_quote($$PWD/src/shaders/$$shader)
    !system($$QSB_COMMAND): \
        error("Couldn't compile the shader $$shader")
    SHADER_FILES += "        <file>shaders/$${shader}.qsb</file>"
}
write_file($$OUT_PWD/shaders.qrc, $$list("<RCC>" "    <qresource prefix=\"/\">" $$SHADER_FILES "    </qresource>" "</RCC>"))
RESOURCES += $$OUT_PWD/shaders.qrc
DISTFILES += src/shaders/glitch.frag src/shaders/pixelfield.frag src/shaders/pixelbar.frag src/shaders/kaleido.frag src/shaders/pixelgrid.frag src/shaders/hologram.frag src/shaders/turntable.frag src/shaders/halolight.frag src/shaders/underglow.frag src/shaders/ledgrid.frag src/shaders/barsfx.frag src/shaders/spot.frag
