#ifndef VIDDEF_H
#define VIDDEF_H

#include <QObject>

// FFmpeg is a pure C project, so to use the libraries within your C++ application you need
// to explicitly state that you are using a C library by using extern "C"."
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
#include <libavutil/avstring.h>
#include <libavutil/time.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
}

#include <cmath>

#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define VIDEO_PICTURE_QUEUE_SIZE 1

// YUV_Overlay is similar to SDL_Overlay
// A SDL_Overlay is similar to a SDL_Surface except it stores a YUV overlay.
// Possible format:
#define SDL_YV12_OVERLAY  0x32315659  /* Planar mode: Y + V + U */
#define SDL_IYUV_OVERLAY  0x56555949  /* Planar mode: Y + U + V */
#define SDL_YUY2_OVERLAY  0x32595559  /* Packed mode: Y0+U0+Y1+V0 */
#define SDL_UYVY_OVERLAY  0x59565955  /* Packed mode: U0+Y0+V0+Y1 */
#define SDL_YVYU_OVERLAY  0x55595659  /* Packed mode: Y0+V0+Y1+U0 */
typedef struct Overlay {
    quint32 format;
    int w, h;
    int planes;
    quint16 *pitches;
    quint8 **pixels;
    quint32 hw_overlay:1;
} YUV_Overlay;

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
} PacketQueue;


typedef struct VideoPicture {
  YUV_Overlay *bmp;
  int width, height; /* source height & width */
  int allocated;
  double pts;
} VideoPicture;

typedef struct VideoState {

  AVFormatContext *pFormatCtx;
  int             videoStream;

  double          frame_timer;
  double          frame_last_pts;
  double          frame_last_delay;
  double          video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
  AVStream        *video_st;
  PacketQueue     videoq;

  VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int             pictq_size, pictq_rindex, pictq_windex;
  int             quit;

  AVIOContext     *io_context;
  struct SwsContext *sws_ctx;
} VideoState;

#endif // VIDDEF_H

