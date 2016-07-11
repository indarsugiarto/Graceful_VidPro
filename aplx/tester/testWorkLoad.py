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

if __name__=='__main__':

    #prepare the socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
      sock.bind( ('', DEF_PORT) )
    except OSError as msg:
      print "%s" % msg
      sock.close()
      sock = None

    if sock is not None:
      flags = 0x07
      tag = 0
      dp = SDP_PORT_FRAME_INFO
      dc = DEF_LEADAP
      dax = 0
      day = 0
      print "Send frame info: 640 x 480",
      cmd = 640
      seq = 480
      sendSDP(flags, tag, dp, dc, dax, day, cmd, seq, 0, 0, 0, None)
      print "done!"

      time.sleep(1)
      dp = SDP_PORT_CONFIG
      cmd = SDP_CMD_GIVE_REPORT
      seq = DEBUG_REPORT_WLOAD
      print "Requesting report...",
      sendSDP(flags, tag, dp, dc, dax, day, cmd, seq, 0, 0, 0, None)
      print "done! Waiting report (ctrl-C to stop)..."

      cntr = 0      # how many cores have sent report
      total = 48*17   # initial guess
      fmt = "<HQ2H3I" # pad, sdp_hdr, cmd_rc, seq, arg1, arg2, arg3
      print "Block-ID, SubBlock-ID, startLine, endLine"
      while True:
          try:
             data = sock.recv(1024)
             # put to the table
             pad, sdp_hdr, cmd_rc, seq, arg1, arg2, arg3 = struct.unpack(fmt, data)
             mxBlk = cmd_rc >> 8
             blkID = cmd_rc & 0xFF
             nW    = seq >> 8
             wID        = seq & 0xFF
             sLine = arg1 >> 16
             eLine = arg1 & 0xFFFF
             # just print it...
             row = [blkID, wID, sLine, eLine]
             wLoad[cntr] = row
             cntr += 1
             print "\t{}\t{}\t{}\t{}".format(blkID, wID, sLine, eLine)
          except KeyboardInterrupt:
             break
      sock.close()

