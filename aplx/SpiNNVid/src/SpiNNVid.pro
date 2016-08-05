TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.c \
    init.c \
    eHandler.c \
    util.c \
    process.c \
    profiler.c

INCLUDEPATH += /opt/spinnaker_tools_134/include

DISTFILES += \
    Makefile \
    ../../APLX.README \
    ../../tester/testBlockInfo.py \
    ../../tester/testWorkLoad.py \
    ../../tester/testMyWID.py \
    ../../tester/testDecompressing.py \
    TODO \
    ../../tester/testStreamFrame.py \
    Makefile.3 \
    Makefile.5 \
    museum

HEADERS += \
    SpiNNVid.h \
    defSpiNNVid.h \
    profiler.h
