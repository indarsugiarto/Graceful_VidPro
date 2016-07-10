#include "cdecoder.h"
#include <QDebug>
#include <QCoreApplication>

cDecoder::cDecoder(QObject *parent) : QObject(parent), go(true)
{
    cntr = 0;
	testCntr = 0;
    isStopped = true;
}

void cDecoder::started()
{
	int             videoStream;
    int             numBytes;
    int             frameFinished;
    AVPacket        packet;

    // initialize stuffs
	is = (VideoState *)av_mallocz(sizeof(VideoState));
	if(is==NULL) {
		emit error("Cannot allocate is");
		return;
	}
	// Register all formats and codecs
	av_register_all();

	pCodec = NULL;
    pCodecCtx = NULL;
    optionsDict = NULL;
    pFrame = NULL;
    pFrameRGB = NULL;
    buffer = NULL;

    // Open video file
	if(avformat_open_input(&is->pFormatCtx, filename.toLocal8Bit().data(),
						   NULL, NULL)!=0) {
		emit error(QString("Cannot open %1").arg(filename));
        return;
    }
    // Retrieve stream information
    if(avformat_find_stream_info(is->pFormatCtx, NULL)<0) {
        error(QString("Cannot find stream information"));
        return;
    }
    // Indar: Optional, dump information about file onto standard error
    // av_dump_format(pFormatCtx, 0, argv[1], 0);

    // Find the first video stream
    videoStream=-1;
	for(unsigned i=0; i<is->pFormatCtx->nb_streams; i++)
      if(is->pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
        videoStream=i;
        break;
      }
    if(videoStream==-1) {
        error(QString("Cannot find a video stream"));
        return;
    }

    // Get a pointer to the codec context for the video stream
    pCodecCtx = is->pFormatCtx->streams[videoStream]->codec;
	w = pCodecCtx->width;
	h = pCodecCtx->height;
	emit gotPicSz(w,h);

    // Find the decoder for the video stream
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL) {
        emit error("Unsupported codec!");
      return; // Codec not found
    }

    // Open codec
    if(avcodec_open2(pCodecCtx, pCodec, &optionsDict)<0) {
        emit error("Could not open codec!");
        return;
    }

    // Allocate video frame
    pFrame = av_frame_alloc();

    // Allocate an AVFrame structure
    pFrameRGB = av_frame_alloc();
    if(pFrameRGB==NULL) {
        emit error("Could not allocate AVFrame structure!");
        return;
    }

    // Determine required buffer size and allocate buffer
	numBytes = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
    buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

    is->sws_ctx =
      sws_getContext
      (
		  pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width,
		  pCodecCtx->height, PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL
      );

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
    // of AVPicture
    avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24,
           pCodecCtx->width, pCodecCtx->height);

    isStopped = false;

	while(!isStopped) {
		//cntr++;
		//if(cntr==9999999)
		if(go)
		{
			//qDebug() << QString("Decode...");
			// Read frames
			if(av_read_frame(is->pFormatCtx, &packet)>=0) {
				// Is this a packet from the video stream?
				if(packet.stream_index==videoStream) {
					// Decode video frame
					avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

					// Did we get a video frame?
					if(frameFinished) {
						// Convert the image from its native format to RGB
						/*
						sws_scale(is->sws_ctx,
								  (uint8_t const * const *)pFrame->data,
								  pFrame->linesize, 0, pCodecCtx->height,
								  pFrameRGB->data, pFrameRGB->linesize);
						*/
						sws_scale(is->sws_ctx, pFrame->data, pFrame->linesize, 0, h,
								  pFrameRGB->data, pFrameRGB->linesize);
						/*
						sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0,
								  pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
						*/
						// Publish the result
						frame = getFrame();
						emit newFrame(frame);
						go = false;

						// wait until refresh timer fire-up
					}
				}
			}
			else {
				isStopped = true;
			}		
		}
		else {
			cntr++;
//			if(cntr==99999999)
//				qDebug() << "tick...";
			QCoreApplication::processEvents();
		}
	}

    emit finished();
	// Free the RGB image
	av_free(buffer);
	av_free(pFrameRGB);

	// Free the YUV frame
	av_free(pFrame);

	// Close the codec
	avcodec_close(pCodecCtx);
	// Close the video file
	avformat_close_input(&is->pFormatCtx);
	av_free(is);
}

cDecoder::~cDecoder()
{

}

QImage cDecoder::getFrame() {
	QImage frame = QImage(w,h,QImage::Format_RGB888);
	for(int y=0;y<h;y++)
		memcpy(frame.scanLine(y),pFrameRGB->data[0]+y*pFrameRGB->linesize[0],w*3);
	return frame;
}

void cDecoder::refresh()
{
	go = true;
	//qDebug() << "go...";
}
