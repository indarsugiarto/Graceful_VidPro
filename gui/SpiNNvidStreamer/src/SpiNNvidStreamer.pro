#-------------------------------------------------
#
# Project created by QtCreator 2016-06-21T18:01:39
#
#-------------------------------------------------

QT       += core gui network opengl

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = SpiNNvidStreamer
TEMPLATE = app


SOURCES += main.cpp\
        vidstreamer.cpp \
    cdecoder.cpp \
    cscreen.cpp \
    cspinncomm.cpp \
    cImgViewer.cpp \
    cPixViewer.cpp

HEADERS  += vidstreamer.h \
    cdecoder.h \
    viddef.h \
    cscreen.h \
    cspinncomm.h \
    cImgViewer.h \
    cPixViewer.h

FORMS    += vidstreamer.ui

LIBS += -lavcodec \
        -lavformat \
        -lswresample \
        -lswscale \
        -lavutil

DISTFILES +=

#Jika dipakai di Fedora, lokasi ffmpeg ada di:
INCLUDEPATH += /usr/include/ffmpeg
#Jika dipakai di Ubuntu, yang   di atas tidak diperlukan atau install dulu:
#sudo apt install libavcodec-dev libavformat-dev libswscale-dev

#To make it consistent with aplx part
INCLUDEPATH += ../../../aplx/SpiNNVid/src
