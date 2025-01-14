/* ---------------------------------------------------------------- */
//
// ASI.h
// ZWOFinder
//
// Created by Christoph Birk on 2018-12-05
// Copyright © 2018 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import <Foundation/Foundation.h>

#import "ZWO.h"                

/* ---------------------------------------------------------------- */

enum asi_error_enum {
  E_asi_error=2000,
  E_asi_devInvalid,
  E_asi_devNotFound,
  E_asi_devNotOpen,
  E_asi_tempRead,
  E_asi_readout,
  E_asi_last
};

/* ---------------------------------------------------------------- */

@interface ASI : NSObject

@property (nonatomic,readonly) float exptime;
@property (nonatomic,readonly) int gain,offset,bin,startX,startY;
@property (readonly) u_short *imageData;
@property (readonly) u_char *videoData;
@property (readonly) u_int w,h,bpp,bitDepth; // binned pixels
@property (readonly) double fps;
@property (readonly,nonatomic) float temperature,percent;
@property (readwrite,nonatomic) float setpoint;
@property (readwrite,nonatomic) int wbred,wblue; 
@property (nonatomic,retain) NSDate *date;
@property (readonly,nonatomic) BOOL Simulator,isColor;
@property (readonly,nonatomic) BOOL hasCooler,cooler;

+ (NSArray*)scan:(ZWO*)server;
+ (NSString*)errorString:(int)err;

- (id)initWithServer:(ZWO*)server;
- (int)open;
- (void)close;

- (int)setExpTime:(float)value;
- (int)setGain:(int)value;
- (int)setOffset:(int)value;
- (int)setBinning:(int)value;
- (int)setBPP:(int)bits;
- (int)setWindowX:(int)x Y:(int)y W:(int)w H:(int)h;
- (int)setGeometry:(BOOL)wmode;
- (int)setFlipX:(int)fx Y:(int)fy;
- (int)setTemp:(float)setp cooler:(BOOL)cool;
- (int)setWhiteBalanceRed:(int)r blue:(int)b;

- (int)getSerial:(char*)string;

- (int)updateTemp;

- (int)expose;

- (int)startVideo;
- (int)getVideo:(float)timeout;
- (int)stopVideo;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
