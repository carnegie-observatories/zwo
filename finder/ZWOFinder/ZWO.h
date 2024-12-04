/* ---------------------------------------------------------------- */
//
// ZWO.h
// ZWOFinder
//
// Created by Christoph Birk on 2019-01-14
// Copyright © 2019 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import "Tcpip.h"

/* ---------------------------------------------------------------- */

enum zwo_error_enum {
  E_zwo_error=E_tcpip_error+100,
  E_zwo_transfer,
  E_zwo_last
};

/* ---------------------------------------------------------------- */

@interface ZWO : TCPIP_Connection
{
}

@property (readonly,nonatomic) int maxWidth,maxHeight;
@property (readonly,nonatomic) int hasCooler,isColor,bitDepth; 
@property (readonly,nonatomic) int startX,startY;
@property (readonly,nonatomic) int width,height,binning,imageType;
@property (readonly,nonatomic) int expStatus;
@property (readonly,nonatomic) int droppedFrames;
@property (retain,nonatomic) NSString *name;

- (int)getNumASI;
- (int)getCameraProperty;
- (int)getSerial:(char*)string;
- (int)setupASI;
- (int)setControl:(int)control value:(int)value;
- (int)getControl:(int)control value:(int*)value;
- (int)setROIFormatW:(int)w H:(int)h B:(int)b T:(int)t;
- (int)getROIFormatW:(int*)w H:(int*)h B:(int*)b T:(int*)t;
- (int)setStartPosX:(int)x Y:(int)y;
- (int)getStartPosX:(int*)x Y:(int*)y;
- (int)startExposure;
- (int)getExpStatus;
- (int)getDataAfterExp:(u_char*)data size:(size_t)size;
- (int)startVideoCapture;
- (int)getVideoData:(u_char*)data size:(size_t)size wait:(int)msec;
- (int)getDroppedFrames;
- (int)stopVideoCapture;
- (int)closeASI;

@property (readonly,nonatomic) int numSlots,uniDir,position,efw_id;
@property (retain,nonatomic) NSString *efwName;

- (int)getNumEFW;
- (int)openEFW;
- (int)closeEFW;
- (int)getProperty;
- (int)getDirection;
- (int)getPosition;
- (int)setPosition:(int)pos;
- (int)calibrate;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
