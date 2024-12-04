/* ---------------------------------------------------------------- */
//
// Logger.h
//
// Created by Christoph Birk on 2013-04-10
// Copyright 2013 Carnegie Observatories. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import <Cocoa/Cocoa.h>

/* ---------------------------------------------------------------- */

enum logger_period_enum { LOGGER_WEEKLY=1,LOGGER_DAILY,LOGGER_HOURLY };

enum logger_keep_enum { LOGGER_CLOSE,LOGGER_FLUSH,LOGGER_OPEN };

/* ---------------------------------------------------------------- */

@interface Logger : NSObject <NSTableViewDataSource>
{
}

@property (readonly ,nonatomic) NSString *logfile;
@property (readwrite,nonatomic) BOOL showUT;
@property (readwrite,nonatomic) int  mode;

- (Logger*)init;
- (Logger*)initWithBase:(const char*)base;
- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared;
- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared period:(int)per;
- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared period:(int)per
           useUT:(BOOL)ut;
- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared period:(int)per
           useUT:(BOOL)ut folder:(const char*)sub;
- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared period:(int)per
           useUT:(BOOL)ut folder:(const char*)sub offset:(int)off;
- (Logger*)initWithBase:(const char*)base useUT:(BOOL)ut folder:(const char*)sub;

- (NSString*)logfileName;
- (BOOL)changeLogfile;
- (void)setTable:(NSTableView*)table;
- (void)setField:(NSTextField*)field;
- (void)setAutoChange:(BOOL)autoChange;

- (void)message:(const char*)text level:(int)level time:(int)dt file:(BOOL)file suppress:(BOOL)suppress;
- (void)message:(const char*)text level:(int)level time:(int)dt file:(BOOL)file;
- (void)message:(const char*)text level:(int)level time:(int)dt;
- (void)message:(const char*)text level:(int)level file:(BOOL)file;
- (void)message:(const char*)text file:(BOOL)file;
- (void)message:(const char*)text level:(int)level;
- (void)message:(const char*)text; 

- (void)append:(const char*)text;
- (void)append:(const char*)text toArray:(NSMutableArray*)array;

- (void)sync;
- (void)close;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
