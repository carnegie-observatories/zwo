/* ---------------------------------------------------------------- */
//
// AppDelegate.m
// 
// Created by Christoph Birk on 2018-09-04
// Copyright (c) 2018 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import "AppDelegate.h"
#import "Notice.h"
#import "Yutils.h"
#import "FITS.h"
#import "main.h"
#import "ASI.h"
#import "EFW.h"
#import "ZWO.h"

#include "ASICamera2.h"

/* ---------------------------------------------------------------- */

#define DEBUG           1

#define DBE_FLIP_X      "dbe_flipX"
#define DBE_FLIP_Y      "dbe_flipY"
#define DBE_FITSPATH    "dbe_fitspath"
#define DBE_RUN         "dbe_fileNumber"
#define DBE_AUTOSTART   "dbe_autostart"
#define DBE_ZWOHOST     "dbe_zwohost"
#define DBE_WINDOW      "dbe_window"
#define DBE_WINMODE     "dbe_useWindow"
#define DBE_GAIN        "dbe_gain"
#define DBE_OFFSET      "dbe_offset"
#define DBE_SETPOINT    "dbe_setpoint"
#define DBE_WBRED       "dbe_whiteBalance_red"
#define DBE_WBLUE       "dbe_whiteBalance_blue"

#define DEF_PREFIX      "zf"            // ?Povilas

#define UNIT_TEST       0               // 1:_test_image, 2:_test_video

/* ---------------------------------------------------------------- */

Logger *main_logger=nil;
BOOL   use_shared=NO;
char   *p_exec=NULL,*p_build=NULL,*p_version=NULL,*s_path=NULL;

/* ---------------------------------------------------------------- */

static char* get_ut_timestr(char* string,time_t ut)
{
  struct tm *now,res;

  if (ut == 0) ut = time(NULL);
  now = gmtime_r(&ut,&res);
  strftime(string,16,"%H:%M:%S",now);  // min.length of 'string'
  return string;
}

/* ---------------------------------------------------------------- */

@implementation AppDelegate
{
  ASI  *ccd;
  EFW  *filterWheel;
  ZWO  *zwoServer;
  NSArray *filterNames;
  volatile int  flipX,flipY;
  volatile BOOL writeFits;
  BOOL          windowMode;
  int           runNumber,videoBits;
  time_t        temp_last,temp_interval;
  u_short       scale_min,scale_max;
}


/* ---------------------------------------------------------------- */
#pragma mark "Application Delegate"
/* ---------------------------------------------------------------- */
//345678901234567890123456789012345678900

- (void)applicationDidFinishLaunching:(NSNotification*)aNotification
{
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,aNotification);
#endif

#if 0 // TESTING
  printf("ASI-SDK-Version= %s\n",ASIGetSDKVersion());
  int num = ASIGetNumOfConnectedCameras();
  printf("num=%d\n",num);
  assert(num == 1);
  ASI_CAMERA_INFO info;
  int ret = ASIGetCameraProperty(&info,0);
  assert(ret == 0);
  int handle = info.CameraID;
  long wmax=info.MaxWidth,hmax=info.MaxHeight;
  printf("name=%s\n",info.Name);
  printf("handle=%d (%ld,%ld)\n",handle,wmax,hmax);
  printf("color=%d, cooler=%d, pixel=%f, bit=%d\n",
    info.IsColorCam,info.IsCoolerCam,info.PixelSize,info.BitDepth);
  ret = ASIOpenCamera(handle); assert(ret == ASI_SUCCESS);
  ret = ASIInitCamera(handle);  assert(ret == ASI_SUCCESS);
#if 0
  ASI_CONTROL_CAPS caps; int numOfCtrl = 0;
  ASIGetNumOfControls(handle, &numOfCtrl);
  for (int i=0; i<numOfCtrl; i++) {
    ASIGetControlCaps(handle,i,&caps);
    printf("%24s %5ld %11ld %6ld %d %d %s\n",caps.Name,
           caps.MinValue,caps.MaxValue,caps.DefaultValue,
           caps.IsAutoSupported,caps.IsWritable,caps.Description);
  }
#endif
  int w,h,b,x=0,y=0; ASI_IMG_TYPE t; long size;
  ret = ASIGetROIFormat(handle,&w,&h,&b,&t); assert(ret == ASI_SUCCESS);
  ret = ASIGetStartPos(handle,&x,&y); assert(ret == ASI_SUCCESS);
  sprintf(buf,"image format %d %d %d %d %d %d ",x,y,w,h,b,t);
  switch (t) {
    case ASI_IMG_RAW8:  strcat(buf,"raw8"); break;   // 0
    case ASI_IMG_RGB24: strcat(buf,"rgb24"); break;  // 1
    case ASI_IMG_RAW16: strcat(buf,"raw16"); break;  // 2
    case ASI_IMG_Y8:    strcat(buf,"y8"); break;     // 3
    default:            strcat(buf,"???"); break;
  }
  printf("%s\n",buf);
#if 1 // set binning
  b=4; w = (int)wmax/b; h = (int)hmax/b;
  while (w % 8) { w--; }
  while (h % 2) { h--; }
  ret = ASISetROIFormat(handle,w,h,b,t); assert(ret == ASI_SUCCESS);
#endif
  size = w*h;
#if 1
  t=ASI_IMG_RAW16; size *= 2;
  ret = ASISetROIFormat(handle,w,h,b,t); assert(ret == ASI_SUCCESS);
#endif
#if 1
  ret = ASIGetROIFormat(handle,&w,&h,&b,&t); assert(ret == ASI_SUCCESS);
  ret = ASIGetStartPos(handle,&x,&y); assert(ret == ASI_SUCCESS);
  sprintf(buf,"image format %d %d %d %d %d %d ",x,y,w,h,b,t);
  switch (t) {
    case ASI_IMG_RAW8:  strcat(buf,"raw8"); break;   // 0
    case ASI_IMG_RGB24: strcat(buf,"rgb24"); break;  // 1
    case ASI_IMG_RAW16: strcat(buf,"raw16"); break;  // 2
    case ASI_IMG_Y8:    strcat(buf,"y8"); break;     // 3
    default:            strcat(buf,"???"); break;
  }
  printf("%s\n",buf);
#if 0
  ret = ASIGetCameraProperty(&info,0);
  assert(ret == 0);
  printf("name=%s\n",info.Name);
  printf("handle=%d (%ld,%ld)\n",handle,info.MaxWidth,info.MaxHeight);
  printf("bit=%d\n",info.BitDepth);
#endif
#endif
//  ret = ASISetControlValue(handle,ASI_GAIN,300,ASI_FALSE); printf("ret=%d (gain)\n",ret);
  ret = ASIStartExposure(handle,ASI_FALSE); printf("ret=%d (start)\n",ret);
  ret = ASIStopExposure(handle); printf("ret=%d (stop)\n",ret);
//  ret = ASISetControlValue(handle,ASI_TARGET_TEMP,0,ASI_FALSE); printf("ret=%d (target)\n",ret);
//  ret = ASISetControlValue(handle,ASI_COOLER_ON,1,ASI_FALSE); printf("ret=%d (cooler)\n",ret);
#if 0 // exposure
  dispatch_async(GLOBAL_QUEUE,^(void) { int ret,et=100000;
    ret = ASISetControlValue(handle,ASI_EXPOSURE,et,ASI_FALSE);
#if 0 // wait for cooler
    for (int i=0; i<300; i++) { ASI_BOOL bAuto; long temp;
      Ysleep(2000);
      ret = ASIGetControlValue(handle,ASI_TEMPERATURE,&temp,&bAuto);
      printf("ret=%d, temp=%ld (%d)\n",ret,temp,i);
      if (temp < 5) break;
    }
#endif
    ASI_EXPOSURE_STATUS status = ASI_EXP_WORKING;
    NSDate *date = [NSDate date];
    u_char *imgData = (u_char*)malloc(size);
    for (int j=0; j<1; j++) {
      ret = ASIStartExposure(handle,ASI_FALSE); printf("ret=%d (start)\n",ret);
      do {
        Ysleep(350);
        ret = ASIGetExpStatus(handle,&status);
        printf("ret=%d (%f) status=%d\n",ret,YsecSince(date),status);
        if (status != ASI_EXP_WORKING) break;
      } while (YsecSince(date) < 15.0);
      if (status == ASI_EXP_SUCCESS) { int bitDepth=16;
        ret = ASIGetDataAfterExp(handle,imgData,size);
        printf("ret(getData)=%d\n",ret);
        int npix=w*h; double s1=0,s2=0,sn=0,v,vmin=0xffff,vmax=0;
        //u_char *p=(u_char*)imgData; // 8-bit
        u_short *p=(u_short*)imgData; // 16-bit
        assert(info.BitDepth != 16);
        if (strstr(info.Name,"ASI294")) {
          if      (b == 1) bitDepth = 12;
          else if (b == 2) bitDepth = 14;
        } else
        if (strstr(info.Name,"ASI1600")) {
          bitDepth = 12;
        }
        printf("bit=%d\n",bitDepth);
        for (int i=0; i<npix; i++,p++) {
          if (((i % 2220) == 0) || (*p & 0x000f)) printf("%04x ",*p);
          if (bitDepth == 12) {
            assert((*p & 0x000f) == 0);
            v = (double)(*p >> 4);
          } else
          if (bitDepth == 14) {
            assert((*p & 0x0003) == 0);
            v = (double)(*p >> 2);
          } else {
            assert(bitDepth == 16);
            v = (double)(*p);
          }
          s1 += v; s2 += v*v; sn += 1;
          if (v > vmax) vmax = v;
          if (v < vmin) vmin = v;
        }
        double ave = s1/sn;
        double sig = sqrt(s2/sn-ave*ave);
        printf("\n%.1f %.2f (%.0f,%.0f) %.0f\n",ave,sig,vmin,vmax,sn);
      } else {
        if (YsecSince(date) > 60) { // stop
          date = [NSDate date];
          ret = ASIStopExposure(handle); printf("ret=%d (stop) %.1f\n",ret,YsecSince(date));
          break;
        }
      }
    }
  });
#endif
#if 0 // video
  ret = ASIStartVideoCapture(handle); assert(ret == ASI_SUCCESS);
  dispatch_async(GLOBAL_QUEUE,^(void) { int ret;
    NSDate *date = [NSDate date];
    u_char *imgData = (u_char*)malloc(size);
    do {
      ret = ASIGetVideoData(handle,imgData,size,350);
      printf("ret=%d (%.4f)\n",ret,YsecSince(date));
      if (!ret) { double s1=0,s2=0;
        if (t == ASI_IMG_RAW8) {
          u_char *p = (u_char*)imgData;
          for (int i=0; i<w*h; i++) { s1 += p[i]; s2 += (double)p[i] * (double)p[i]; }
        } else {
          u_short *p = (u_short*)imgData;
          for (int i=0; i<w*h; i++) { s1 += p[i]; s2 += (double)p[i] * (double)p[i]; }
        }
        double ave = s1/(w*h);
        double sig = sqrt(s2/(w*h) - ave*ave);
        printf("%.1f +- %.2f\n",ave,sig);
      } else
      if (ret == ASI_ERROR_TIMEOUT) {
        if (YsecSince(date) > 2.0) {
          ret = ASIStopVideoCapture(handle); printf("ret=%d (stop)\n",ret);
          ret = ASIStartVideoCapture(handle);  printf("ret=%d (start)\n",ret);
          date = [NSDate date];
        }
      }
    } while (YsecSince(date) < 5.0);
    ret = ASIStopVideoCapture(handle);
    printf("ret=%d\n",ret);
    free(imgData);
  });
#endif
  // sleep(2);
  // exit(0);
#endif

  YbundleInfo(&p_exec,&p_build,&p_version,NULL,NULL,NULL);

  if (!YappNapDisabled()) {
    printf("Warning: AppNap enabled\rPlease restart the GUI");
  }
  YappSupportCreate(&s_path,use_shared);

  main_logger = [[Logger alloc] initWithBase:nil useUT:YES folder:NULL];
  main_logger.mode = LOGGER_FLUSH;
  NSString *s = [NSString stringWithFormat:@"user= %@ , sharedSetup= %d , UT= %s",
                 NSUserName(),use_shared,get_ut_timestr(buf,0)];
  [main_logger append:[s UTF8String]];
  [main_logger setAutoChange:YES];
  
  sprintf(buf,"ASI-SDK-Version= %s",ASIGetSDKVersion());
  [main_logger append:buf];

#ifdef NDEBUG
// ?Povilas [edit_scale setHidden:YES];
#endif

  flipX = YprefGetInt(DBE_FLIP_X,0,NO);
    [chk_flipx setState:((flipX) ? NSControlStateValueOn : NSControlStateValueOff)];
  flipY = YprefGetInt(DBE_FLIP_Y,0,NO);
    [chk_flipy setState:((flipY) ? NSControlStateValueOn : NSControlStateValueOff)];
  
  runNumber = YprefGetInt(DBE_RUN,1,NO);
  sprintf(buf,"%d",runNumber); YeditSet(edit_run,buf);
  
  [edit_window setStringValue:YprefGetString(@DBE_WINDOW,@"",NO)]; // b0021
  windowMode = (YprefGetInt(DBE_WINMODE,0,NO)) ? YES : NO; // b0025
    [chk_window setState:((windowMode) ? NSControlStateValueOn : NSControlStateValueOff)];
  
  self.fitsPath = YprefGetString(@DBE_FITSPATH,NSHomeDirectory(),NO);
  if (!YpathWritable([_fitsPath UTF8String])) self.fitsPath = NSHomeDirectory();

  [edit_zwohost setStringValue:YprefGetString(@DBE_ZWOHOST,@"",NO)];

  videoBits = [[pop_vbits titleOfSelectedItem] intValue]; // b0022

  if (YprefGetInt(DBE_AUTOSTART,0,NO)) {
    [self action_startup:nil];
  }
  
  [configWindow setDefaultButtonCell:[but_startup cell]];
}

/* ---------------------------------------------------------------- */

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)appl
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,appl);
#endif
  return YES;
}

/* ---------------------------------------------------------------- */

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)appl
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,appl);
#endif

  if (self.running) {
    self.stop = TRUE;
    dispatch_async(GLOBAL_QUEUE,^(void) {
      while (self.running) { Ysleep(350); }
      [NSApp replyToApplicationShouldTerminate:YES];
    });
    return NSTerminateLater;
  }

  return NSTerminateNow;
}

/* ---------------------------------------------------------------- */

- (void)applicationWillTerminate:(NSNotification*)note
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,note);
#endif

  [ccd close];
  if (zwoServer) [zwoServer close];
}

/* ---------------------------------------------------------------- */
#pragma mark "Instance"
/* ---------------------------------------------------------------- */
//345678901234567890123456789012345678900

- (void)dealloc
{
 [_lastImage release];
 [_fitsPath release];
 [super dealloc];
}

/* ---------------------------------------------------------------- */

- (void)fireTimer:(NSTimer*)theTimer
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p): running=%d\n",PREFUN,theTimer,self.running);
#endif
  assert([NSThread isMainThread]);

  if (self.running) return;             // check if exposure is running

    if ([chk_loop state] == NSControlStateValueOn) {  // auto looping ?
    if (YsecSince(_lastImage) >= [edit_loop doubleValue]) {
      [self action_button:but_image];
    }
  }

  if (time(NULL) >= temp_last+temp_interval) {
    dispatch_async(GLOBAL_QUEUE,^(void) { char buf[32];
      [ccd updateTemp];
      sprintf(buf,"%.1f",ccd.temperature); YeditSet(edit_temp,buf);
      if (ccd.hasCooler) {              // b0022
        sprintf(buf,"%.0f",ccd.percent); YeditSet(edit_cool,buf);
      }
    });
    temp_last = time(NULL);
    temp_interval += 1; if (temp_interval > 60) temp_interval = 60;
  }
}

/* ---------------------------------------------------------------- */

- (NSImage*)flip_x:(NSImage*)inputImage
{
  NSSize size = [inputImage size];
  NSImage *tmpImage = [[NSImage alloc] initWithSize:size];
  NSAffineTransformStruct flip = {-1.0,0.0,0.0,1.0,size.width,0.0};
  NSAffineTransform *transform = [NSAffineTransform transform];
  [tmpImage lockFocus];
  [transform setTransformStruct:flip];
  [transform concat];
  [inputImage drawAtPoint:NSMakePoint(0,0) fromRect:NSMakeRect(0,0,size.width,size.height) operation:NSCompositingOperationCopy fraction:1.0];
  [tmpImage unlockFocus];
  [view_image setImage:tmpImage];
  return [tmpImage autorelease];
}

- (NSImage*)flip_y:(NSImage*)inputImage
{
  NSSize size = [inputImage size];
  NSImage *tmpImage = [[NSImage alloc] initWithSize:size];
  NSAffineTransformStruct flip = {1.0,0.0,0.0,-1.0,0.0,size.height};
  NSAffineTransform *transform = [NSAffineTransform transform];
  [tmpImage lockFocus];
  [transform setTransformStruct:flip];
  [transform concat];
  [inputImage drawAtPoint:NSMakePoint(0,0) fromRect:NSMakeRect(0,0,size.width,size.height) operation:NSCompositingOperationCopy fraction:1.0];
  [tmpImage unlockFocus];
  [view_image setImage:tmpImage];
  return [tmpImage autorelease];
}

/* ---------------------------------------------------------------- */

- (NSBitmapImageRep*)update:(void*)data W:(int)w H:(int)h B:(int)bpp
{
  NSString *s;
  assert([NSThread isMainThread]);  // NSView is mainThread only

  int bps = (bpp==24) ? 8 : bpp;    // color b0026
  int spp = (bpp==24) ? 3 : 1;
  NSColorSpaceName colorSpace = (bpp==24) ? NSDeviceRGBColorSpace : NSDeviceWhiteColorSpace;
  NSBitmapImageRep *bitmap = [NSBitmapImageRep alloc];
  bitmap = [bitmap initWithBitmapDataPlanes:NULL
                   pixelsWide:w
                   pixelsHigh:h
                   bitsPerSample:bps
                   samplesPerPixel:spp
                   hasAlpha:NO
                   isPlanar:NO
                   colorSpaceName:colorSpace
                   bytesPerRow:(w*bpp)/8
                   bitsPerPixel:bpp];
  memcpy([bitmap bitmapData],data,(w*h*bpp)/8);
  NSImage *image = [[NSImage alloc] init];
  [image addRepresentation:bitmap];

  [view_image setImage:image];
  if (ccd.Simulator) { // done in hardware
    if (flipX) [view_image setImage:[self flip_x:[view_image image]]];
    if (flipY) [view_image setImage:[self flip_y:[view_image image]]];
  }
  [image release];
  
  if (ccd.bpp == 24) s =  @"n/a";
  else s = [NSString stringWithFormat:@"%d %d",scale_min,scale_max];
  [edit_scale setStringValue:s];

  return [bitmap autorelease];
}

/* ---------------------------------------------------------------- */

- (void)showWarning:(const char*)text error:(int)err
{
  NSString *s = [NSString stringWithFormat:@"%s, err=%d\r%@",
                          text,err,[ASI errorString:err]];
  [main_logger message:[s UTF8String] level:1 time:30 file:YES];
}

/* ---------------------------------------------------------------- */

- (void)scale8:(u_char*)data count:(u_int)npix  // b0012
{
  double s1=0,s2=0;
  u_char min=0xff,max=0;
  
  u_char *p=data;
  for (u_int i=0; i<npix; i++,p++) {
    if (*p < min) min = *p;
    if (*p > max) max = *p;
    s1 += (double)*p; s2 += (double)*p * (double)*p;
  }
  double ave = s1/npix;
  double sig = sqrt(s2/npix - ave*ave);
  if (ave-3*sig > min) min = ave-3*sig;
  if (ave+5*sig < max) max = ave+5*sig;
  int dyn = max-min; if (dyn == 0) dyn = 1;
  p = data;
  float scale = 255.0f / (float)dyn;
  for (u_int i=0; i<npix; i++,p++) {
    if      (*p < min) *p = 0;
    else if (*p > max) *p = 0xff;
    else               *p = (u_char)((*p-min) * scale);
  }
  scale_min = min; scale_max = max;
}

/* --- */

- (void)scale16:(u_short*)data count:(u_int)npix
{
  double s1=0,s2=0;
  u_short min=0xffff,max=0;

  u_short *p = data;
  for (u_int i=0; i<npix; i++,p++) {
    if (*p < min) min = *p;
    if (*p > max) max = *p;
    s1 += (double)*p; s2 += (double)*p * (double)*p;
  }
  // printf("min=%d, max=%d\n",min,max);
  double ave = s1/npix;
  double sig = sqrt(s2/npix - ave*ave);
  if (ave-3*sig > min) min = ave-3*sig;
  if (ave+5*sig < max) max = ave+5*sig;
  int dyn = max-min; if (dyn == 0) dyn = 1;
  p = data;
  float scale = (float)0xffff / (float)dyn;
  for (u_int i=0; i<npix; i++,p++) {
    if      (*p < min) *p = 0;
    else if (*p > max) *p = 0xffff;
    else               *p = (u_short)((*p-min) * scale);
  }
  scale_min = min; scale_max = max;
}

/* ---------------------------------------------------------------- */

- (void)writeFitsFile
{
  char file[128];
  // IDEA get telescope, ID, etc from CDIMM -- DimmMonitor

  FITS *fits = [[FITS alloc] initWithBase:DEF_PREFIX width:ccd.w height:ccd.h];
  if (ccd.bitDepth == 12) fits.bzero = 0; // b0038

  sprintf(file,"%s%04d.fits",DEF_PREFIX,runNumber);
  if (![fits open:file paths:@[_fitsPath]]) { NSString *text;
    text = [NSString stringWithFormat:@"failed to open %@",fits.failedFile];
    [Notice openNotice:[text UTF8String] title:P_TITLE time:60];
  } else { struct tm res; int x1,x2,y1,y2; char buf[128]; NSString *str;
    time_t ut = time(NULL);
    gmtime_r(&ut,&res);
    strftime(buf,sizeof(buf),"%Y-%m-%d",&res);
    [fits charValue:buf forKey:"UT-DATE" comment:"UT date"];
    strftime(buf,sizeof(buf),"%H:%M:%S",&res);
    [fits charValue:buf forKey:"UT-TIME" comment:"UT time"];
    [fits floatValue:ccd.exptime forKey:"EXPTIME" prec:6 comment:"[seconds]"];
    [fits intValue:ccd.bin forKey:"BINNING" comment:NULL]; // b0012
    x1 = 1+ccd.startX; x2 = x1+ccd.w-1;
    y1 = 1+ccd.startY; y2 = y1+ccd.h-1;
    sprintf(buf,"[%d:%d,%d:%d]",x1,x2,y1,y2);
    [fits charValue:buf forKey:"WINDOW" comment:NULL]; // b0012
    [fits intValue:ccd.gain forKey:"CCD-GAIN" comment:"0..600"];
    [fits intValue:ccd.offset forKey:"CCD-OFFS" comment:"0..100"];
    [fits floatValue:ccd.temperature forKey:"CCD-TEMP" prec:1 comment:"CCD temperature [C]"];
    [fits intValue:ccd.percent forKey:"CCD-COOL" comment:"CCD cooler [percent]"];
    if (filterNames) {                  // b0023
      str = filterNames[filterWheel.curPos];
      [fits charValue:[str UTF8String] forKey:"FILTER" comment:"filter name"];
    }
    dispatch_sync(MAIN_QUEUE,^(void) { NSString *str; // b0024
      str = [edit_object stringValue];
      [fits charValue:[str UTF8String] forKey:"OBJECT" comment:NULL];
      str = [edit_comment stringValue];
      [fits charValue:[str UTF8String] forKey:"COMMENT" comment:NULL];
    });
    sprintf(buf,"Version %s (%s)",p_version,p_build);
    [fits charValue:buf forKey:"SOFTWARE" comment:NULL];
    [fits endHeader];
    [fits writeData:ccd.imageData];
    [fits close:1];
    [self updateRunNumber:1];
    [main_logger append:[fits.linktarg UTF8String]];
  }
  [fits release];
}

/* ---------------------------------------------------------------- */

// todo window geometry with new ratio (9576,6388) -- depends on ASI1600,6200,294

- (int)openCCD:(int)high                // b0022
{
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)",PREFUN,high);
#endif
  assert(ccd);

  int err = [ccd open]; if (err) return err;

  dispatch_async(MAIN_QUEUE,^(void) {
    [edit_cool setHidden:!ccd.hasCooler];
    [pop_vbits removeItemAtIndex:1];
    [pop_vbits addItemWithTitle:((ccd.bitDepth==12) ? @"12" : @"16")];
  });

  int bits = (!high) ? 8 : (ccd.isColor ? 24 : 16);
  if (!err) err = [ccd setBPP:bits];
  if (!err) err = [ccd setFlipX:flipX Y:flipY];
  // called by setBPP if (!err) err = [ccd setGeometry:windowMode];
  if (!err) err = [ccd getSerial:buf]; // NEW b0043
  printf("ser=%s\n",buf); //xxx
  
  return err;
}

/* ---------------------------------------------------------------- */

- (void)updateWindowGeometry:(BOOL)expose
{
  int x=0,y=0,w=ccd.w,h=ccd.h;
#if (DEBUG > 0) 
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif
  assert([NSThread isMainThread]);
  
  if (self.running) {
    [Notice openNotice:"cannot update window geometry while taking data"
            title:P_TITLE];
    return;
  }

  YprefPutString(@DBE_WINDOW,[edit_window stringValue],NO); // b0024
    windowMode = ([chk_window state] == NSControlStateValueOn) ? YES : NO;
  YprefPutInt(DBE_WINMODE,windowMode,NO); // b0025
  if (windowMode) {
    const char *text = [[edit_window stringValue] UTF8String];
    if (strchr(text,':')) { int x1,x2,y1,y2;
      if (sscanf(text,"%d:%d %d:%d",&x1,&x2,&y1,&y2) == 4) {
        x = x1-1; w = x2-x1+1;                // {1..N}
        y = y1-1; h = y2-y1+1;
      }
    } else {
      sscanf(text,"%d %d %d %d",&x,&y,&w,&h); // {0..N-1}
    }
  }
  dispatch_async(GLOBAL_QUEUE,^(void) { int err;
    if (windowMode) err = [ccd setWindowX:x Y:y W:w H:h];
    else            err = [ccd setGeometry:NO];
    if (err) [self showWarning:"setting window geometry failed" error:err];
    if (expose) [self action_button:but_image];
  });
}

/* ---------------------------------------------------------------- */

- (void)updateWhiteBalance:(BOOL)expose
{
  assert([NSThread isMainThread]);

  if (!ccd.isColor) return;
  
  int r = [edit_wbred intValue];
  YprefPutInt(DBE_WBRED,r,use_shared);
  int b = [edit_wblue intValue];
  YprefPutInt(DBE_WBLUE,b,use_shared);

  dispatch_async(GLOBAL_QUEUE,^(void) {
    int err = [ccd setWhiteBalanceRed:r blue:b];
    if (err) [self showWarning:"setting WhiteBalance failed" error:err];
    if (expose) [self action_button:but_image];
  });
}

/* ---------------------------------------------------------------- */

- (void)updateRunNumber:(int)inc
{
  char buf[128];
  
  runNumber += inc; if (runNumber > 9999) runNumber = 1;
  YprefPutInt(DBE_RUN,runNumber,NO);
  sprintf(buf,"%d",runNumber); YeditSet(edit_run,buf);
}

/* ---------------------------------------------------------------- */
#pragma mark "Actions"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (IBAction)action_startup:(id)sender
{
  [window orderFront:self];
  
  NSString *host = [edit_zwohost stringValue];
  YprefPutString(@DBE_ZWOHOST,host,NO);
  if ([host compare:@""] != NSOrderedSame) { // has host name
    if ([host compare:@"rock"] == NSOrderedSame) host = @"10.7.1.182"; 
    if ([host compare:@"rpi"] == NSOrderedSame) host = @"10.7.1.69";
    int p = 50011 + PROJECT_ID*100;
    zwoServer = [[ZWO alloc] initWithHost:[host UTF8String] port:p online:1];
  }
  if (zwoServer) {
    int err = [zwoServer open];
    if (!err) {
#if (DEBUG > 1)
      printf("numASI=%d %lu\n",[zwoServer getNumASI],[[ASI scan:zwoServer] count]);
      printf("numEFW=%d %lu\n",[zwoServer getNumEFW],[[EFW scan:zwoServer] count]);
#endif
      // [zwoServer close]; // done at application closing
    } else {
      YnotifyNotice(P_TITLE,"failed to connect to zwo-server",0,2);
      YprefPutInt(DBE_AUTOSTART,0,NO); // b0019
      [NSApp terminate:self];
    }
  } else {
#if (DEBUG > 1)
    printf("numASI=%lu\n",[[ASI scan:zwoServer] count]);
    printf("numEFW=%lu\n",[[EFW scan:zwoServer] count]);
#endif
  }
  NSArray *a = [ASI scan:zwoServer];   // b0028
  if ([a count] == 0) {
    YnotifyNotice(P_TITLE,"no device found",0,2);
    YprefPutInt(DBE_AUTOSTART,0,NO);   // b0019
    [NSApp terminate:self];
  }
#if (DEBUG > 0)
  for (NSDictionary *d in a) {
    printf("%s %d %d %d %d\n",[[d objectForKey:@"Name"] UTF8String],
                              [[d objectForKey:@"MaxWidth"] intValue],
                              [[d objectForKey:@"MaxHeight"] intValue],
                              [[d objectForKey:@"IsCoolerCam"] intValue],
                              [[d objectForKey:@"IsColorCam"] intValue]);
  }
#endif

  ccd = [[ASI alloc] initWithServer:zwoServer];
  assert(ccd);
  
  ccd.setpoint = YprefGetDouble(DBE_SETPOINT,-5,use_shared);
  [edit_setpoint setFloatValue:ccd.setpoint];
  ccd.wbred = YprefGetInt(DBE_WBRED,52,use_shared);
  [edit_wbred setIntValue:ccd.wbred];
  ccd.wblue = YprefGetInt(DBE_WBLUE,95,use_shared);
  [edit_wblue setIntValue:ccd.wblue];
  
  a = [EFW scan:zwoServer];            // filter
  [pop_filter removeAllItems];         // on main GUI window b0024
  if (a.count >= 1) {
    filterWheel = [[EFW alloc] initWithServer:zwoServer];
    NSDictionary *d = a[0];
    int ident = [[d objectForKey:@"ID"] intValue];
    int err = [filterWheel setup:ident];
    if (!err) {
      NSString *trg = [NSString stringWithFormat:@"%s/filters.xml",s_path];
      filterNames = YplistWithContentsOfFile(trg);
      if (!filterNames) {              // copy default names
        const char *src = YbundleResource("filters","xml");
        YfileCopy(src,[trg UTF8String]);
        filterNames = YplistWithContentsOfFile(trg);
      }
      assert(filterNames);
      [filterNames retain];
      for (int i=0; i<filterWheel.numPos; i++) {
        [pop_filter addItemWithTitle:filterNames[i]];
      }
      [pop_filter selectItemAtIndex:filterWheel.curPos];
    }
  } else {
    [pop_filter setEnabled:NO];
    [but_calibrate setEnabled:NO];
  }
  
  [edit_exptime setFloatValue:0.125]; // b0024 
  [self action_edit:edit_exptime];
  
  [edit_gain setIntValue:YprefGetInt(DBE_GAIN,0,NO)]; // b0024
  [self action_edit:edit_gain];

  [edit_offset setIntValue:YprefGetInt(DBE_OFFSET,10,NO)]; // b0024
  [self action_edit:edit_offset];

  [self action_binning:pop_binning];
  
  [self updateWindowGeometry:YES];      // takes exposure

  assert([NSThread isMainThread]);     // timer on main thread
  [NSTimer scheduledTimerWithTimeInterval:1.0 target:self
           selector:@selector(fireTimer:) userInfo:nil repeats:YES];
  
  [configWindow orderOut:self];        // hide Startup panel
}

/* ---------------------------------------------------------------- */

- (IBAction)action_button:(id)sender
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p): MT=%d\n",PREFUN,sender,[NSThread isMainThread]);
#endif
  assert((sender==but_image) || (sender==but_video));
  
  if (![NSThread isMainThread]) {      // enforce mainThread
    [self performSelectorOnMainThread:_cmd withObject:sender waitUntilDone:YES];
    return;
  }
  assert([NSThread isMainThread]);

  if (self.running) {
    self.stop = TRUE;
    [chk_loop setEnabled:YES];
    return;
  }

  if ([[sender title] compare:@"Video"] == NSOrderedSame) {
    [chk_loop setEnabled:NO];
#if   (UNIT_TEST == 1)
    [self performSelectorInBackground:@selector(run_test_image:) withObject:nil];
#elif (UNIT_TEST == 2)
    [self performSelectorInBackground:@selector(run_test_video:) withObject:nil];
#else
    [self performSelectorInBackground:@selector(run_video:) withObject:nil];
#endif
  } else {
    [self performSelectorInBackground:@selector(run_image:) withObject:nil];
  }
}

/* ---------------------------------------------------------------- */

- (IBAction)action_check:(id)sender
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,sender);
#endif
  assert([NSThread isMainThread]);

  if (sender == chk_reticle) {
      view_image.reticleMode = ([chk_reticle state] == NSControlStateValueOn) ? YES : NO;
    view_image.needsDisplay = YES;  // b0042
  } else
  if (sender == chk_flipx) {
      flipX = ([sender state] == NSControlStateValueOn) ? 1 : 0;
    YprefPutInt(DBE_FLIP_X,flipX,NO);
    [ccd setFlipX:flipX Y:flipY];
    [view_image setImage:[self flip_x:[view_image image]]];
  } else
  if (sender == chk_flipy) {
      flipY = ([sender state] == NSControlStateValueOn) ? 1 : 0;
    YprefPutInt(DBE_FLIP_Y,flipY,NO);
    [ccd setFlipX:flipX Y:flipY];
    [view_image setImage:[self flip_y:[view_image image]]];
  } else
  if (sender == chk_fits) {
      writeFits = ([sender state] == NSControlStateValueOn) ? YES : NO;
  } else {
    assert(0);
  }
}

/* ---------------------------------------------------------------- */

- (IBAction)action_edit:(id)sender
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,sender);
#endif
  
  if (sender == edit_exptime) {
    int err = [ccd setExpTime:[sender floatValue]];
    if (err) { char buf[128];
      sprintf(buf,"setting exposure time, err=%d",err);
      [Notice openNotice:buf title:P_TITLE];
    }
  } else
  if (sender == edit_offset) {
    int err = [ccd setOffset:[sender intValue]];
    if (err) { char buf[128];
      sprintf(buf,"setting offset, err=%d",err);
      [Notice openNotice:buf title:P_TITLE];
    } else {
      YprefPutInt(DBE_OFFSET,ccd.offset,NO);  // b0024
    }
  } else
  if (sender == edit_gain) {
    int err = [ccd setGain:[sender intValue]];
    if (err) { char buf[128];
      sprintf(buf,"setting offset, err=%d",err);
      [Notice openNotice:buf title:P_TITLE];
    } else {
      YprefPutInt(DBE_GAIN,ccd.gain,NO);  // b0024
    }
  } else {
    assert(0);
  }
}

/* ---------------------------------------------------------------- */

- (IBAction)action_binning:(id)sender
{
  int  bx,by;
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,sender);
#endif
  assert(ccd);
  
  if (self.running) {
    [pop_binning selectItemAtIndex:(ccd.bin-1)];
    return;
  }
  Ystr2chr([sender titleOfSelectedItem],buf,sizeof(buf));
  sscanf(buf,"%dx%d",&bx,&by);
  assert(bx == by);
  (void)[ccd setBinning:bx];
}

/* ---------------------------------------------------------------- */

- (IBAction)action_menu:(id)sender
{
  char title[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,[[sender title] UTF8String]);
#endif

  Ystr2chr([sender title],title,sizeof(title));
  if (strstr(title,"Help")) {
    Ywww("http://instrumentation.obs.carnegiescience.edu/Software/ZWO/zwofinder.html");
    return;
  }
  if (!ccd) return;
  
  if (strstr(title,"Preferences") || strstr(title,"Settings")) {
      [chk_autostart setState:(YprefGetInt(DBE_AUTOSTART,0,NO) ? NSControlStateValueOn : NSControlStateValueOff)];
    [edit_fitspath setStringValue:_fitsPath];
    assert(ccd);
    [edit_setpoint setEnabled:ccd.hasCooler];
    [chk_cooler setEnabled:ccd.hasCooler];   // b0022
      [chk_cooler setState:(ccd.cooler ? NSControlStateValueOn : NSControlStateValueOff)];
    [edit_wbred setEnabled:ccd.isColor];
    [edit_wblue setEnabled:ccd.isColor];
    [pop_vbits selectItemWithTitle:[NSString stringWithFormat:@"%d",videoBits]];
    [panel_preferences makeKeyAndOrderFront:self];
  } else
  if (strstr(title,"Logfile")) {
#if (MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_13)
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:main_logger.logfile]];
#else
    [[NSWorkspace sharedWorkspace] openFile:main_logger.logfile];
#endif
  } else
  if (strstr(title,"DataPath")) {
    NSURL *url = [NSURL fileURLWithPath:_fitsPath isDirectory:YES];
    [[NSWorkspace sharedWorkspace] openURL:url];
  } else
  if (strstr(title,"AppSupport")) {    // filters.xml b0023
    NSURL *url = [NSURL fileURLWithPath:@(s_path) isDirectory:YES];
    [[NSWorkspace sharedWorkspace] openURL:url];
  } else {
    assert(0);
  }
}

/* ---------------------------------------------------------------- */

- (IBAction)action_pref:(id)sender
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,sender);
#endif
  assert([NSThread isMainThread]);
  assert(ccd);

  if (sender == chk_window) {
    [self updateWindowGeometry:YES];
  } else
  if (sender == edit_window) {
    [self updateWindowGeometry:YES];
  } else
  if ((sender == edit_wbred) || (sender == edit_wblue)) {
    [self updateWhiteBalance:YES];
  } else
  if ([[sender title] compare:@"Apply"] == NSOrderedSame) {
    [panel_preferences orderOut:self];
      YprefPutInt(DBE_AUTOSTART,([chk_autostart state] == NSControlStateValueOn) ? 1 : 0,NO);
    NSString *path = [edit_fitspath stringValue];
    if (!YpathWritable([path UTF8String])) YpathCreate([path UTF8String],YES);
    if (!YpathWritable([path UTF8String])) path = NSHomeDirectory();
    self.fitsPath = path;
    YprefPutString(@DBE_FITSPATH,_fitsPath,NO);
    if (ccd.hasCooler) {            // b0022
      float setp = [edit_setpoint floatValue];
      YprefPutDouble(DBE_SETPOINT,setp,use_shared); // b0029
        BOOL cool = ([chk_cooler state] == NSControlStateValueOn) ? YES : NO;
      dispatch_async(GLOBAL_QUEUE,^(void) {
        int err = [ccd setTemp:setp cooler:cool];
        if (err) [self showWarning:"setting setpoint failed" error:err];
      });
    }
    temp_interval = 2;
    [self updateWhiteBalance:NO];
    videoBits = [[pop_vbits titleOfSelectedItem] intValue];
    [self action_edit:edit_offset];
    [self action_edit:edit_gain];
    runNumber = [edit_run intValue]; [self updateRunNumber:0];
    [self updateWindowGeometry:YES];
  } else {
    assert(0);
  }
}

/* ---------------------------------------------------------------- */

- (IBAction)action_filter:(id)sender   // b0008
{
  int pos=-1;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,[[sender title] UTF8String]);
#endif
  if (!filterWheel) return;
  if (![pop_filter isEnabled]) return;
  if (![but_calibrate isEnabled]) return;

  if (sender == pop_filter) {
    pos = (int)[sender indexOfSelectedItem];
  }
  [sender setEnabled:NO];

  dispatch_async(GLOBAL_QUEUE,^(void) {
    if (sender == pop_filter) {
      [filterWheel moveToPosition:pos]; // {0..6}
    } else
    if (sender == but_calibrate) {
      [filterWheel calibrate];        // b0009
      dispatch_sync(MAIN_QUEUE,^(void) { [pop_filter selectItemAtIndex:0]; });
    }
    YcontrolEnable(sender,YES);
  });
}

/* ---------------------------------------------------------------- */
#pragma mark "Run"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (void)run_image:(id)param
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,param);
#endif
  assert(ccd);

  self.running = TRUE;
  [main_logger append:PREFUN];

  int err = [self openCCD:1]; 
  if (err) {
    self.running=FALSE;
    [Notice openNotice:"failed to open connection to camera" title:P_TITLE];
    return;
  }
  
  if (!err) err = [ccd expose];

  if (!err) {
    // NOTE: "display scaling" uses the original data -- we have to write before display
    if (writeFits) {
      if (ccd.bpp == 16) [self writeFitsFile];
    }
    if (ccd.bpp == 16) [self scale16:ccd.imageData count:(ccd.w*ccd.h)];
    dispatch_sync(MAIN_QUEUE,^(void) {
      NSBitmapImageRep *rep = [self update:ccd.imageData W:ccd.w H:ccd.h B:ccd.bpp];
      if (writeFits && ccd.isColor) { 
          NSData *data = [rep representationUsingType:NSBitmapImageFileTypeJPEG properties:@{}];
        NSString *file = [NSString stringWithFormat:@"%@/zf%04d.jpg",_fitsPath,runNumber];
        [data writeToFile:file atomically:NO];
        [self updateRunNumber:1];
      }
    });
  } else {
    [self showWarning:"exposure failed" error:err]; // b0019
  }
  
  self.lastImage = [NSDate date];
  self.running = FALSE;
}

/* ---------------------------------------------------------------- */

- (void)run_video:(id)param
{
  int  err=0;
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,param);
#endif
  assert(ccd);

  self.running = TRUE;  self.stop = FALSE;
  [main_logger append:PREFUN];

  err = [self openCCD:((videoBits==8) ? 0 : 1)]; // '8' or '12/16'
  if (err) {
    self.running=FALSE;
    [Notice openNotice:"failed to open connection to camera" title:P_TITLE];
    return;
  }
  
  err = [ccd startVideo];
  if (err) {
    [ccd close];
    self.running=FALSE;
    [Notice openNotice:"failed to start video" title:P_TITLE];
    return;
  }
  
  do { 
    err = [ccd getVideo:(2.0+ccd.exptime)];
    if (err) {
      sprintf(buf,"%.1f fps",1.0/YsecSince(ccd.date)); YeditSet(edit_fps,buf);
      continue;
    }
    if (ccd.bpp==8)       [self scale8:ccd.videoData count:(ccd.w*ccd.h)];
    else if (ccd.bpp==16) [self scale16:ccd.imageData count:(ccd.w*ccd.h)];
    dispatch_sync(MAIN_QUEUE,^(void) {
      [self update:ccd.videoData W:ccd.w H:ccd.h B:ccd.bpp];
      [edit_fps setStringValue:[NSString stringWithFormat:@"%.1f fps",ccd.fps]];
    });
    float dt = ccd.exptime - YsecSince(ccd.date);
    // if (dt > 0) printf("dtR=%f\n",dt);
    if (dt > 0) [NSThread sleepForTimeInterval:(dt/2.0)];
  } while (!self.stop);
  [ccd stopVideo];
  
  YeditSet(edit_fps,"");

  self.running = FALSE;
}

/* ---------------------------------------------------------------- */

// NOTE: ST5: 10 um pixels -- ZWO: 3.8 um pixels

#if (UNIT_TEST == 1)
- (void)run_test_image:(id)param
{
  int  err=0;
  float runtime=10.0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,param);
#endif
  assert(ccd);

  self.running = TRUE;  self.stop = FALSE;
  [main_logger append:PREFUN];

  err = [ccd open];
  if (err) {
    self.running=FALSE;
    [Notice openNotice:"failed to open connection to camera" title:P_TITLE];
    return;
  }
  [ccd setBPP:16];
  [ccd setWindowX:100 Y:150 W:800 H:200];

  NSMutableArray *array = [NSMutableArray arrayWithCapacity:(int)(1.0+runtime/ccd.exptime)];
  NSDate *start = [NSDate date];
  size_t size = ccd.w * ccd.h * sizeof(u_short);
  double s1=0,s2=0,sn=0;
  do {
    double t0 = YmsSince(start);
    [ccd expose];                         // overhead ~30 ms
    double dt = YmsSince(start)-t0;
    s1 += dt; s2 += dt*dt; sn += 1;
    NSData *data = [NSData dataWithBytes:[ccd imageData] length:size];
    [array addObject:data];
    if (self.stop) break;
  } while (YsecSince(start) < runtime);
  double ave = s1/sn;
  double sig = sqrt(s2/sn-ave*ave);
  double eff = (sn*ccd.exptime)/runtime;
  printf("ave=%.2f, sig=%.2f, n=%.0f, eff=%.3f\n",ave,sig,sn,eff);

  self.running = FALSE;
}
#endif

/* --- */

#if (UNIT_TEST == 2)
- (void)run_test_video:(id)param
{
  int  err=0;
  float runtime=10.0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,param);
#endif
  assert(ccd);

  self.running = TRUE;  self.stop = FALSE;
  [main_logger append:PREFUN];

  err = [ccd open];
  if (err) {
    self.running=FALSE;
    [Notice openNotice:"failed to open connection to camera" title:P_TITLE];
    return;
  }
  NSDate *start = [NSDate date];
  [ccd setBPP:16];
  [ccd setBinning:2];
  [ccd setWindowX:100 Y:150 W:800 H:200];
  [ccd startVideo];
  double t0 = YmsSince(start);
  
  NSMutableArray *array = [NSMutableArray arrayWithCapacity:(int)(1.0+runtime/ccd.exptime)];
  size_t size = ccd.w * ccd.h * sizeof(u_short);
  double s1=0,s2=0,sn=0;
  do {
    [ccd getVideo:(2.0+ccd.exptime)];    // bottom ~ 9 ms (160k pixels)
    double t1 = YmsSince(start);
    double dt = t1-t0;                   //        ~ 3 ms (40k pixels,bin=2)
    s1 += dt; s2 += dt*dt; sn += 1; t0 = t1;
    NSData *data = [NSData dataWithBytes:[ccd imageData] length:size];
    [array addObject:data];
    if (self.stop) break;
  } while (YsecSince(start) < runtime);
  [ccd stopVideo];
  double ave = s1/sn;
  double sig = sqrt(s2/sn-ave*ave);
  double eff = (sn*ccd.exptime)/runtime;
  printf("ave=%.2f, sig=%.2f, n=%.0f, eff=%.3f\n",ave,sig,sn,eff);

  self.running = FALSE;
}
#endif

/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
