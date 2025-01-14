/* ----------------------------------------------------------------
 *
 * zwotcp.h
 *
 * ---------------------------------------------------------------- */

#include <pthread.h>

#ifdef MACOSX
#include <sys/types.h>
#endif

#include "asiusb.h"

/* ---------------------------------------------------------------- */

enum zwo_errors_enum { E_zwo_error = 1+E_asi_last,
                       E_zwo_not_connected,
                       E_zwo_setpar,
                       E_zwo_last };

#define ZWO_NBUFS     3

typedef struct zwp_frame_tag {
  double timeStamp;
  u_int seqNumber;
  u_short *data;
  int w,h;
  volatile int wlock,rlock;
} ZwoFrame;

typedef struct zwo_struct_tag {
  char   host[128];
  int    port,handle; // TODO sock
  volatile int err;
  char   serverVersion[64];
  u_int  cookie;
  int    maxWidth,maxHeight,hasCooler,isColor,bitDepth;
  float  tempSensor,percent;
  int    aoiW,aoiH;
  double expTime;
  int gain,offset;
  double fps;
  volatile int rolling;
  int    filter;
  pthread_mutex_t ioLock,frameLock;
  u_int seqNumber;
  ZwoFrame frames[ZWO_NBUFS];
  pthread_t tid;
  volatile int stop_flag;
} ZwoStruct;

/* ---------------------------------------------------------------- */

ZwoStruct* zwo_create  (const char*,int);

int  zwo_connect    (ZwoStruct*);
int  zwo_open       (ZwoStruct*);
int  zwo_setup      (ZwoStruct*,int,int,int);
int  zwo_disconnect (ZwoStruct*);
int  zwo_close      (ZwoStruct*);
void zwo_free       (ZwoStruct*);

int  zwo_temperature (ZwoStruct*);
int  zwo_tempcon     (ZwoStruct*,int);
int  zwo_exptime     (ZwoStruct*,double);
int  zwo_offset      (ZwoStruct*,int);
int  zwo_gain        (ZwoStruct*,int);
int  zwo_flip        (ZwoStruct*,int,int);
int  zwo_filter      (ZwoStruct*,int);
int  zwo_rolling     (ZwoStruct*,int);

int  zwo_cycle_start (ZwoStruct*);
int  zwo_cycle_stop  (ZwoStruct*);

int  zwo_server(ZwoStruct*,const char*,char*);

ZwoFrame* zwo_frame4writing(ZwoStruct*,u_int);
ZwoFrame* zwo_frame4reading(ZwoStruct*,u_int);
void      zwo_frame_release(ZwoStruct*,ZwoFrame*);

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

