/* ---------------------------------------------------------------- */
//
// FITS.h
//
// Created by Christoph Birk on 2015-08-21
// Copyright (c) 2015 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import <Foundation/Foundation.h>

/* ---------------------------------------------------------------- */

#define FITS_MAX_CHAR   60

#define KEY_TRIMSEC     "TRIMSEC"
#define KEY_DATASEC     "DATASEC"

u_short* fits_load16(const char* file,int* w,int* h,int trim);
int*     fits_load32(const char* file,int* w,int* h,int trim);
float*   fits_load4 (const char*,int*,int*,int);

/* ---------------------------------------------------------------- */

@interface FITS : NSObject
{
  int      n_folders,big;
  NSString *linkname,*linktarg,*base;
  FILE     **fp;
  u_int    nbytes,nlines,data_width,data_height;
  short    *swabuf,*shobuf;
}

@property (readwrite,nonatomic)      double   bzero;
@property (readonly,nonatomic,copy) NSString *failedFile;
@property (readonly,nonatomic)      NSString *linktarg;

- (instancetype)initWithBase:(const char*)preStr width:(u_int)w height:(u_int)h;
- (BOOL)open:(const char*)file paths:(NSArray*)datapaths;
- (void)intValue:(int)value forKey:(const char*)key comment:(const char*)com;
- (void)floatValue:(double)value forKey:(const char*)key prec:(int)dp comment:(const char*)com;
- (void)expValue:(double)value forKey:(const char*)key prec:(int)dp comment:(const char*)com;
- (void)charValue:(const char*)value forKey:(const char*)key comment:(const char*)com;
- (void)stringValue:(NSString*)value forKey:(const char*)key comment:(const char*)com;
- (void)endHeader;
- (void)writeLine:(u_short*)buf;
- (void)writeLine:(u_short*)buf bzero:(int)bz;
- (void)writeShort:(short*)buf;
- (void)writeData:(u_short*)buf;
- (void)close:(int)clink;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
