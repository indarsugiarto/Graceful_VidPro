TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.c \
    init.c \
    eHandler.c \
    util.c \
    process.c

INCLUDEPATH += /opt/spinnaker_tools_134/include

DISTFILES += \
    Makefile \
    ../../APLX.README \
    ../../tester/testBlockInfo.py \
    ../../tester/testWorkLoad.py \
    ../../tester/testMyWID.py \
    ../../tester/testDecompressing.py

HEADERS += \
    SpiNNVid.h
