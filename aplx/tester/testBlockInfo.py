#!/usr/bin/python

import struct
from PyQt4 import Qt, QtGui, QtCore, QtNetwork

SPINN3_HOST 		= '192.168.240.253'
SPINN5_HOST 		= '192.168.240.1'

DEF_SEND_PORT 		= 17893 #tidak bisa diganti dengan yang lain
# in the aplx, it is defined:
SDP_TAG_REPLY		= 1
SDP_UDP_REPLY_PORT	= 20001
SDP_HOST_IP		= 0x02F0A8C0	# 192.168.240.2, dibalik!
SDP_TAG_RESULT		= 2
SDP_UDP_RESULT_PORT	= 20002
SDP_TAG_DEBUG           = 3
SDP_UDP_DEBUG_PORT      = 20003

DEF_HOST		= SPINN3_HOST

SDP_CMD_CONFIG_NETWORK 	= 1   # for setting up the network

Num_of_Blocks		= 3
#Let's try this:
#Node-0: <0,0>
#Node-1: <1,0>
#Node-2: <1,1>

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
    flags = 0x07
