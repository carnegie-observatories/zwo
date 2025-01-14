/* ----------------------------------------------------------------
 *
 * asiusb.h
 *
 * ---------------------------------------------------------------- */

#include <pthread.h>

#ifdef MACOSX
#include <sys/types.h>
#endif

#include "ASICamera2.h"

/* ---------------------------------------------------------------- */

enum asi_errors_enum { E_asi_error = 1+ASI_ERROR_END,
                       E_asi_not_connected, 
                       E_asi_not_present,
                       E_asi_timeout,
                       E_asi_last };

#define ASI_NBUFS   3

typedef struct asi_frame_tag {
  double timeStamp;
  u_int seqNumber;
  u_short *data;
  int w,h;
  volatile int wlock,rlock;
} AsiFrame;

typedef struct asi_struct_tag {
  int    id;
  int    handle;
  int    initialized;
  volatile int err;
  char   model[32],serialNumber[32],name[32]; // todo 
  float  tempSensor,percent;
  double offset,gain;
  int    sensorW,sensorH;
  int    aoiW,aoiH,binH,binV,startX,startY;
  int    winX,winY,winW,winH,winMode;
  double expTime;  
  double fps;
  pthread_mutex_t ioLock,frameLock;
  u_int seqNumber;             /* last frame number read from ZWO/ASI */
  u_char *data_0;
  int    videoMode;
  AsiFrame frames[ASI_NBUFS];
  pthread_t tid;
  volatile int stop_flag;
} AsiStruct;

/* ---------------------------------------------------------------- */

AsiStruct* asi_create(int);
int        asi_open  (AsiStruct*);
int        asi_init  (AsiStruct*);
int        asi_close (AsiStruct*);
int        asi_free  (AsiStruct*);

int asi_window      (AsiStruct*,int,int,int,int);
int asi_aoi         (AsiStruct*,int,int,int);
int asi_binning     (AsiStruct*,int);
int asi_temperature (AsiStruct*);
int asi_tempcon     (AsiStruct*,float,int);
int asi_exptime     (AsiStruct*,double);
int asi_gain        (AsiStruct*,double);
int asi_offset      (AsiStruct*,double);
int asi_flip        (AsiStruct*,int,int);

int asi_cycle_start (AsiStruct*);
int asi_cycle_stop  (AsiStruct*);

AsiFrame* asi_frame4writing(AsiStruct*,u_int);
AsiFrame* asi_frame4reading(AsiStruct*,u_int);
void      asi_frame_release(AsiStruct*,AsiFrame*);

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

