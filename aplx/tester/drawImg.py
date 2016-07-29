#!/usr/bin/python

import struct
from PyQt4 import Qt, QtGui, QtCore, QtNetwork
import socket
import os
import time
import sys


#wImg = 2825	# 5296875 = 50D2EB
#hImg = 1875
wImg = 640
hImg = 480
FILE_SIZE = wImg*hImg


def loadImg(chip):
    if chip==0:
        suffix = ".00"
    elif chip==1:
        suffix = ".10"
    elif chip==2:
        suffix = ".01"
    else:
        suffix = ".11"

    frName = "red" + suffix
    fgName = "green" + suffix
    fbName = "blue" + suffix
    fyName = "gray" + suffix
    fzName = "res" + suffix

    with open(frName, "rb") as fr:
        r = fr.read(FILE_SIZE)
    with open(fgName, "rb") as fg:
        g = fg.read(FILE_SIZE)
    with open(fbName, "rb") as fb:
        b = fb.read(FILE_SIZE)
    with open(fyName, "rb") as fy:
        y = fy.read(FILE_SIZE)
    with open(fzName, "rz") as fz:
        z = fz.read(FILE_SIZE)
    rgbImg = Qt.QImage(wImg, hImg, Qt.QImage.Format_RGB888)
    gryImg = Qt.QImage(wImg, hImg, Qt.QImage.Format_RGB888)
    resImg = Qt.QImage(wImg, hImg, Qt.QImage.Format_RGB888)
    col = 0
    row = 0
    #clr = Qt.QColor
    #gry = Qt.QColor
    for i in range(FILE_SIZE):
        clr = Qt.QColor(ord(r[i]), ord(g[i]), ord(b[i]))
        gry = Qt.QColor(ord(y[i]), ord(y[i]), ord(y[i]))
        res = Qt.QColor(ord(z[i]), ord(z[i]), ord(z[i]))
        #clr.setRgb(ord(r[i]), ord(g[i]), ord(b[i]))
        #gry.setRgb(ord(y[i]), ord(y[i]), ord(y[i]))
        #print "0x%x" % clr.rgb()
        rgbImg.setPixel(col, row, clr.rgb())
        gryImg.setPixel(col, row, gry.rgb())
        resImg.setPixel(col, row, res.rgb())
        col += 1
        if col==wImg:
            col = 0
            row += 1
    return rgbImg, gryImg, resImg


def main():
    if len(sys.argv) < 2:
        print "Usage: drawImg.py <chip>"
        return
    else:
        chip = int(sys.argv[1])
    app = Qt.QApplication(sys.argv)
    #gui = myGUI()
    #gui.show()
    rgbViewer = QtGui.QGraphicsView()
    gryViewer = QtGui.QGraphicsView()
    resViewer = QtGui.QGraphicsView()
    rgbImg, gryImg, resImg = loadImg(chip)
    rgbPix = Qt.QPixmap().fromImage(rgbImg)
    gryPix = Qt.QPixmap().fromImage(gryImg)
    resPix = Qt.QPixmap().fromImage(resImg)
    
    """ The followings work:
    rgbPix.load("SpiNN-3.jpg")
    gryPix.load("SpiNN-3.jpg")
    """
    rgbScene = QtGui.QGraphicsScene()
    gryScene = QtGui.QGraphicsScene()
    resScene = QtGui.QGraphicsScene()
    rgbScene.addPixmap(rgbPix)
    gryScene.addPixmap(gryPix)
    resScene.addPixmap(resPix)
    rgbViewer.setScene(rgbScene)
    gryViewer.setScene(gryScene)
    resViewer.setScene(resScene)
    rgbViewer.show()
    gryViewer.show()
    resViewer.show()
    sys.exit(app.exec_())


if __name__=='__main__':
    main()
