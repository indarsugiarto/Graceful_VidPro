#include "frameIO.h"

void resizeSDRAMbuf()
{
	// release if already exist
	if(frameIO_SDRAM_img_buf != NULL) {
		sark_xfree(sv->sdram_heap, frameIO_SDRAM_img_buf1, ALLOC_LOCK);
		sark_xfree(sv->sdram_heap, frameIO_SDRAM_img_buf2, ALLOC_LOCK);
	}
	// create and check if available
	uint sz = hImg * wImg;
	uint allocTag = FRAMEIO_SDRAM_ALLOC_TAG | 1;
	frameIO_SDRAM_img_buf1 = sark_xalloc(sv->sdram_heap, sz,
										allocTag, ALLOC_LOCK);
	allocTag = FRAMEIO_SDRAM_ALLOC_TAG | 2;
	frameIO_SDRAM_img_buf2 = sark_xalloc(sv->sdram_heap, sz,
										 allocTag, ALLOC_LOCK);
	if(frameIO_SDRAM_img_buf1==NULL || frameIO_SDRAM_img_buf2==NULL) {
		io_printf(IO_STD, "[FRAMEIO] SDRAM img buffer error!\n");
		rt_error(RTE_ABORT);
	}
}

void hSDP(uint mBox, uint port)
{
	sdp_msg_t *msg = (sdp_msg_t *)mBox;

#if (DEBUG_LEVEL>1)
	io_printf(IO_STD, "got sdp on port-%d, tag = 0x%x, srce_port = 0x%x, srce_addr = 0x%x, "
					  "dest_port = 0x%x\n",
			  port, msg->tag, msg->srce_port, msg->srce_addr, msg->dest_port);
#endif

	if(port==SDP_PORT_CONFIG) {
		// will send: maxBlock, nodeID, op-type
		configure_network(mBox);
	}

	else if(port==SDP_PORT_FRAME_INFO) {
		if(msg->cmd_rc==SDP_CMD_FRAME_INFO_SIZE) {
			// first, resize SDRAM image buffer
			hImg = msg->arg1 & 0xFFFF;
			wImg = msg->arg1 >> 16;
			resizeSDRAMbuf();

			// second, tell pxFwdr where to store/fetch into it
			uint key = MCPL_FRAMEIO_SDRAM_BUF_ADDR | SDRAM_BUFF_1_ID;
			spin1_send_mc_packet(key, (uint)frameIO_SDRAM_img_buf1, WITH_PAYLOAD);
			key = MCPL_FRAMEIO_SDRAM_BUF_ADDR | SDRAM_BUFF_2_ID;
			spin1_send_mc_packet(key, (uint)frameIO_SDRAM_img_buf2, WITH_PAYLOAD);

			// third, send wImg (in arg1.high) and hImg (in arg1.low)
			// to core pxFwdr, the streamer, and SpiNNVid-nodes about it:
			spin1_send_mc_packet(MCPL_FRAMEIO_SZFRAME,
								 msg->arg1, WITH_PAYLOAD);

		}
		// NOTE: EOF can be detected if pxBuffer.pxSeq == 0xFFFF, or, alternatively
		// by sending empty message via SDP_PORT_FRAME_END
		if(msg->cmd_rc==SDP_CMD_FRAME_INFO_EOF){
			// TODO:........
		}
	}
	else if(port==SDP_PORT_MISC) {
		else if(msg->cmd_rc == SDP_CMD_GIVE_REPORT) {
			// will use MCPL_BCAST_ALL_REPORT to all cores in the system
			// and the requesting info will be provided in the seq
			spin1_send_mc_packet(MCPL_BCAST_ALL_REPORT, msg->seq, WITH_PAYLOAD);
		}
		else if(msg->cmd_rc == SDP_CMD_RESET_NETWORK) {
			// then broadcast reset network to make maxBlock 0 and nodeBlockID 0xFF
			spin1_send_mc_packet(MCPL_BCAST_RESET_NET, 0, WITH_PAYLOAD);
		}
	}

	/*--------------------------------------------------------------------------------------*/
	/*--------------------------------------------------------------------------------------*/
	/*-------------- New revision: several cores may receives frames directly --------------*/

	else if(port==SDP_PORT_R_IMG_DATA) {
		//pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(pxBuffer.rpxbuf, &msg->cmd_rc, pxBuffer.pxLen);

	}
	else if(port==SDP_PORT_G_IMG_DATA) {
		//pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(pxBuffer.gpxbuf, &msg->cmd_rc, pxBuffer.pxLen);
	}
	else if(port==SDP_PORT_B_IMG_DATA) {
		pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(pxBuffer.bpxbuf, &msg->cmd_rc, pxBuffer.pxLen);

		// process gray scalling and forward afterwards
		spin1_schedule_callback(processGrayScaling, 0, 0, PRIORITY_PROCESSING);
	}
	// NOTE: EOF can be detected if pxBuffer.pxSeq == 0xFFFF, or, alternatively
	// by sending empty message via SDP_PORT_FRAME_END


	else if(port==SDP_PORT_HISTO) {
		if(msg->cmd_rc==SDP_CMD_REPORT_HIST) {
			uchar *ptr;
			if(msg->seq == histPropTree.c[0]) {
				ptr = (uchar *)child1hist;
			}
			else {
				ptr = (uchar *)child2hist;
			}

			// then copy the sdp data
			ptr += msg->arg1*256;
			sark_mem_cpy(ptr, msg->data, 256);

			// then check if all children have reported their histogram
			histPropTree.SDPItemCntr++;
			spin1_schedule_callback(leaderUpdateHist, 0, 0, PRIORITY_PROCESSING);
		}
	}

	// Note: variable msg might be released somewhere else
	spin1_msg_free(msg);
}


/* frameIO will decode the network configuration and broadcast it to all
 * */
void configure_network(uint mBox)
{
	sdp_msg_t *msg = (sdp_msg_t *)mBox;
	uint payload, freq, x, y, id;

#if(USING_SPIN==3)
	if(msg->cmd_rc >3) {
		io_printf(IO_STD, "[CONFIG] Invalid network size!\n");
		return;
	}
#else

	io_printf(IO_STD, "[CONFIG] Got configuration...\n");

	// NOTE: encoding strategy:
	//       seq = (freq << 8) | (opType << 4) | (wFilter << 2) | wHistEq;
	//		 srce_port = sdp_del_factor (i.e., sdpDelayFactorSpin)

	/* TODO: should be handle by LEAD_CORE in SpiNNVid nodes:
	// for safety, reset the network:
	spin1_send_mc_packet(MCPL_BCAST_RESET_NET, 0, WITH_PAYLOAD);
	*/

	// tell own-profiler, pxFwdr, streamer, and extLink about the op:
	payload = (msg->srce_port << 16) | msg->seq;
	spin1_send_mc_packet(MCPL_FRAMEIO_OP_INFO, payload, WITH_PAYLOAD);

	/* The above MCPL_FRAMEIO_OP_INFO summarizes the following:
	// tell own-profiler about the governor strategy:
	freq = (msg->seq >> 8) | PROF_MSG_SET_FREQ;
	spin1_send_mc_packet(MCPL_TO_OWN_PROFILER, freq, WITH_PAYLOAD);

	// then tell pxFwdr and other nodes about this op-freq info
	spin1_send_mc_packet(MCPL_FRAMEIO_OP_INFO, msg->seq, WITH_PAYLOAD);

	// tell streamer about sdpDelayFactorSpin:
	sdpDelayFactorSpin = msg->srce_port * DEF_DEL_VAL;
	spin1_send_mc_packet(MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_DEL,
						 sdpDelayFactorSpin, WITH_PAYLOAD);
	*/

	// broadcasting for nodes configuration:
	// cmd_rc contains the pre-defined number of nodes
	// NOTE: we decide to use at least 3 nodes:
	// arg1, arg2, arg3 contain node-0, node-1, node-2

	// node-0 (arg1):
	x = msg->arg1 >> 8; y = msg->arg1 & 0xFF;
	payload = (msg->cmd_rc << 24) | (x << 8) | (y);
	spin1_send_mc_packet(MCPL_BCAST_NODES_INFO, payload, WITH_PAYLOAD);
	// node-1 (arg2):
	x = msg->arg2 >> 8; y = msg->arg2 & 0xFF;
	payload = (msg->cmd_rc << 24) | (1 << 16) | (x << 8) | (y);
	spin1_send_mc_packet(MCPL_BCAST_NODES_INFO, payload, WITH_PAYLOAD);
	// node-2 (arg3):
	x = msg->arg3 >> 8; y = msg->arg3 & 0xFF;
	payload = (msg->cmd_rc << 24) | (2 << 16) | (x << 8) | (y);
	spin1_send_mc_packet(MCPL_BCAST_NODES_INFO, payload, WITH_PAYLOAD);

	// infer from msg->length
	uchar restNodes = (msg->length - sizeof(sdp_hdr_t) - sizeof(cmd_hdr_t)) / 2;
	if(restNodes > 0) {
		for(uchar i=0; i<restNodes; i++) {
			id = i+3;
			x = msg->data[i*2];
			y = msg->data[i*2 + 1];
			payload = (msg->cmd_rc << 24) | (id << 16) | (x << 8) | y;
			spin1_send_mc_packet(MCPL_BCAST_NODES_INFO, payload, WITH_PAYLOAD);
		}
	}
}

