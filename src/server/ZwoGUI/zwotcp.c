/* ---------------------------------------------------------------- */
//
// ZWO library (TCP)
// 2019-01-07  Christoph C. Birk (birk@carnegiescience.edu)
//
/* ---------------------------------------------------------------- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include "zwo.h"
#include "zwotcp.h"
#include "tcpip.h"
#include "ptlib.h"
#include "utils.h"

/* ---------------------------------------------------------------- */

#define DEBUG           1
#define TIME_TEST       0

#ifndef PREFUN
#define PREFUN          __func__
#endif

/* ---------------------------------------------------------------- */
/* zwo_() */
/* ---------------------------------------------------------------- */
 
ZwoStruct* zwo_create(const char* host,int port)
{
  int i;

  ZwoStruct *self = (ZwoStruct*)malloc(sizeof(ZwoStruct));
  if (!self) return NULL;

  strcpy(self->host,host);
  self->port = port;
  self->handle = -1;                   /* connection closed */
  self->err = 0;                       /* used from threads */
  self->serverVersion[0] = '\0';
  self->tempSensor = self->percent = 0.0f;
  self->aoiW = self->aoiH = 0;
  self->expTime = 0.5f;
  self->gain = self->offset = 0.0;
  self->fps = 0.0;
  self->rolling = 0;
  self->filter = 0;

  pthread_mutex_init(&self->ioLock,NULL);
  pthread_mutex_init(&self->frameLock,NULL);

  self->seqNumber = 0;
  for (i=0; i<ZWO_NBUFS; i++) {
    ZwoFrame *frame = &self->frames[i];
    frame->data = NULL;
  }
  self->tid = 0;
  self->stop_flag = 1;

  return self;
}

/* ---------------------------------------------------------------- */

static int zwo_request(ZwoStruct* self,const char* cmd,char* res,int tout)
{
  char command[128],answer[128];
  assert(strlen(cmd) < sizeof(command)+2);
  assert(self->handle >= 0);
  if (self->handle < 0) return E_NOTINIT;
  assert(pthread_mutex_trylock(&self->ioLock));  /* must fail here */

  sprintf(command,"%s\n",cmd);
  int err = TCPIP_Request3(self->handle,command,answer,sizeof(answer),tout);
  if (!err) { if (res) strcpy(res,answer); }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_connect(ZwoStruct* self)
{
  int  err=0,handle=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s:%d)\n",PREFUN,self->host,self->port);
#endif
  assert(self);
  assert(self->handle < 0);

  pthread_mutex_lock(&self->ioLock);
  if (!err) {
    handle = TCPIP_CreateClientSocket(self->host,self->port,&err);
#if (DEBUG > 0)
    printf("%s(%s:%d): handle=%d\n",PREFUN,self->host,self->port,handle);
#endif
    if (!err) self->handle = handle;
  }
  if (!err) { char buf[128];
    err = zwo_request(self,"version",buf,5);
    if (!err) {
      assert(strlen(buf) < sizeof(self->serverVersion));
      sscanf(buf,"%s %u",self->serverVersion,&self->cookie);
      sprintf(buf,"serverVersion= %s",self->serverVersion);
      message(self,buf,MSG_FILE);
    }
  }
  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_open(ZwoStruct* self)
{
  int  err=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s:%d)\n",PREFUN,self->host,self->port);
#endif
  assert(self);

  if (self->handle < 0) err = zwo_connect(self);

  if (!err) { char buf[128];
    pthread_mutex_lock(&self->ioLock);
    err = zwo_request(self,"open",buf,40);
    printf("buf=%s (%lu)\n",buf,strlen(buf));
    if (!err) {
      sscanf(buf,"%d %d %d %d %d",&self->maxWidth,&self->maxHeight,
             &self->hasCooler,&self->isColor,&self->bitDepth);
    } 
    pthread_mutex_unlock(&self->ioLock);
  }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_setup(ZwoStruct* self,int w,int h,int bin)
{ 
  int  err=0,x,y,b,p;
  char cmd[128],buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d,%d)\n",PREFUN,w,h,bin);
#endif
  assert(self);
  assert(self->handle >= 0);
  if (self->handle < 0) return E_NOTINIT;
  assert(self->tid == 0);
  if (self->tid) return E_RUNNING;

  pthread_mutex_lock(&self->ioLock);

  sprintf(cmd,"offtime %ld",cor_time(0));
  if (!err) err = zwo_request(self,cmd,buf,5);

  sprintf(cmd,"setup %d %d %d %d %d %d",0,0,w,h,bin,16);
  if (!err) err = zwo_request(self,cmd,buf,5);
  if (!err) sscanf(buf,"%d %d %d %d %d %d",&x,&y,
                   &self->aoiW,&self->aoiH,&b,&p);

  sprintf(cmd,"exptime");
  if (!err) err = zwo_request(self,cmd,buf,5);
  if (!err) self->expTime = atof(buf); 

  pthread_mutex_unlock(&self->ioLock);
 
  self->err = err;

  return err;  
}

/* ---------------------------------------------------------------- */

int zwo_disconnect(ZwoStruct* self)
{
  int err=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,self);
#endif
  assert(self);

  if (self->tid) return E_RUNNING;

  pthread_mutex_lock(&self->ioLock);
  if (self->handle >= 0) err = close(self->handle);
  self->handle = -1;
  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_close(ZwoStruct* self)
{
#if (DEBUG > 0)
  fprintf(stderr,"%s %s(%p): tid=%ld\n",__FILE__,PREFUN,self,self->tid);
#endif
  assert(self);

  if (self->handle < 0) return 0;
  if (self->tid) return E_RUNNING;

  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,"close",NULL,5);
  pthread_mutex_unlock(&self->ioLock);
  zwo_disconnect(self);
  message(self,"disconnected from zwoserver",MSG_FLUSH);

  return err;
}

/* ---------------------------------------------------------------- */

void zwo_free(ZwoStruct* self)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,self);
#endif
  assert(self);

  pthread_mutex_destroy(&self->ioLock);
  pthread_mutex_destroy(&self->frameLock);

  free((void*)self);
}

/* ---------------------------------------------------------------- */

int zwo_temperature(ZwoStruct *self)
{
  char buf[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,self);
#endif
  assert(self->handle >= 0);

  pthread_mutex_lock(&self->ioLock); 
  int err = zwo_request(self,"tempcon",buf,5);
  pthread_mutex_unlock(&self->ioLock);
  if (!err) { double t,p; 
    if (sscanf(buf,"%lf %lf",&t,&p) == 2) {
      self->tempSensor = (float)t;
      self->percent    = (float)p;
    }
  }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_tempcon(ZwoStruct* self,int value)
{
  char cmd[128],buf[128];

  sprintf(cmd,"tempcon %d",value);
  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,buf,5);
  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_exptime(ZwoStruct* self,double value)
{
  char cmd[128],buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%f)\n",PREFUN,value);
#endif

  value = fmin(MAX_EXPTIME,fmax(MIN_EXPTIME,value));
  sprintf(cmd,"exptime %.6f",value);
  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,buf,5);
  pthread_mutex_unlock(&self->ioLock);
  if (!err) self->expTime = value;

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_offset(ZwoStruct* self,int value)
{
  char cmd[128],buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,value);
#endif

  sprintf(cmd,"offset %d",value);
  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,buf,5);
  pthread_mutex_unlock(&self->ioLock);
  if (!err) self->offset = value;

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_gain(ZwoStruct* self,int value)
{
  char cmd[128],buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,value);
#endif

  sprintf(cmd,"gain %d",value);
  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,buf,5);
  pthread_mutex_unlock(&self->ioLock);
  if (!err) self->gain = value;

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_filter(ZwoStruct* self,int value)
{
  char cmd[128],buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,value);
#endif

  value = fmin(6,fmax(0,value));
  sprintf(cmd,"filter %d",value);
  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,buf,5); // todo depends if we wait
  pthread_mutex_unlock(&self->ioLock);
  if (!err) {
    if (strncmp(buf,"-E",2)) {
      self->filter = atoi(buf);
    } else { 
      char *p = strchr(buf,'='); if (p) err = atoi(p+1);
    }
  }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_flip(ZwoStruct* self,int x,int y)
{
  char cmd[128],buf[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d,%d)\n",PREFUN,self,x,y);
#endif

  sprintf(cmd,"flip %d %d",x,y);
  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,buf,5);
printf("err=%d, buf=%s\n",err,buf);
  pthread_mutex_unlock(&self->ioLock);

  return err; 
}

/* ---------------------------------------------------------------- */

#if 0 // at TCP or USB ? -- here -- don't send it to server
int zwo_rolling(ZwoStruct* self,int rolling)
{
  char cmd[128],buf[128];

  sprintf(cmd,"rolling %d",rolling);
  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,buf,5);
  pthread_mutex_unlock(&self->ioLock);
  if (!err) self->rolling = rolling;

  return err;
}
#endif

/* ---------------------------------------------------------------- */

// todo allow looptime 

static void* run_cycle(void* param) // todo simplify
{
  ZwoStruct *self=(ZwoStruct*)param;
  int     i,n,size,err=0,last_err=0;
  double  t1,t2;
  u_int   seq=0;
  char    buf[128];
  u_short *roll_buf=NULL;
#if (DEBUG > 0)
  fprintf(stderr,"%s: %s(%p)\n",__FILE__,PREFUN,param);
#endif
  message(self,PREFUN,MSG_FLUSH);
#if (TIME_TEST > 1)
  double s1=0,s2=0,sn=0,sm=0,dt,last_expt=0;
  time_t last=0;
#endif
  assert(self->handle >= 0);

  t1 = walltime(0);

  int npix = self->aoiW * self->aoiH;
  int nbytes = npix * sizeof(u_short);
  u_char *data = (u_char*)malloc(nbytes);

  self->fps = 0;
  self->err = 0;
  while (!self->stop_flag) {
    msleep(500); ///xxx
    pthread_mutex_lock(&self->ioLock);
    err = zwo_request(self,"data",buf,5);
    if (err) {
      fprintf(stderr,"%s: err=%d\n",PREFUN,err);
    } else
    if (!strncmp(buf,"-E",2)) {
      if (strstr(buf,"-Enodata")) {
        float dt = walltime(0)-t1;
        if (dt > self->expTime+0.5f) {       /* todo loop time */
          printf("%s: %s:%d - no data %.1f\n",get_local_timestr(buf,0),
                 self->host,self->port,dt);
        }
      } else {
        printf("%s: other error '%s'\n",PREFUN,buf);
        char *p = strchr(buf,'='); 
        err = (p) ? atoi(p+1) : E_ERROR;
      }
    } else {
      size = atoi(buf);
      assert(nbytes == size);
      for (i=0,n=0; i<nbytes/42; i++) {
        ssize_t r = TCPIP_Recv(self->handle,(char*)data+n,nbytes-n,2);
        if (r <= 0) break;
        n += r; if (n >= nbytes) break;
      }
      t2 = walltime(0);
      self->fps = 0.7*self->fps + 0.3/(t2-t1);
      t1 = t2;
      if (n != nbytes) {               /* data missing */
        err = E_INCFRAME;
      } else {
        self->seqNumber = ++seq;
        ZwoFrame *frame = zwo_frame4writing(self,seq);
        if (frame) {
          frame->seqNumber = seq;
          //xxx frame->timeStamp = tst;
          if (self->rolling == 0) {    /* rolling average */
            memcpy(frame->data,data,nbytes);
            if (roll_buf) { free((void*)roll_buf); roll_buf=NULL; }
          } else { int mul=self->rolling; float div=1+self->rolling;
#if (TIME_TEST > 1) // IDEA "debug" level
            double c1 = walltime(0);
#endif
            if (!roll_buf) {
              roll_buf = (u_short*)malloc(npix*sizeof(u_short));
              memcpy(roll_buf,data,nbytes);
            } else {
              register u_short *d=(u_short*)data;
              register u_short *p=roll_buf;
              for (i=0; i<npix; i++,p++,d++) {
                *p = (u_short)(0.5f+(((int)*p * mul) + (int)*d) / div);
              }
            }
            memcpy(frame->data,roll_buf,nbytes);
#if (TIME_TEST > 1)
            if (self->expTime != last_expt) { 
              s1 = s2 = sn = sm = 0; last_expt = self->expTime;
            }
            dt = (walltime(0)-c1)*1000.0;   /* [ms] */
            s1 += dt; s2 += dt*dt; sn += 1.0; if (dt > sm) sm = dt;
            if (cor_time(0) > last) { 
              double ave = s1/sn;
              double sig = sqrt(s2/sn-ave*ave);
              sprintf(buf,"%s: ave=%.1f +- %.2f (%.1f)",PREFUN,ave,sig,sm);
              message(self,buf,MSG_FILE); printf("%s\n",buf);
              last = cor_time(0);
            }
#endif
          }
          zwo_frame_release(self,frame);
        } // endif(PANIC)
      } // endif(incomplete frame)
    } // endif(err)
    pthread_mutex_unlock(&self->ioLock);
    if (err) {
      if (err != last_err) {
        switch (err) {
        case E_tcpip_send:
          sprintf(buf,"sending req. to '%s' failed",self->host);
          break;
        case E_tcpip_timeout:
          sprintf(buf,"no response from '%s'",self->host);
          break;
        case E_RUNNING:  
          sprintf(buf,"%s: data-acq not running",PREFUN);
          break;
        case E_INCFRAME: 
          sprintf(buf,"incomplete frame from '%s'",self->host); 
          break;
        default:         
          sprintf(buf,"%s: err=%d",PREFUN,err); 
          break;
        }
        message(self,buf,MSG_ERROR);
        last_err = err; 
      }
      msleep(500);
    }
  } /* endwhile (!stop_flag) */

  if (roll_buf) { free((void*)roll_buf); roll_buf=NULL; }
  free((void*)data);
  self->err = err;
#if (DEBUG > 0)
  fprintf(stderr,"%s %s() done\n",__FILE__,PREFUN);
#endif

  return (void*)(long)err;
}

/* ---------------------------------------------------------------- */

int zwo_cycle_start(ZwoStruct* self)
{
  char buf[128];

  size_t size = self->aoiW * self->aoiH * sizeof(u_short);

  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,"start",buf,5);
  if (!err) zwo_request(self,"status",buf,5);
  if (!err) self->seqNumber = stringVal(buf,5);
  pthread_mutex_unlock(&self->ioLock);

  if (!err) { int i;
    for (i=0; i<ZWO_NBUFS; i++) {
      ZwoFrame *frame = &self->frames[i];    
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
    msleep(50);
  } else {
    self->tid = 0;
  }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_cycle_stop(ZwoStruct *self)
{
  int  err=0,i;
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,self);
#endif

  self->stop_flag = 1;
  pthread_join(self->tid,NULL);  
  self->tid = 0;

  pthread_mutex_lock(&self->ioLock);
  zwo_request(self,"stop",buf,5);
  zwo_request(self,"status",buf,5);
  self->seqNumber = stringVal(buf,4);
  pthread_mutex_unlock(&self->ioLock);

  for (i=0; i<ZWO_NBUFS; i++) {
    ZwoFrame *f = &self->frames[i]; 
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

ZwoFrame* zwo_frame4writing(ZwoStruct* self,u_int s)
{
  int i;
  ZwoFrame *frame=NULL;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%u)\n",PREFUN,self,s);
#endif

  pthread_mutex_lock(&self->frameLock);
  
  for (i=0; i<ZWO_NBUFS; i++) {
    ZwoFrame *f = &self->frames[i];    
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
    sprintf(buf,"%s: PANIC: no frame available for writing",__FILE__);
    message(self,buf,MSG_WARN | MSG_FILE);
  } else {
    frame->wlock = 1;
  }
  pthread_mutex_unlock(&self->frameLock);
        
  return frame;
}

/* ---------------------------------------------------------------- */

ZwoFrame* zwo_frame4reading(ZwoStruct* self,u_int s)
{
  int i;
  ZwoFrame *frame=NULL;

  if (self->stop_flag) return NULL;

  pthread_mutex_lock(&self->frameLock);
  
  for (i=0; i<ZWO_NBUFS; i++) {
    ZwoFrame *f = &self->frames[i];    
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

void zwo_frame_release(ZwoStruct* self,ZwoFrame *frame)
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

int zwo_server(ZwoStruct* self,const char* cmd,char* res) // todo 
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%s,%s)\n",PREFUN,self,cmd,par);
#endif
  assert(self);
  if (self->handle < 0) return E_NOTINIT;

  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,res,5);
  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

