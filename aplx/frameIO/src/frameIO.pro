TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.c \
    init.c \
    util.c \
    process.c \
    streamer.c \
    sendResult.c \
    hSDP.c \
    eMCP.c \
    eTDMA.c

INCLUDEPATH += /opt/spinnaker_tools_3.0.1/include

DISTFILES += \
    Makefile

HEADERS += \
    frameIO.h \
    ../../SpiNNVid/src/defSpiNNVid.h

