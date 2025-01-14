/* ---------------------------------------------------------------- */
//
// ZWO/ASI library (USB)
// 2019-01-07  Christoph C. Birk (birk@carnegiescience.edu)
//
/* ---------------------------------------------------------------- */

#define DEBUG           1
#define TIME_TEST       0

/* ---------------------------------------------------------------- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <assert.h>

#include "zwo.h"
#include "asiusb.h"
#include "ptlib.h"
#include "utils.h"
#include "random.h"

/* ---------------------------------------------------------------- */

#ifndef PREFUN
#define PREFUN          __func__
#endif

/* ---------------------------------------------------------------- */
/* asi_() */
/* ---------------------------------------------------------------- */
 
AsiStruct* asi_create(int id)
{
  int i;

  AsiStruct *self = (AsiStruct*)malloc(sizeof(AsiStruct));
  if (!self) return NULL;

  self->id = id;
  self->handle = -1;                   /* connection closed */
  self->initialized = 0;
  self->err = 0;
  self->model[0] = '\0';
  self->serialNumber[0] = '\0';
  self->name[0] = '\0';
  self->tempSensor = 0.0f;
  self->percent = 0;
  self->gain = self->offset = 0;
  self->sensorW = self->sensorH = 0;
  self->aoiW = self->aoiH = 0;
  self->startX = self->startY = 0;
  self->winMode = 0;
  self->winX = self->winY = self->winW = self->winH = 0;
  self->binH = self->binV = 1;
  self->expTime = 0.5;
  self->fps = 0.0;
  self->videoMode = 1;

  pthread_mutex_init(&self->ioLock,NULL);
  pthread_mutex_init(&self->frameLock,NULL);

  self->seqNumber = 0;
  self->data_0 = NULL;

  for (i=0; i<ASI_NBUFS; i++) {
    AsiFrame *frame = &self->frames[i];
    frame->data = NULL;
  }

  self->tid = 0;
  self->stop_flag = 1;

  return self;
}

/* ---------------------------------------------------------------- */

int asi_open(AsiStruct* self)
{
  int     err=0;
  int     handle=0;
  char    line[256];
  ASI_CAMERA_INFO info;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%s(%d)\n",__FILE__,PREFUN,self->id);
#endif
  assert(self);
  assert(self->handle < 0);

  int n = ASIGetNumOfConnectedCameras();
  if (n == 0) err = E_asi_not_present;

  if (!err) ASIGetCameraProperty(&info,self->id);

  if (!err) {
    int ret = ASIOpenCamera(self->id);
    if (ret != ASI_SUCCESS) err = E_asi_not_connected;
#if (DEBUG > 0)
    fprintf(stderr,"%s(%d): handle=%d\n",PREFUN,self->id,handle);
#endif
  }
  if (!err) ASIInitCamera(handle);

  self->model[0] = '\0';
  self->name[0] = '\0';
  if (!err) {
    sprintf(line,"model= %s",info.Name);
    message(self,line,MSG_FILE);
    strcpy(self->model,info.Name);
    strcpy(self->name,info.Name);
  }

  self->serialNumber[0] = '\0';
  if (!err) {
    strcpy(self->serialNumber,"1"); // todo
  }

  if (!err) {
    if (!err) self->sensorW = (int)info.MaxWidth;
    if (!err) self->sensorH = (int)info.MaxHeight;
#if (DEBUG > 0)
    fprintf(stderr,"sensor= %d,%d\n",self->sensorW,self->sensorH);
#endif
    err = ASISetROIFormat(handle,self->sensorW,self->sensorH,1,ASI_IMG_RAW16);
    assert(err == ASI_SUCCESS);
  }

#if (DEBUG > 0) // todo 
  long v; ASI_BOOL bAuto; int ret;
  ret = ASIGetControlValue(handle,ASI_GAIN,&v,&bAuto);
  printf("ret=%d, gain=%ld\n",ret,v); // todo self->gain
  ret = ASIGetControlValue(handle,ASI_OFFSET,&v,&bAuto);
  printf("ret=%d, offs=%ld\n",ret,v); // todo self-offset
#endif

  if (!err) self->handle = handle;

  return err;
}

/* ---------------------------------------------------------------- */

int asi_init(AsiStruct* self)
{ 
  int err=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%s(%d)\n",__FILE__,PREFUN,self->id);
#endif
  assert(self);
  assert(self->handle >= 0);

  if (self->initialized) return 0;     /* just once */

  ASI_CAMERA_INFO info;
  err = ASIGetCameraProperty(&info,self->handle);
 
  if (!err) self->aoiW = info.MaxWidth;
  if (!err) self->aoiH = info.MaxHeight;

#if (DEBUG > 1)
  if (!err) {  int startX,startY;
    err = ASIGetStartPos(handle,&startX,&startY);
#if (DEBUG > 1)
    if (!err) printf("startX=%d, startY=%d\n",startX,startY);
#endif
    assert(startX == 0);
    assert(startY == 0);
  }
#endif
 
#if 0 // todo 
#if (DEBUG > 1) // default=40 for USB3
  if (!err) { double d=0;
    err = AT_GetFloat(handle,L"MaxInterfaceTransferRate",&d);
    if (!err) printf("transferRate=%f\n",d);
  }
#endif
#endif

#if 0 // todo 
#if (DEBUG > 1) // default=6.5 
  if (!err) { double d=0;
    err = AT_GetFloat(handle,WSTR_PIXEL_H,&d);
    if (!err) printf("pixelHeight=%f\n",d);
  }
  if (!err) { double d=0;
    err = AT_GetFloat(handle,WSTR_PIXEL_W,&d);
    if (!err) printf("pixelWidth=%f\n",d);
  }
#endif
#endif

  if (!err) err = asi_tempcon(self,0.0f,1);
  if (!err) err = asi_exptime(self,0.5);
  if (!err) self->initialized = 1;

  self->err = err;

  return err;  
}

/* ---------------------------------------------------------------- */

int asi_close(AsiStruct* self)
{
  int err=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%s(%p) self.err=%d\n",__FILE__,PREFUN,self,self->err);
#endif
  assert(self);

  pthread_mutex_lock(&self->ioLock);

  if (self->handle >= 0) {
    ASICloseCamera(self->handle);
  }
  self->handle = -1;
  self->initialized = 0;
  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */

int asi_free(AsiStruct* self)
{
  int i;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%s(%p)\n",__FILE__,PREFUN,self);
#endif
  assert(self);

  pthread_mutex_destroy(&self->ioLock);
  pthread_mutex_destroy(&self->frameLock);
  for (i=0; i<ASI_NBUFS; i++) {
    AsiFrame *frame = &self->frames[i];
    if (frame->data) free((void*)frame->data);
  }
  free((void*)self);

  return 0;
}

/* ---------------------------------------------------------------- */

static int asi_geometry(AsiStruct* self,int wmode)
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,wmode);
#endif

  pthread_mutex_lock(&self->ioLock);
  
  self->winMode = wmode;
  if (wmode) { 
    self->aoiW = self->winW / self->binH;
    self->aoiH = self->winH / self->binV;
  } else {
    self->aoiW = self->sensorW / self->binH;
    self->aoiH = self->sensorH / self->binV;
  }
  while (self->aoiW % 8) self->aoiW -= 1;
  while (self->aoiH % 2) self->aoiH -= 1;

  if (self->handle >= 0) {
    assert(self->binH == self->binV);
    ret = ASISetROIFormat(self->handle,self->aoiW,self->aoiH,self->binH,ASI_IMG_RAW16);
    if (wmode) {                       /* NOTE: after setROI */
      self->startX = self->winX / self->binH;
      self->startY = self->winY / self->binV;
      if (!ret) ret = ASISetStartPos(self->handle, self->startX, self->startY);
    } else {
      if (!ret) ret = ASIGetStartPos(self->handle,&self->startX,&self->startY);
    }
    if (ret == ASI_SUCCESS) {
      (void)ASIStartExposure(handle,ASI_FALSE); // NEW v0022 todo BitDepth
      (void)ASIStopExposure(handle); // NEW v0022
    }
  }
  printf("%s: ret=%d, w=%d, h=%d, x=%d, y=%d\n",PREFUN,ret,
         self->aoiW,self->aoiH,self->startX,self->startY);

  pthread_mutex_unlock(&self->ioLock);

  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

int asi_window(AsiStruct* self,int x,int y,int w,int h)
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d,%d,%d)\n",PREFUN,x,y,w,h);
#endif

  pthread_mutex_lock(&self->ioLock);
  self->winW = imax(0,imin(self->sensorW,w));
  self->winH = imax(0,imin(self->sensorH,h));
  self->winX = imax(0,imin(self->sensorW-w,x));
  self->winY = imax(0,imin(self->sensorH-h,y));
  pthread_mutex_unlock(&self->ioLock);
  int err = asi_geometry(self,1);

  return err; 
}

/* --- */

int asi_aoi(AsiStruct* self,int w,int h,int b)
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d,%d)\n",PREFUN,w,h,b);
#endif

  pthread_mutex_lock(&self->ioLock);
  self->winW = imax(0,imin(self->sensorW,w));
  self->winH = imax(0,imin(self->sensorH,h));
  self->winX = (self->sensorW-w)/2;
  self->winY = (self->sensorH-h)/2;
  self->binH = self->binV = b;
  pthread_mutex_unlock(&self->ioLock);
  int err = asi_geometry(self,1);

  return err;
}

/* ---------------------------------------------------------------- */

int asi_binning(AsiStruct* self,int value)
{
  int err=0;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d)\n",PREFUN,value);
#endif

  pthread_mutex_lock(&self->ioLock);

  self->binH = self->binV = value;
  if (self->handle >= 0) {
    int ret = ASISetControlValue(self->handle,ASI_HARDWARE_BIN,1,ASI_FALSE);
    if (ret != ASI_SUCCESS) err = E_asi_error;
  }
  pthread_mutex_unlock(&self->ioLock);

  if (!err) err = asi_geometry(self,self->winMode);

  return err;
}

/* ---------------------------------------------------------------- */

int asi_temperature(AsiStruct *self)
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 1)
  fprintf(stderr,"%s:%s(%p)\n",__FILE__,PREFUN,self);
#endif
  assert(self->handle >= 0);
  if (self->handle < 0) return E_NOTINIT;

  pthread_mutex_lock(&self->ioLock);

  if (self->handle >= 0) { long temp,perc; ASI_BOOL bAuto;
    ret = ASIGetControlValue(self->handle,ASI_TEMPERATURE,&temp,&bAuto);
    if (!ret) self->tempSensor = (float)temp/10.0f;
    ret = ASIGetControlValue(self->handle,ASI_COOLER_POWER_PERC,&perc,&bAuto);
    if (!ret) self->percent = (float)perc;
#if (DEBUG > 1)
    long cool;
    ASIGetControlValue(self->handle,ASI_TARGET_TEMP,&temp,&bAuto);
    ASIGetControlValue(self->handle,ASI_COOLER_ON  ,&cool,&bAuto);
    printf("target=%ld, perc=%ld, cool=%ld\n",temp,perc,cool);
#endif
  }
  pthread_mutex_unlock(&self->ioLock);

  int err = (ret == ASI_SUCCESS) ? 0 : E_asi_error;
  if (err) self->err = err;

  return err;
}

/* ---------------------------------------------------------------- */

int asi_tempcon(AsiStruct* self,float temp,int cool)
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%.1f,%d)\n",PREFUN,temp,cool);
#endif
  assert(!ret);

  pthread_mutex_lock(&self->ioLock);

#ifdef SIM_ONLY
  self->tempSensor = (cool) ? temp : 25.0f;
  self->percent = (cool) ? 50-temp : 0;
#endif
  if (self->handle >= 0) { int value;
    value = (int)floor(0.5+temp);
    if (!ret) ret = ASISetControlValue(self->handle,ASI_TARGET_TEMP,value,ASI_FALSE);
    value = (cool) ? 1 : 0;
    if (!ret) ret = ASISetControlValue(self->handle,ASI_COOLER_ON,value,ASI_FALSE);
  }
   
  pthread_mutex_unlock(&self->ioLock);

  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

int asi_exptime(AsiStruct* self,double value)
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%f)\n",PREFUN,value);
#endif

  pthread_mutex_lock(&self->ioLock);
  
  self->expTime = value;
  if (self->handle >= 0) {
    int et = (int)floor(1.0e6*value+0.5);
    if (et < 32) et = 32;
    if (et > 2000000000) et = 2000000000;
    ret = ASISetControlValue(self->handle,ASI_EXPOSURE,et,ASI_FALSE);
    self->expTime = et*1.0e-6;
  }
  pthread_mutex_unlock(&self->ioLock);

  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

int asi_gain(AsiStruct* self,double value)
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%f)\n",PREFUN,value);
#endif

  pthread_mutex_lock(&self->ioLock);

  self->gain = fmax(0.0,fmin(1.0,value));
  if (self->handle >= 0) {
    int v = (int)floor(0.5+self->gain*600.0);
    ret = ASISetControlValue(self->handle,ASI_GAIN,v,ASI_FALSE);
  }
  pthread_mutex_unlock(&self->ioLock);

  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

int asi_offset(AsiStruct* self,double value)
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%f)\n",PREFUN,value);
#endif

  pthread_mutex_lock(&self->ioLock);

  self->offset = fmax(0.0,fmin(1.0,value));
  if (self->handle >= 0) {
    int v = (int)floor(0.5+self->offset*100.0);
    ret = ASISetControlValue(self->handle,ASI_OFFSET,v,ASI_FALSE);
  }
  pthread_mutex_unlock(&self->ioLock);

  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

int asi_flip(AsiStruct* self,int x,int y)
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d)\n",PREFUN,x,y);
#endif

  pthread_mutex_lock(&self->ioLock);

  if (self->handle >= 0) {
    int value = x+2*y;
    ret = ASISetControlValue(self->handle,ASI_FLIP,value,ASI_FALSE);
  }
  pthread_mutex_unlock(&self->ioLock);

  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

static void* run_cycle(void* param)
{
  int err=0;
  AsiStruct *self=(AsiStruct*)param;
  double t1,t2;
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s:%s(%p)\n",__FILE__,PREFUN,param);
#endif
  assert(self->handle >= 0);
  message(self,PREFUN,MSG_FLUSH);

  pthread_mutex_lock(&self->ioLock);
  int bufSize = self->aoiW * self->aoiH * sizeof(u_short);
  if (self->videoMode) err = ASIStartVideoCapture(self->handle);
  else                 err = ASIStartExposure(self->handle,ASI_FALSE);
  t1 = walltime(0);
  pthread_mutex_unlock(&self->ioLock);

  self->fps = 0;
  if (!err) {
    self->err = 0;
    while (!self->stop_flag) {
      msleep(5); /* IDEA query temperature here ? */
      pthread_mutex_lock(&self->ioLock);
      if (self->videoMode) {
        int ret = ASIGetVideoData(self->handle,self->data_0,bufSize,350);
        // printf("ret=%d\n",ret);
        err = (ret == ASI_SUCCESS) ? 0 : E_asi_timeout;
      } else {
        ASI_EXPOSURE_STATUS status=ASI_EXP_WORKING;
        double timeout = walltime(0)+0.350;
        do {
          ASIGetExpStatus(self->handle,&status); // printf("status=%d\n",status);
          if (status != ASI_EXP_WORKING) break;
        } while (walltime(0) < timeout);
        err = E_asi_timeout;
        if (status == ASI_EXP_SUCCESS) { 
          int ret = ASIGetDataAfterExp(self->handle,self->data_0,bufSize);
          err = (ret == ASI_SUCCESS) ? 0 : E_asi_timeout;
        }
      }
      pthread_mutex_unlock(&self->ioLock);
      if (self->stop_flag) break;
      if (err == E_asi_timeout) { double dt = walltime(0)-t1;
        if (dt > 2+MAX_EXPTIME) {
          sprintf(buf,"%s: err=%d",PREFUN,err);
          message(self,buf,MSG_FLUSH);
          t1 = walltime(0);
        }
        continue;
      }
      if (err) break;                  /* other error -- terminate */
#if 1
      u_short *p = (u_short*)self->data_0; // todo co-add
      int npix = self->aoiW * self->aoiH;
      for (  ; npix>0; npix--) *p++ >>= 4;
#endif
      t2 = walltime(0);
      self->fps = 0.5*self->fps + 0.5/(t2-t1);
      t1 = t2;
      self->seqNumber += 1;
      AsiFrame *frame = asi_frame4writing(self,self->seqNumber);
      if (frame) {
        frame->seqNumber = self->seqNumber;
        frame->timeStamp = t2 - self->expTime;
        memcpy(frame->data,self->data_0,bufSize);
        asi_frame_release(self,frame);
      }
      pthread_mutex_lock(&self->ioLock);
      if (!self->videoMode) ASIStartExposure(self->handle,ASI_FALSE);
      pthread_mutex_unlock(&self->ioLock);
      if (err) {  
        sprintf(buf,"%s: PANIC -- buffer queue failed, err=%d",PREFUN,err);
        message(self,buf,MSG_FILE);
      }
    } /* endwhile (!stop_flag) */
  } /* endif(!err) */
  free((void*)self->data_0); self->data_0 = NULL;

  self->err = err; // todo just if != E_asi_timeout ?
  sprintf(buf,"%s/%s: done, err=%d",__FILE__,PREFUN,err);
#if (DEBUG > 0)
  fprintf(stderr,"%s\n",buf);
#endif
  if (err) message(self,buf,MSG_FLUSH);

  return (void*)(long)err;
}

/* ---------------------------------------------------------------- */

int asi_cycle_start(AsiStruct* self)
{
  int err=0;

  message(NULL,PREFUN,MSG_FLUSH);

  pthread_mutex_lock(&self->ioLock);

  size_t size = self->aoiW * self->aoiH * sizeof(u_short);
  self->data_0 = (u_char*)malloc(size);

  pthread_mutex_unlock(&self->ioLock);

  if (!err) { int i;
    for (i=0; i<ASI_NBUFS; i++) {
      AsiFrame *frame = &self->frames[i];    
      frame->timeStamp = 0;
      frame->seqNumber = 0;
      assert(!frame->data);
      frame->data = (u_short*)malloc(size);
      assert(((u_long)frame->data & 0x07) == 0);
      frame->w = self->aoiW;
      frame->h = self->aoiH;
      frame->wlock = 0;
      frame->rlock = 0;
    }
  }

  if (!err) {
    self->stop_flag = 0;
    thread_create(run_cycle,(void*)self,&self->tid);
  } else {
    self->tid = 0;
  }

  return err;
}

/* ---------------------------------------------------------------- */

int asi_cycle_stop(AsiStruct *self)
{
  int  ret,err=0,i;
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s:%s(%p)\n",__FILE__,PREFUN,self);
#endif
  message(NULL,PREFUN,MSG_FLUSH);

  pthread_mutex_lock(&self->ioLock);
  if (self->videoMode) ret = ASIStopVideoCapture(self->handle);
  else                 ret = ASIStopExposure(self->handle);
  err = (ret==ASI_SUCCESS) ? 0 : E_asi_error;
  sprintf(buf,"%s: err(stop)=%d",PREFUN,err);
  message(self,buf,MSG_FILE);
  pthread_mutex_unlock(&self->ioLock);

  self->stop_flag = 1;
  pthread_join(self->tid,NULL); self->tid = 0;

  for (i=0; i<ASI_NBUFS; i++) {
    AsiFrame *f = &self->frames[i]; 
    while (f->wlock != 0) { 
      msleep(50); fprintf(stderr,"%s: f=%d, wlock=%d\n",PREFUN,i,f->wlock); 
    }
    while (f->rlock != 0) { 
      msleep(50); fprintf(stderr,"%s: f=%d, rlock=%d\n",PREFUN,i,f->rlock); 
    } 
    if (f->data) { free((void*)f->data); f->data = NULL; }
  }

  return err;
}

/* ---------------------------------------------------------------- */

AsiFrame* asi_frame4writing(AsiStruct* self,u_int s)
{
  int i;
  AsiFrame *frame=NULL;

  pthread_mutex_lock(&self->frameLock);
  
  for (i=0; i<ASI_NBUFS; i++) {
    AsiFrame *f = &self->frames[i];    
    assert(f->data);
    assert(f->wlock == 0);             // no frame locked for writing
    if (f->rlock == 0) {               // not locked for reading
      if (f->seqNumber < s) {          // oldest frame
        frame = f;
        s = frame->seqNumber;
      }
    }
  }
  if (!frame) { char buf[128];
    sprintf(buf,"PANIC: no frame available for writing");
    message(self,buf,MSG_FILE);
  } else {
    frame->wlock = 1;
  }
  pthread_mutex_unlock(&self->frameLock);
        
  return frame;
}

/* ---------------------------------------------------------------- */

AsiFrame* asi_frame4reading(AsiStruct* self,u_int s)
{
  int i;
  AsiFrame *frame=NULL;

  if (self->stop_flag) return NULL;

  pthread_mutex_lock(&self->frameLock);
  
  for (i=0; i<ASI_NBUFS; i++) {
    AsiFrame *f = &self->frames[i];    
    assert(f->data);
    if (f->wlock == 0) {               // not locked for writing
      if (f->seqNumber > s) {          // newest frame
        frame = &self->frames[i];
        s = frame->seqNumber;
      }
    }
  }
  if (frame) {
    frame->rlock += 1;
  }
  pthread_mutex_unlock(&self->frameLock);
        
  return frame;
}


/* ---------------------------------------------------------------- */

void asi_frame_release(AsiStruct* self,AsiFrame *frame)
{
  pthread_mutex_lock(&self->frameLock);

  if (frame->wlock) {                  /* is locked for writing */
    assert(frame->rlock == 0);
    assert(frame->wlock == 1);
    frame->wlock = 0;
  } else {                             /* is locked for reading */
    assert(frame->rlock >  0);
    frame->rlock -= 1;
    assert(frame->rlock >= 0);
  }
  
  pthread_mutex_unlock(&self->frameLock);
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

