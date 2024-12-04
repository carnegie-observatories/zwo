/* ---------------------------------------------------------------- */
//
// ZWO.m
// ZWOFinder
//
// Created by Christoph Birk on 2019-01-14
// Copyright © 2019 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#define DEBUG      0                   // debug level

/* ---------------------------------------------------------------- */

#import "ZWO.h"
#import "ASICamera2.h"
#import "Yutils.h"

/* ---------------------------------------------------------------- */

@implementation ZWO                    // b0014
{
}

/* ---------------------------------------------------------------- */
#pragma mark "Class"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890


/* ---------------------------------------------------------------- */
#pragma mark "Instance"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (void)dealloc
{
 [_name release];
 [super dealloc];
}

/* ---------------------------------------------------------------- */

#if (DEBUG > 0)
- (int)open
{
#if (DEBUG > 0)
  if (count == 0) fprintf(stderr,"%s: host=%s port=%d count=%d\n",PREFUN,host,port,count);
#endif

  int err = [super open]; if (err) return err;

  return err;
}
#endif

/* ---------------------------------------------------------------- */

#if (DEBUG > 0)
- (void)close
{
#if (DEBUG > 0)
  if (count == 1) fprintf(stderr,"%s: host=%s port=%d count=%d\n",PREFUN,host,port,count);
#endif

  [super close];
}
#endif

/* ---------------------------------------------------------------- */
#pragma mark "Instance/ASI"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (int)getNumASI
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s()\n",PREFUN);
#endif

  sprintf(cmd,"ASIGetNum\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (err) return -1;

  return atoi(res);
}

/* ---------------------------------------------------------------- */

- (int)getCameraProperty
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

  sprintf(cmd,"ASIGetCameraProperty\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) { char *p,*b=res;
    p = strsep(&b," "); if (p) _maxWidth  = atoi(p);
    p = strsep(&b," "); if (p) _maxHeight = atoi(p);
    p = strsep(&b," "); if (p) _hasCooler = atoi(p); // b0028
    p = strsep(&b," "); if (p) _isColor   = atoi(p); // b0028
    p = strsep(&b," "); if (p) _bitDepth  = atoi(p); // b0037
    self.name = @(b);
    NSLog(@"name=%@",self.name); // TODO serial number may be appended
  }

  return err;
}

/* ---------------------------------------------------------------- */

- (int)setupASI 
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s: count=%d\n",PREFUN,count);
#endif

  int err = [self open]; if (err) return err;
  
  sprintf(cmd,"ASIOpenCamera\n");
  if (!err) err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);
  sprintf(cmd,"ASIInitCamera\n");
  if (!err) err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);
  sprintf(cmd,"ASIGetCameraProperty\n");
  if (!err) err = [self request:cmd response:res max:sizeof(res)];
  if (!err) { char name[64]="";
    sscanf(res,"%d %d %d %d %d %s",&_maxWidth,&_maxHeight,
               &_hasCooler,&_isColor,&_bitDepth,name);
    self.name = @(name);         // NEW b0040
    NSLog(@"name=%@",self.name); // TODO serial number may be appended
  }

  return err;
}

/* ---------------------------------------------------------------- */

- (int)closeASI
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s: host=%s port=%d count=%d\n",PREFUN,host,port,count);
#endif

  sprintf(cmd,"ASICloseCamera\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);

  [self close];
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)setControl:(int)control value:(int)value
{
  char cmd[128],res[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d,%d)\n",PREFUN,control,value);
#endif

  sprintf(cmd,"ASISetControlValue %d %d\n",control,value);
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);

  return err;
}

/* --- */

- (int)getControl:(int)control value:(int*)value
{
  char cmd[128],res[128];
#if (DEBUG > 2)
  fprintf(stderr,"%s(%d,%p)\n",PREFUN,control,value);
#endif

  sprintf(cmd,"ASIGetControlValue %d\n",control);
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) sscanf(res,"%d %d",&err,value);

  return err;
}

/* ---------------------------------------------------------------- */

- (int)setROIFormatW:(int)w H:(int)h B:(int)b T:(int)t
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d,%d,%d)\n",PREFUN,w,h,b,t);
#endif

  sprintf(cmd,"ASISetROIFormat %d %d %d %d\n",w,h,b,t);
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);

  return err;
}

/* ---------------------------------------------------------------- */

- (int)getROIFormatW:(int*)w H:(int*)h B:(int*)b T:(int*)t
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%p,%p,%p)\n",PREFUN,w,h,b,t);
#endif

  sprintf(cmd,"ASIGetROIFormat\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) sscanf(res,"%d %d %d %d %d",&err,w,h,b,t);
  if (!err) { _width = *w; _height = *h; _binning = *b; _imageType = *t; }

  return err;
}

/* ---------------------------------------------------------------- */

- (int)setStartPosX:(int)x Y:(int)y
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d)\n",PREFUN,x,y);
#endif

  sprintf(cmd,"ASISetStartPos %d %d\n",x,y);
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);
  if (!err) { _startX = x; _startY = y; }
  
  return err;
}

/* --- */

- (int)getStartPosX:(int*)x Y:(int*)y
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%p)\n",PREFUN,x,y);
#endif

  sprintf(cmd,"ASIGetStartPos\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) sscanf(res,"%d %d %d",&err,x,y);
  if (!err) { _startX = *x; _startY = *y; }
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)startExposure
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s()\n",PREFUN);
#endif

  sprintf(cmd,"ASIStartExposure\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);
  if (!err) _expStatus = ASI_EXP_WORKING;
  
  return err;
}

/* --- */

- (int)getExpStatus
{
  char cmd[128],res[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s()\n",PREFUN);
#endif
  
  sprintf(cmd,"ASIGetExpStatus\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) sscanf(res,"%d %d",&err,&_expStatus);

  return err;
}

/* ---------------------------------------------------------------- */

- (int)getDataAfterExp:(u_char*)data size:(size_t)size
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%lu)\n",PREFUN,data,size);
#endif

  int err = [self open]; if (err) return err;
  
  sprintf(cmd,"ASIGetDataAfterExp %lu\n",size);
  err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);

  if (!err) { char *p = (char*)data; ssize_t n=0;
    // NSDate *date = [NSDate date];
    for (int i=0; i<size/42; i++) {
      ssize_t r = TCPIP_Recv(sock,p,size-n,2000); // bug-fix b0018
      if (r <= 0) break;
      n += r; p += r; if (n >= size) break;
    }
    if (n < size) err = -2; // TODO proper error code
    // float dt=YsecSince(date); printf("%.1f MB/s (%.3f)\n",((float)size/1.0e6)/dt,dt);
    // Raspberry: 11.8 MB/s
    // Rock64:    99.5 MB/s
    // Minnow:   115.0 MB/s
    // NUC:      115.0 MB/s
  }
  [self close];
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)startVideoCapture
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s()\n",PREFUN);
#endif

  sprintf(cmd,"ASIStartVideoCapture\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)getVideoData:(u_char*)data size:(size_t)size wait:(int)msec
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%lu)\n",PREFUN,data,size);
#endif

  int err = [self open]; if (err) return err;

  sprintf(cmd,"ASIGetVideoData %lu %d\n",size,msec);
  err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);

  if (!err) { char *p = (char*)data; ssize_t n=0;
    // NSDate *date = [NSDate date];
    for (int i=0; i<size/42; i++) {
      ssize_t r = TCPIP_Recv(sock,p,size-n,2000);
      if (r <= 0) break;
      n += r; p += r; if (n >= size) break;
    }
    if (n < size) err = -2; // TODO proper error code
    // float dt=YsecSince(date); printf("%.1f MB/s (%.3f)\n",((float)size/1.0e6)/dt,dt);
    // Raspberry: 11.6 MB/s
    // Rock64: 70-90.0 MB/s
    // Minnow:   115.5 MB/s
    // NUC:      116.5 MB/s
  }

  [self close];
  // Raspberry: et=0.020 [fps] 1x1=0.6 2x2= 2.4  4x4=6.2
  // Rock64:    et=0.020 [fps] 1x1=2.5 2x2= 8.5  4x4=11.0
  // Minnow:    et=0.020 [fps] 1x1=2.8 2x2= 8.3  4x4=8.9
  // NUC:       et=0.020 [fps] 1x1=3.3 2x2=12.6  4x4=21
  // USB3:               [fps] 1x1=6.7 2x2=23+

  return err;
}

/* ---------------------------------------------------------------- */

- (int)getDroppedFrames
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s()\n",PREFUN);
#endif

  sprintf(cmd,"ASIGetDroppedFrames\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) sscanf(res,"%d %d",&err,&_droppedFrames);
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)stopVideoCapture
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s()\n",PREFUN);
#endif

  sprintf(cmd,"ASIStopVideoCapture\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);
  
  return err;
}

/* ---------------------------------------------------------------- */
#pragma mark "Instance/EFW"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (int)getNumEFW
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s()\n",PREFUN);
#endif

  sprintf(cmd,"EFWGetNum\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (err) return -1;

  return atoi(res);
}

/* ---------------------------------------------------------------- */

- (int)openEFW
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s: host=%s port=%d count=%d\n",PREFUN,host,port,count);
#endif

  int err = [self open]; if (err) return err;
  
  sprintf(cmd,"EFWOpen\n");
  err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)closeEFW
{
  char cmd[128],res[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s: host=%s port=%d count=%d\n",PREFUN,host,port,count);
#endif

  sprintf(cmd,"EFWClose\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);
  [self close];
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)getProperty
{
  char cmd[128],res[128],name[32];

  sprintf(cmd,"EFWGetProperty\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) sscanf(res,"%d %d %d %s",&err,&_efw_id,&_numSlots,name);
  if (!err) self.efwName = @(name);

  return err;
}

/* ---------------------------------------------------------------- */

- (int)getDirection
{
  char cmd[128],res[128];

  sprintf(cmd,"EFWGetDirection\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) sscanf(res,"%d %d",&err,&_uniDir);

  return err;
}

/* ---------------------------------------------------------------- */

- (int)getPosition
{
  char cmd[128],res[128];

  sprintf(cmd,"EFWGetPosition\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) sscanf(res,"%d %d",&err,&_position);

  return err;
}

/* ---------------------------------------------------------------- */

- (int)setPosition:(int)pos
{
  char cmd[128],res[128];

  sprintf(cmd,"EFWSetPosition %d\n",pos);
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);

  return err;
}

/* ---------------------------------------------------------------- */

- (int)calibrate
{
  char cmd[128],res[128];

  sprintf(cmd,"EFWCalibrate\n");
  int err = [self request:cmd response:res max:sizeof(res)];
  if (!err) err = atoi(res);

  return err;
}

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
