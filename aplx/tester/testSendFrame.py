#!/usr/bin/python

import struct
from PyQt4 import Qt, QtGui, QtCore, QtNetwork
import socket
import os
import time

SPINN3_HOST             = '192.168.240.253'
SPINN5_HOST             = '192.168.240.1'

DEF_SEND_PORT             = 17893 #tidak bisa diganti dengan yang lain
# in the aplx, it is defined:
SDP_TAG_REPLY            = 1
SDP_UDP_REPLY_PORT      = 20001
SDP_HOST_IP            = 0x02F0A8C0      # 192.168.240.2, dibalik!
SDP_TAG_RESULT            = 2
SDP_UDP_RESULT_PORT      = 20002
SDP_TAG_DEBUG           = 3
SDP_UDP_DEBUG_PORT      = 20003

SDP_PORT_R_IMG_DATA      = 1      # port for sending R-channel
SDP_PORT_G_IMG_DATA      = 2      # port for sending G-channel
SDP_PORT_B_IMG_DATA      = 3      # port for sending B-channel
SDP_PORT_FRAME_INFO     = 5      # will be used to send the basic info about frame
SDP_PORT_ACK            = 6
SDP_PORT_CONFIG            = 7      # for sending image/frame info

DEF_HOST            = SPINN3_HOST
DEF_LEADAP            = 1

SDP_CMD_CONFIG_NETWORK       = 1   # for setting up the network
SDP_CMD_GIVE_REPORT      = 2

DEBUG_REPORT_BLKINFO      = 3
DEBUG_REPORT_MYWID      = 4
DEBUG_REPORT_WLOAD      = 5

Num_of_Blocks            = 3
#Let's try this:
#Node-0: <0,0>
#Node-1: <1,0>
#Node-2: <1,1>

DEF_PORT            = 20003


# Untuk eksperiment, coba ganti parameter berikut:
#DELAY_VALUE = 0.000001		# in second
DELAY_VALUE = 3			# in second
#pxLen = 272
pxLen = 4


# The maximum current network is 48*17 = 816 cores
# each row contains 4 column: block-ID, subblock-ID, startLine, endLine
wLoad = [[0 for _ in range(4)] for _ in range(48*17)]

def sendSDP(flags, tag, dp, dc, dax, day, cmd, seq, arg1, arg2, arg3, bArray):
    da = (dax << 8) + day
    dpc = (dp << 5) + dc
    sa = 0
    spc = 255
    pad = struct.pack('<2B',0,0)
    hdr = struct.pack('<4B2H',flags,tag,dpc,spc,da,sa)
    scp = struct.pack('<2H3I',cmd,seq,arg1,arg2,arg3)
    if bArray is not None:
        sdp = pad + hdr + scp + bArray
    else:
        sdp = pad + hdr + scp

    CmdSock = QtNetwork.QUdpSocket()
    CmdSock.writeDatagram(sdp, QtNetwork.QHostAddress(DEF_HOST), DEF_SEND_PORT)
    CmdSock.flush()
    return sdp

def sendChunk(ch_port, ba):
    da = 0
    dpc = (ch_port << 5) + 1
    sa = 0
    spc = 255
    pad = struct.pack('<2B',0,0)
    hdr = struct.pack('<4B2H',7,0,dpc,spc,da,sa)

    sdp = pad + hdr + ba

    CmdSock = QtNetwork.QUdpSocket()
    CmdSock.writeDatagram(sdp, QtNetwork.QHostAddress(DEF_HOST), DEF_SEND_PORT)
    CmdSock.flush()
    

def getImage(fName):
    rCh = list()
    gCh = list()
    bCh = list()
    img = QtGui.QImage()
    img.load(fName)
    wImg = img.width()
    hImg = img.height()
    for y in range(hImg):
        r = [0 for _ in range(wImg)]
        g = [0 for _ in range(wImg)]
        b = [0 for _ in range(wImg)]
        for x in range(wImg):
            clr = Qt.QColor(img.pixel(x, y))
            r[x] = clr.red()
            g[x] = clr.green()
            b[x] = clr.blue()
        rCh.append(r)
        gCh.append(g)
        bCh.append(b)
    return rCh, gCh, bCh

imgFile = "SpiNN-3.jpg"

def main():
    #prepare the socket
    # prepare the socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(('', DEF_PORT))
    except OSError as msg:
        print "%s" % msg
        sock.close()
        sock = None

    if sock is not None:
      print "Send frame info: 640 x 480",
      dp = SDP_PORT_FRAME_INFO
      dc = DEF_LEADAP
      cmd = 640
      seq = 480
      sendSDP(7, 0, dp, dc, 0, 0, cmd, seq, 0, 0, 0, None)
      print "done!"

      time.sleep(1)

      print "Send chunks...",
      rba = bytearray()
      gba = bytearray()
      bba = bytearray()
      for i in range(pxLen):
            if (i % 2) == 0:
                  rba += struct.pack('B', 0x1A)
                  gba += struct.pack('B', 0x2A)
                  bba += struct.pack('B', 0x3A)
            else:
                  rba += struct.pack('B', 0x4B)
                  gba += struct.pack('B', 0x5B)
                  bba += struct.pack('B', 0x6B)
      sendChunk(SDP_PORT_R_IMG_DATA, rba)
      time.sleep(DELAY_VALUE)
      sendChunk(SDP_PORT_G_IMG_DATA, gba)
      time.sleep(DELAY_VALUE)
      sendChunk(SDP_PORT_B_IMG_DATA, bba)
      print "done!"

if __name__=='__main__':
      main()


