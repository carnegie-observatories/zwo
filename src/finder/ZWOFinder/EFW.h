/* ---------------------------------------------------------------- */
//
// EFW.h
// ZWOFinder
//
// Created by Christoph Birk on 2018-12017
// Copyright © 2018 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import <Foundation/Foundation.h>

#import "ZWO.h"                   // b0015

/* ---------------------------------------------------------------- */

enum efw_error_enum {
  E_efw_error=3000,
  E_efw_timeout,
  E_efw_last
};
/* ---------------------------------------------------------------- */

@interface EFW : NSObject

@property (readonly) int numPos,curPos;
@property (readonly,retain) NSString *filter;

+ (NSArray*)scan:(ZWO*)server;
+ (NSString*)errorString:(int)err;

- (id)initWithServer:(ZWO*)server;

- (int)setup:(int)ident;
- (int)calibrate;
- (int)moveToPosition:(int)pos;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
