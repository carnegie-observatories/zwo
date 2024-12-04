/* ---------------------------------------------------------------- */
//
// ASI.m
// ZWOFinder
//
// Created by Christoph Birk on 2018-12-05
// Copyright © 2018 Christoph Birk. All rights reserved.
//
// linking to static library (.a) works at build time
//  -- but complains about missing .dylib at runtime
//
/* ---------------------------------------------------------------- */

#import  "ASI.h"
#import  "Yutils.h"
#include "ASICamera2.h"

/* ---------------------------------------------------------------- */

#define DEBUG        1                 // debug level

#undef  SIM_ONLY

#define RAW_NAME(x)  (((x)==2) ? "raw16" : "raw8")
#define BPP_NAME(x)  (((x)==8) ? "raw8" : "raw16")

#if (DEBUG > 0)
#import "Logger.h"
extern Logger *main_logger;
#endif

/* ---------------------------------------------------------------- */

static int imax(int a,int b) { return (a>b) ? a : b; }
static int imin(int a,int b) { return (a<b) ? a : b; }

/* ---------------------------------------------------------------- */

@implementation ASI
{
  // IDEA NSRecursiveLock *mutex;
  ZWO *zwoServer;
  int device,handle;
  u_char *imgData;
  int wmax,hmax;
  int winX,winY,winW,winH;             // unbinned pixels
  BOOL winMode;
  char modelName[32];                  // NEW b0040
}

@dynamic imageData,videoData;

/* ---------------------------------------------------------------- */
#pragma mark "Class"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

+ (void)initialize
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

#ifdef SIM_ONLY
  if (self) return;
#endif

#if (DEBUG > 0)
  printf("ASI-SDK-Version= %s\n",ASIGetSDKVersion());
#endif
}

/* ---------------------------------------------------------------- */

+ (NSArray*)scan:(ZWO*)server
{
  int num;
  ASI_CAMERA_INFO info;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,server);
#endif

#ifdef SIM_ONLY
  if (self) {
    NSDictionary *d = @{@"Name"        : @"dummy",
                        @"MaxWidth"    : @(1920),
                        @"MaxHeight"   : @(1080),
                        @"IsCoolerCam" : @(1),
                        @"IsColorCam"  : @(0)};
    return @[d];
  }
#endif

  if (server) num = [server getNumASI];
  else        num = ASIGetNumOfConnectedCameras();
  if (num <= 0) return nil;
  
  NSMutableArray *a = [NSMutableArray arrayWithCapacity:1];
  for (int i=0; i<num; i++) {
    if (server) {
      [server getCameraProperty];
      info.MaxWidth = server.maxWidth;
      info.MaxHeight = server.maxHeight;
      info.IsCoolerCam = server.hasCooler; // b0028
      info.IsColorCam = server.isColor; // b0028
      info.BitDepth = server.bitDepth; // b0037
      strcpy(info.Name,[server.name UTF8String]);
#if (DEBUG > 1)
      printf("%d: %s, ID=%d\n",i,info.Name,i);
      printf("cooler=%d\n",info.IsCoolerCam);
      printf("color=%d\n",info.IsColorCam);
      printf("bitDepth=%d\n",info.BitDepth);
#endif
    } else {
      ASIGetCameraProperty(&info,i);
#if (DEBUG > 1)
      printf("%d: %s, ID=%d\n",i,info.Name,info.CameraID);
      printf("pixelSize=%f\n",info.PixelSize);
#if 0
      for (int j=0; j<16; j++) {
        printf(" %d",info.SupportedBins[j]); if (info.SupportedBins[j]==0) break;
      } printf("\n");
      for (int j=0; j<8; j++) {
        printf(" %d",info.SupportedVideoFormat[j]); if (info.SupportedVideoFormat[j] == ASI_IMG_END) break;
      } printf("\n");
#endif
      printf("cooler=%d\n",info.IsCoolerCam);
      printf("bitDepth=%d\n",info.BitDepth);
      printf("eGain=%f\n",info.ElecPerADU);
      printf("color=%d (bayer=%d)\n",info.IsColorCam,info.BayerPattern);
#endif
    }
    NSDictionary *d = @{@"Name"        : @(info.Name),
                        @"MaxWidth"    : @(info.MaxWidth),
                        @"MaxHeight"   : @(info.MaxHeight),
                        @"IsCoolerCam" : @(info.IsCoolerCam),
                        @"IsColorCam"  : @(info.IsColorCam)};
    [a addObject:d];                   // b0028
  }

  return a;
}

/* --- */

+ (NSString*)errorString:(int)err
{
  NSString *string=nil;
  
  switch (err) {
    case E_asi_error: string = @"ZWO/ASI: error"; break;
    case E_asi_devInvalid: string = @"ZWO/ASI: invalid device"; break;
    case E_asi_devNotFound: string = @"ZWO/ASI: device not found"; break;
    case E_asi_devNotOpen: string = @"ZWO/ASI: device open failed"; break;
    case E_asi_tempRead: string = @"ZWO/ASI: reading temperature sensor failed"; break;
    case E_asi_readout: string = @"ZWO/ASI: no data from camera"; break;
    // no Super to call
  }
  return string;
}

/* ---------------------------------------------------------------- */
#pragma mark "Instance"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (id)initWithServer:(ZWO*)server
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,server);
#endif

  if (!(self = [super init])) return nil;

  handle = -1;
  device = 0;
  _setpoint = 2.0f; 
#ifdef SIM_ONLY
  srand48(time(NULL)+(long)self);
  _Simulator = TRUE;
#endif
  zwoServer = [server retain]; 

  return self;
}

/* ---------------------------------------------------------------- */

- (int)open
{
  int err=0;
  ASI_CAMERA_INFO info;
#if (DEBUG > 0)
  fprintf(stderr,"%s(): device=%d, handle=%d\n",PREFUN,device,handle);
#endif
  assert(device >= 0);

#ifdef SIM_ONLY
  _w = wmax = 4656/2;
  _h = hmax = 3520/2;
  _bin = 1;
  _bitDepth = 16;
  *modelName = '\0';
  if (self) return 0;                  // always true
#endif

  if (handle == device) return 0;      // already open
  assert(handle < 0);

  if (zwoServer) {                     // network camera
    err = [zwoServer setupASI]; if (err) return err;
    handle = device;
    _w = wmax = zwoServer.maxWidth;
    _h = hmax = zwoServer.maxHeight;
    _hasCooler = zwoServer.hasCooler;  // b0027
    _isColor = zwoServer.isColor;      // b0027
    _bitDepth = zwoServer.bitDepth;    // b0037
    strcpy(modelName,[zwoServer.name UTF8String]); // NEW b0040
  } else { int i;                      // local USB camera
    int num = ASIGetNumOfConnectedCameras();
    for (i=0; i<num; i++) {
      ASIGetCameraProperty(&info,i);
      if (device == info.CameraID) {
        int ret = ASIOpenCamera(device);
        if (ret == ASI_SUCCESS) handle = device;
        break;
      }
    }
    if (i == num) return E_asi_devNotFound;
    if (handle < 0) return E_asi_devNotOpen;
    assert(!err);
    _w = wmax = (int)info.MaxWidth;
    _h = hmax = (int)info.MaxHeight;
    _hasCooler = info.IsCoolerCam ? YES : NO; // b0022
    _isColor = info.IsColorCam ? YES : NO; // b0026
    _bitDepth = info.BitDepth; // b0035
    strcpy(modelName,info.Name); // NEW b0040
    ASIInitCamera(handle);
#if (DEBUG > 1)
    ASI_CONTROL_CAPS caps; int numOfCtrl = 0;
    ASIGetNumOfControls(handle, &numOfCtrl);
    for (int i=0; i<numOfCtrl; i++) {
      ASIGetControlCaps(handle,i,&caps);
      printf("%24s %5ld %11ld %6ld %d %d %s\n",caps.Name,
             caps.MinValue,caps.MaxValue,caps.DefaultValue,
             caps.IsAutoSupported,caps.IsWritable,caps.Description);
    }
#endif
    ASISetControlValue(handle,ASI_BANDWIDTHOVERLOAD,100,ASI_FALSE); // b0034 -- IDEA at server
  }
  
  assert(!err);
  if (_hasCooler) {                    // b0029
    if (!err) err = [self setTemp:_setpoint cooler:TRUE];
  }
  if (!err) err = [self setExpTime:_exptime];
  if (!err) err = [self setGain:_gain];
  if (!err) err = [self setOffset:_offset];
  if (_isColor) {                      // b0029
    if (!err) err = [self setWhiteBalanceRed:_wbred blue:_wblue];
  }
  
  return 0; // NOTE: errors above are ignored -- separate open/setupDefaults
}

/* ---------------------------------------------------------------- */

- (void)close
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p: handle=%d\n",PREFUN,self,handle);
#endif

  if (handle >= 0) {
    if (zwoServer) [zwoServer closeASI];
    else ASICloseCamera(handle);
    handle = -1;
  }
  
  if (imgData) {
    free((void*)imgData); imgData = NULL;
  }
}

/* ---------------------------------------------------------------- */

- (void)dealloc
{
  [_date release];
  [super dealloc];
}

/* ---------------------------------------------------------------- */
#pragma mark "Parameters"
/* ---------------------------------------------------------------- */

- (int)setExpTime:(float)value
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%f): handle=%d\n",PREFUN,value,handle);
#endif

  @synchronized (self) {
    _exptime = value;
    if (handle >= 0) {
      int et = (int)floor(1.0e6*value+0.5);
      if (et < 32) et = 32;
      if (et > 2000000000) et = 2000000000;
      if (zwoServer) ret = [zwoServer setControl:ASI_EXPOSURE value:et];
      else ret = ASISetControlValue(handle,ASI_EXPOSURE,et,ASI_FALSE);
      _exptime = et*1.0e-6;
    }
  }
  return (ret==ASI_SUCCESS) ?  0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

- (int)setGain:(int)value
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,value);
#endif

  value = (int)fmin(600,fmax(0,value));

  @synchronized (self) {
    _gain = value;
    if (handle >= 0) {
      if (zwoServer) ret = [zwoServer setControl:ASI_GAIN value:_gain];
      else ret = ASISetControlValue(handle,ASI_GAIN,_gain,ASI_FALSE);
    }
  }
  return (ret==ASI_SUCCESS) ?  0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

- (int)setOffset:(int)value
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,value);
#endif

  value = (int)fmin(100,fmax(0,value));

  @synchronized (self) {
    _offset = value;
    if (handle >= 0) {
      if (zwoServer) ret = [zwoServer setControl:ASI_OFFSET value:_offset];
      else ret = ASISetControlValue(handle,ASI_OFFSET,_offset,ASI_FALSE);
    }
  }
  return (ret==ASI_SUCCESS) ?  0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

- (int)setBinning:(int)value
{
  int ret=ASI_SUCCESS,err=0;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d)\n",PREFUN,value);
#endif

  @synchronized (self) {
    _bin = value;
    if (handle >= 0) { // NOTE: just ignore by unsupported camera
      if (zwoServer) ret = [zwoServer setControl:ASI_HARDWARE_BIN value:1];
      else ret = ASISetControlValue(handle,ASI_HARDWARE_BIN,1,ASI_FALSE);
    }
    if (ret != ASI_SUCCESS) err = E_asi_error;
  }
  if (!err) err = [self setGeometry:winMode];

  return err;
}

/* ---------------------------------------------------------------- */

- (int)setBPP:(int)bits
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,bits);
#endif

  @synchronized(self) {
    _bpp = bits;
  }
  int err = [self setGeometry:winMode];

#if (DEBUG > 1)
  if (handle >= 0) { int x,y,w,h,b; ASI_IMG_TYPE t; char buf[128];
    if (zwoServer) {
      [zwoServer getROIFormatW:&w H:&h B:&b T:&t];
      [zwoServer getStartPosX:&x Y:&y];
    } else {
      ASIGetROIFormat(handle,&w,&h,&b,&t);
      ASIGetStartPos(handle,&x,&y);
    }
    sprintf(buf,"image format %d %d %d %d %d %d ",x,y,w,h,b,t);
    switch (t) {
      case ASI_IMG_RAW8:  strcat(buf,"raw8"); break;   // 0
      case ASI_IMG_RGB24: strcat(buf,"rgb24"); break;  // 1
      case ASI_IMG_RAW16: strcat(buf,"raw16"); break;  // 2
      case ASI_IMG_Y8:    strcat(buf,"y8"); break;     // 3
      default:            strcat(buf,"???"); break;
    }
    printf("%s\n",buf); [main_logger append:buf];
  }
#endif

  return err;
}

/* ---------------------------------------------------------------- */

- (int)setWindowX:(int)x Y:(int)y W:(int)w H:(int)h
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d,%d,%d,%d)\n",PREFUN,x,y,w,h);
#endif

  @synchronized(self) {
    winW = (wmax) ? imax(0,imin(wmax,w))   : w;
    winH = (hmax) ? imax(0,imin(hmax,h))   : h;
    winX = (wmax) ? imax(0,imin(wmax-w,x)) : x;
    winY = (hmax) ? imax(0,imin(hmax-h,y)) : y;
  }
  int err = [self setGeometry:TRUE];

  return err;
}

/* ---------------------------------------------------------------- */

// NOTE: window coordinates at the ZwoFinder GUI are in unbinned pixels
//       at the ASI level they are in binned pixels

- (int)setGeometry:(BOOL)wmode
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d)\n",PREFUN,wmode);
#endif
  
  @synchronized(self) {
    winMode = wmode;
    if (winMode) {
      _w = winW/_bin;  _h = winH/_bin;
    } else {
      _w = wmax/_bin;  _h = hmax/_bin;
    }
    while (_w % 8) { _w--; }
    while (_h % 2) { _h--; }
    if (handle >= 0) { ASI_IMG_TYPE t;
      if (_isColor) {
        t = (_bpp==8) ? ASI_IMG_Y8 : ASI_IMG_RGB24; // b0026
      } else {
        t = (_bpp==8) ? ASI_IMG_RAW8 : ASI_IMG_RAW16;
      }
      if (zwoServer) ret = [zwoServer setROIFormatW:_w H:_h B:_bin T:t];
      else  ret = ASISetROIFormat(handle,_w,_h,_bin,t);
      if (winMode) {                   // NOTE: -after- SetROI
        _startX = winX/_bin; _startY = winY/_bin;
        if (!ret) {
          if (zwoServer) ret = [zwoServer setStartPosX:_startX Y:_startY];
          else ret = ASISetStartPos(handle,_startX,_startY);
        }
      } else {
        if (zwoServer) {
          ret = [zwoServer getStartPosX:&_startX Y:&_startY];
        } else {
          ret = ASIGetStartPos(handle,&_startX,&_startY);
        }
      }
      if (ret == ASI_SUCCESS) {
        if (!zwoServer) { // b0035 (at zwoserver-0022)
          ret = ASIStartExposure(handle,ASI_FALSE); printf("ret=%d (start)\n",ret);
          ret = ASIStopExposure(handle); printf("ret=%d (stop)\n",ret);
        }
      }
    }
  }
  // printf("%s: ret=%d w=%d, h=%d, x=%d, y=%d\n",PREFUN,ret,_w,_h,_startX,_startY);
  
  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

- (int)setFlipX:(int)fx Y:(int)fy
{
  int ret=ASI_SUCCESS;
  
  @synchronized(self) {
    if (handle >= 0) { // long v; ASI_BOOL b;
      int value = fx+2*fy;
      if (zwoServer) ret = [zwoServer setControl:ASI_FLIP value:value];
      else ret = ASISetControlValue(handle,ASI_FLIP,value,ASI_FALSE);
      // ASIGetControlValue(handle,ASI_FLIP,&v,&b);
    }
  }
  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

- (int)setTemp:(float)temp cooler:(BOOL)cool
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%.1f,%d)\n",PREFUN,temp,cool);
#endif
  assert(!ret);

  if (!self.hasCooler) return 0; // b0022 
  
  @synchronized(self) {
    if (cool) _setpoint = temp;
    _cooler = cool;
#ifdef SIM_ONLY
    _temperature = (cool) ? temp : 25.0;
    _percent = (cool) ? 50-temp : 0;
#endif
    if (handle >= 0) { int value;
      value = (int)floor(0.5+temp);
      if (ret == ASI_SUCCESS) {
        if (zwoServer) ret = [zwoServer setControl:ASI_TARGET_TEMP value:value];
        else ret = ASISetControlValue(handle,ASI_TARGET_TEMP,value,ASI_FALSE);
      }
      value = (cool) ? 1 : 0;
      if (ret == ASI_SUCCESS) {
        if (zwoServer) ret = [zwoServer setControl:ASI_COOLER_ON value:value];
        else ret = ASISetControlValue(handle,ASI_COOLER_ON,value,ASI_FALSE);
      }
    }
  }
  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* --- */

- (int)updateTemp
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 2)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif
  assert(!ret);
  
  @synchronized(self) {
    if (handle >= 0) {
      if (zwoServer) { int temp,perc;  // network
        ret = [zwoServer getControl:ASI_TEMPERATURE value:&temp];
        if (!ret) _temperature = (float)temp/10.0f;
        if (self.hasCooler) {
          ret = [zwoServer getControl:ASI_COOLER_POWER_PERC value:&perc];
          if (!ret) _percent = (float)perc;
        }
      } else { long temp,perc; ASI_BOOL bAuto; // local USB
        ret = ASIGetControlValue(handle,ASI_TEMPERATURE,&temp,&bAuto);
        if (!ret) _temperature = (float)temp/10.0f;
        if (self.hasCooler) {
          ret = ASIGetControlValue(handle,ASI_COOLER_POWER_PERC,&perc,&bAuto);
          if (!ret) _percent = (float)perc;
#if (DEBUG > 2)
          long cool;
          ASIGetControlValue(handle,ASI_TARGET_TEMP,&temp,&bAuto);
          ASIGetControlValue(handle,ASI_COOLER_ON,&cool,&bAuto);
          printf("target=%ld, perc=%ld, cool=%ld\n",temp,perc,cool);
#endif
        }
      }
    }
  }
  return (ret==ASI_SUCCESS) ? 0 : E_asi_tempRead;
}

/* ---------------------------------------------------------------- */

- (int)setWhiteBalanceRed:(int)r blue:(int)b
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d)\n",PREFUN,r,b);
#endif
  assert(!ret);

  if (!self.isColor) return 0; // b0026
  
  _wbred = imax(1,imin(99,r));
  _wblue = imax(1,imin(99,b));
  @synchronized(self) {
    if (handle >= 0) {
      if (ret == ASI_SUCCESS) {
        if (zwoServer) ret = [zwoServer setControl:ASI_WB_R value:_wbred];
        else ret = ASISetControlValue(handle,ASI_WB_R,_wbred,ASI_FALSE);
      }
      if (ret == ASI_SUCCESS) {
        if (zwoServer) ret = [zwoServer setControl:ASI_WB_B value:_wblue];
        else ret = ASISetControlValue(handle,ASI_WB_B,_wblue,ASI_FALSE);
      }
    }
  }
  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

- (int)getSerial:(char*)string          // NEW b0043
{
  int ret=ASI_SUCCESS;
  
  *string = '\0';
  @synchronized(self) {
    if (handle >= 0) {
      if (zwoServer) {
        ret = [zwoServer getSerial:(char*)string];
      } else {  ASI_SN sn; char buf[32];
        ret = ASIGetSerialNumber(handle,&sn);
        if (ret == ASI_SUCCESS) {
          assert(sizeof(sn.idNumber) == 8);
          for (int i=0; i<8; i++) {
            sprintf(buf,"%02x",sn.idNumber[i]); strcat(string,buf);
          }
        }
      }
    }
  }
  return (ret==ASI_SUCCESS) ? 0 : E_asi_error;
}
  
/* ---------------------------------------------------------------- */
#pragma mark "Exposure"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (int)expose
{
  int err=E_asi_readout;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p: exptime=%f\n",PREFUN,self,_exptime);
#endif

  int npix = _w * _h;
  size_t size = npix * (_bpp / 8);
  imgData = (u_char*)realloc(imgData,size);

#ifdef SIM_ONLY
  NSDate *simDate = [NSDate date];
  if (!_isColor && (_bpp == 16)) {     // 16-bit mono
    u_short *p = (u_short*)imgData;
    u_short umax = (_bitDepth == 12) ? 0x0fff : 0xffff; // b0035
    double x,off=(double)_offset,sig=8.0+(double)_gain*100;
    for (int i=0; i<npix; i++) {
      x = off + sig*drand48();
      *p++ = (x > umax) ? umax : (u_short)x;
    }
    p = (u_short*)imgData;
    for (int y=0; y<20; y++) {         // mark (0,0) corner
      for (int x=0; x<20-y; x++) p[x+y*_w] = off;
    }
  } else
  if (_isColor && (_bpp == 24)) {      // 24-bit color b0030
    u_char *p=(u_char*)imgData,*q,h; int i=0,n1=npix/4,n2=npix/2,n3=3*npix/4;
    for (i=0; i<n1;   i++,p+=3) { p[0] = 0x00; p[1] = 0x00; p[2] = 0xff; } // B
    for (   ; i<n2;   i++,p+=3) { p[0] = 0x00; p[1] = 0xff; p[2] = 0x00; } // G
    for (   ; i<n3;   i++,p+=3) { p[0] = 0xff; p[1] = 0x00; p[2] = 0x00; } // R
    for (   ; i<npix; i++,p+=3) { p[0] = 0xff; p[1] = 0xff; p[2] = 0xff; }
    p = (u_char*)imgData; q=(u_char*)imgData+2;
    for (int i=0; i<npix; i++,p+=3,q+=3) { h = *p; *p = *q; *q = h; } // swap
  }
  [NSThread sleepForTimeInterval:(_exptime-YsecSince(simDate))];
  if (self) return 0;
#endif
  assert(handle >= 0);

  if (zwoServer) [zwoServer startExposure];
  else           ASIStartExposure(handle,ASI_FALSE);
    
  NSDate *date = [NSDate date];
  float timeout = 2.0+self.exptime;
  ASI_EXPOSURE_STATUS status = ASI_EXP_WORKING;
  do {
    double st = fmax(0.02,fmin(0.35,_exptime-YsecSince(date)));
    [NSThread sleepForTimeInterval:st];
    @synchronized(self) {
      // IDEA if (abort) ASIStopExposure(handle)
      if (zwoServer) {
        err = [zwoServer getExpStatus];
        if (!err) status = zwoServer.expStatus;
        if (status == ASI_EXP_WORKING) Ysleep(50);
      } else {
        err = ASIGetExpStatus(handle,&status);
        // printf("err=%d (%f) status=%d\n",err,YsecSince(date),status);
      }
    }
    if (status != ASI_EXP_WORKING) break;
  } while (YsecSince(date) < timeout); 
  
  if (status == ASI_EXP_SUCCESS) { int ret;
    @synchronized(self) {
      if (zwoServer) ret = [zwoServer getDataAfterExp:imgData size:size];
      else ret = ASIGetDataAfterExp(handle,imgData,size);
      // zwoserver/Raspberry: USB2 -- overhead ~ 1.4 seconds
      // zwoserver/Rock64:    USB3 -- overhead ~ 0.4 seconds
      // zwoserver/Minnow:    USB3 -- overhead ~ 0.4 seconds
      // zwoserver/NUC:       USB3 -- overhead ~ 0.4 seconds
      // it appears the driver downloads the image before I request it
    }
    if (ret == ASI_SUCCESS) {
      err = 0;
      if (_bpp == 16) {                // 16-bit data
        if (_bitDepth != 16) [self shiftImageData];
      } else
      if (_bpp == 24) {                // swap BGR<->RGB b0030
        u_char *p=(u_char*)imgData,*q=(u_char*)imgData+2,h;
        for (int i=0; i<npix; i++,p+=3,q+=3) { h = *p; *p = *q; *q = h; }
      }
    }
  } else {
    // NSDate *date = [NSDate date];
    ASIStopExposure(handle); 
    // printf("after STOP (%1f)\n",YsecSince(date)); // 32sec-->30, 12sec-->50
    err = E_asi_readout; 
  }

  return err;
}

/* --- */

- (u_short*)imageData
{
  return (u_short*)imgData;
}

/* --- */

- (void)shiftImageData                 // NEW b0041
{
  int shift=0,npix=_w*_h;

  if (strstr(modelName,"ASI294")) {    // variable shift NEW b0040
    assert(self.bitDepth == 12);
    if      (_bin == 1) shift = 4;     // 12-bit ADC
    else if (_bin == 2) shift = 2;     // 14-bit ADC
  } else
  if (_bitDepth == 12) {
    shift = 4;
  } else
  if (_bitDepth == 14) {
    shift = 2;
  }
  // printf("shift=%d\n",shift);
  u_short *p = (u_short*)imgData;      // shift pixels
  if (shift == 4) {
    for (int i=0; i<npix; i++,p++) { *p = (*p >> 4); } // assert((*p & 0x000f) == 0)
  } else
  if (shift == 2) {
    for (int i=0; i<npix; i++,p++) { *p = (*p >> 2); } // assert((*p & 0x0003) == 0)
  }
}

/* ---------------------------------------------------------------- */
#pragma mark "Video"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (int)startVideo
{
  int ret=ASI_SUCCESS;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p: exptime=%f\n",PREFUN,self,_exptime);
#endif
  // assert(_bpp == 8);

  size_t size = _w * _h * (_bpp/8);
  imgData = (u_char*)realloc(imgData,size);

  // NOTE no effect -- ret = ASISetControlValue(handle,ASI_HIGH_SPEED_MODE,1,ASI_FALSE);
  @synchronized(self) {
    if (handle >= 0) {
      if (zwoServer) ret = [zwoServer startVideoCapture];
      else ret = ASIStartVideoCapture(handle);
    }
  }

  assert(!_date);
  if (ret == ASI_SUCCESS) {
    self.date = [NSDate date];
  }
  
  return (ret == ASI_SUCCESS) ? 0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

- (int)getVideo:(float)timeout
{
  int ret=ASI_SUCCESS;

  do {
    @synchronized(self) {
#ifdef SIM_ONLY
      int npix=_w*_h;
      u_char *p = (u_char*)imgData;
      double x,off=(double)_offset,sig=8.0+(double)_gain*100.0/256.0;
      for (int i=0; i<npix; i++) {
        x = off + sig*drand48();
        *p++ = (x > 0xff) ? 0xff : (u_short)x; // IDEA 8/12/16 bits
      }
      for (int y=0; y<20; y++) { // make (0,0) corner
        for (int x=0; x<20-y; x++) imgData[x+y*_w] = off;
      }
      float dt = _exptime - YsecSince(_date);
      // if (dt > 0) printf("dtV=%f\n",dt);
      if (dt > 0) [NSThread sleepForTimeInterval:(dt/2.0)];
#else
      long size = _w * _h * (_bpp/8);
      if (zwoServer) ret = [zwoServer getVideoData:imgData size:size wait:350];
      else ret = ASIGetVideoData(handle,imgData,size,350);
      // zwoserver/Raspberry: [MB/s] 1x1=290  2x2=200 4x4=20
      // zwoserver/Rock64:    [MB/s] 1x1=320  2x2=200 4x4=16
      // zwoserver/Minnow:    [MB/s] 1x1=600  2x2=400 4x4=13
      // zwoserver/NUC:       [MB/s] 1x1=1200 2x2=600 4x4=40
      // it appears the driver downloads the image before I request it
#endif
    }
    if (ret == ASI_SUCCESS) break;
    float dt = _exptime - YsecSince(_date);
    // if (dt < 0) printf("ret=%d (%f)\n",ret,dt);
    [NSThread sleepForTimeInterval:((dt < 0) ? 0.35 : dt/2.0)];
  } while (YsecSince(_date) < timeout);
  
  if (ret == ASI_SUCCESS) {
#ifndef SIM_ONLY                          // NOT simulator
    if (_bpp == 16) {
      if (_bitDepth != 16) [self shiftImageData];
    } // endif(bits per  pixel == 16)
#endif
    double f = exp(-3.0*fmax(_exptime,YsecSince(_date)));
    if (_fps) _fps = f*_fps + (1.0-f)/YsecSince(_date);
    else      _fps = 1.0/YsecSince(_date);
    self.date = [NSDate date];
  }

#if (DEBUG > 2)
  if (handle >= 0) { int dropped=0;
    if (zwoServer) {
      ret = [zwoServer getDroppedFrames];
      if (!ret) dropped = zwoServer.droppedFrames;
    } else {
      ASIGetDroppedFrames(handle,&dropped);
    }
    printf("dropped frames= %d\n",dropped);
  }
#endif

  return (ret == ASI_SUCCESS) ?  0 : E_asi_readout;
}

/* ---------------------------------------------------------------- */

- (int)stopVideo
{
  int ret=ASI_SUCCESS;

#if (DEBUG > 0)
  if (handle >= 0) { int dropped=0; // Note: before stopping video capture
    if (zwoServer) {
      ret = [zwoServer getDroppedFrames];
      if (!ret) dropped = zwoServer.droppedFrames;
    } else {
      ASIGetDroppedFrames(handle,&dropped);
    }
    printf("dropped frames= %d\n",dropped);
  }
#endif

  @synchronized(self) {
    if (handle >= 0) {
      if (zwoServer) ret = [zwoServer stopVideoCapture];
      else ret = ASIStopVideoCapture(handle);
    }
  }
  self.date = nil;
  
  return (ret == ASI_SUCCESS) ?  0 : E_asi_error;
}

/* ---------------------------------------------------------------- */

- (u_char*)videoData
{
  return (u_char*)imgData;
}

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
