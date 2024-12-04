/* ---------------------------------------------------------------- */
//
// Logger.m
//
// Created by Christoph Birk on 2013-04-10
// Copyright 2013 Carnegie Observatories. All rights reserved.
//
// 2014-03-07  /Users/Shared/Library/Logs
// 2014-04-02  set permissions when using Shared
// 2014-04-23  use Ystr2tmp()
// 2014-09-24  [append: toArray:]
// 2014-12-04  remove noticeLock
// 2014-12-10  YsharedLogPath()
// 2015-03-16  add CFBundleShortVersionString
// 2015-04-06  add subFolder, autoChange
// 2015-06-22  showUT
// 2015-10-28  timer=nil in setAutoChange
// 2016-08-09  mode, sync, close
// 2016-10-17  NSLog() instead of fprintf(stderr,...)
// 2017-03-13  setLogfile: set permissions after file is created
// 2017-09-13  remove <CR>,<LF> from messages
// 2018-03-19  add 'useUT' and 'offset' parameters to 'init' method
// 2018-04-23  replace Ystr2tmp() by [ UTF8String]
// 2018-11-01  move iVARs into implementation
//
/* ---------------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG      0                   // debug level
#endif

/* ---------------------------------------------------------------- */

#import "Logger.h"
#import "Notice.h"
#import "Yutils.h"

/* ---------------------------------------------------------------- */

static NSDateFormatter *timeFormatter=nil;

/* ---------------------------------------------------------------- */

static void replaceElement(NSMutableArray* array,NSString* string,int head)
{
  if (head < [array count]) {  // NSMutableArray protected by 'messageLock'
    [array replaceObjectAtIndex:head withObject:string];
  } else {
    [array addObject:string];
  }
}

/* ---------------------------------------------------------------- */

@implementation Logger
{
  NSString    *p_exec,*p_build,*p_version;
  NSString    *baseName;
  NSString    *last_text;
  NSString    *folder;
  NSLock      *messageLock,*fileLock;
  NSTableView *messageTable;
  NSTextField *messageField;
  NSMutableArray *mesgArray,*timeArray;
  int         n_msg,mesg_head;
  BOOL        sharedPath;
  int         period,offset;
  NSTimer     *timer;
  FILE        *file_pointer;
}

/* ---------------------------------------------------------------- */
#pragma mark "Class"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

+ (void)initialize
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

  if (self == [Logger class]) {        // avoid call by sub-class
    timeFormatter = [[NSDateFormatter alloc] init];
    [timeFormatter setDateFormat:@"HH:mm:ss"];
  }
}

/* ---------------------------------------------------------------- */
#pragma mark "Instance"
/* ---------------------------------------------------------------- */

- (Logger*)init
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

  return [self initWithBase:nil];
}

/* --- */

- (Logger*)initWithBase:(const char*)base
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,base);
#endif

  return [self initWithBase:base useShared:NO];
}

/* --- */

- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s,%d)\n",PREFUN,base,shared);
#endif

  return [self initWithBase:base useShared:shared period:LOGGER_WEEKLY];
}

/* --- */

- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared period:(int)per
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s,%d,%d)\n",PREFUN,base,shared,per);
#endif

  return [self initWithBase:base useShared:shared period:per useUT:NO];
}

/* --- */

- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared period:(int)per
           useUT:(BOOL)ut
{
  return [self initWithBase:base useShared:shared period:per useUT:ut folder:NULL];
}

/* --- */

- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared period:(int)per
           useUT:(BOOL)ut folder:(const char*)sub
{
  return [self initWithBase:base useShared:shared period:per useUT:ut folder:sub offset:9];
}

/* ---  */

- (Logger*)initWithBase:(const char*)base useShared:(BOOL)shared period:(int)per
           useUT:(BOOL)ut folder:(const char*)sub offset:(int)off
{
  NSMutableString *path=nil;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%d,%d,%p,%d)\n",PREFUN,base,shared,per,sub,off);
#endif

  if ((self = [super init]) == nil) return nil;  // Clang complains without
  
  NSBundle *bundle = [NSBundle mainBundle];
  p_exec    = [[bundle objectForInfoDictionaryKey:@"CFBundleExecutable"] retain];
  p_build   = [[bundle objectForInfoDictionaryKey:@"CFBundleVersion"] retain];
  p_version = [[bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] retain];
  if (base) baseName = @(base);
  else      baseName = [p_exec lowercaseString];
  [baseName retain];

  file_pointer = NULL;
  _mode = LOGGER_CLOSE;
  messageLock = [[NSLock alloc] init];
  fileLock = [[NSLock alloc] init];
  n_msg = 128;                      // table size
  mesgArray = [[NSMutableArray alloc] initWithCapacity:n_msg];
  timeArray = [[NSMutableArray alloc] initWithCapacity:n_msg];
  assert(mesg_head == 0);
  sharedPath = shared;
  period = per;
  offset = off;                     // 2018-03-19
  _showUT = ut;                     // 2018-03-19
  
  if (sharedPath) {
    path = [NSMutableString stringWithFormat:@"%@/%@",YsharedLogPath(),p_exec];
  } else { NSArray *ary;
    ary = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory,NSUserDomainMask,YES);
    if ([ary count] > 0) {
      path = [NSMutableString stringWithFormat:@"%@/Logs/%@",ary[0],p_exec];
    }
  }
  if (!path) path = [NSMutableString stringWithFormat:@"%@",NSHomeDirectory()];
  if (sub) [path appendFormat:@"/%s",sub];
  folder = [[NSString alloc] initWithString:path]; // 2018-03-19
  
  [self setLogfile:[self logfileName]];
  
  return self;
}

/* ---  */

- (Logger*)initWithBase:(const char*)base useUT:(BOOL)ut folder:(const char*)sub
{
  return [self initWithBase:base useShared:NO period:LOGGER_WEEKLY
               useUT:ut folder:sub offset:9];
}

/* ---------------------------------------------------------------- */

- (void)dealloc
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

  if (file_pointer) { fclose(file_pointer); }

  [p_version release];
  [p_build release];
  [p_exec release];
  [baseName release];
  [messageLock release];
  [fileLock release];
  [mesgArray release];
  [timeArray release];
  [folder release];
  
  [super dealloc];
}

/* ---------------------------------------------------------------- */

- (NSString*)logfileName
{
  time_t    ut; 
  struct tm *now,res;
  char      tstr[32];
  
  switch (period) {
#ifndef NDEBUG
  case -1:
    ut = time(NULL);
    now = localtime_r(&ut,&res);
    strftime(tstr,sizeof(tstr),"%Y_%m_%d_%H_%M",now);
    break;
#endif
  case LOGGER_HOURLY:
    ut = time(NULL);                     // UT
    now = localtime_r(&ut,&res);
    strftime(tstr,sizeof(tstr),"%Y_%m_%d_%H",now);
    break;
  case LOGGER_DAILY:
    ut = time(NULL) - offset*3600;
    now = localtime_r(&ut,&res);
    strftime(tstr,sizeof(tstr),"%Y_%m_%d",now);
    break;
  case LOGGER_WEEKLY:
  default:
    ut = time(NULL) - offset*3600;
    now = localtime_r(&ut,&res);
    strftime(tstr,sizeof(tstr),"%Y_%W",now);
    break;
  }
  
  return [NSString stringWithFormat:@"%@/%@%s.log",folder,baseName,tstr];
}

/* ---------------------------------------------------------------- */

- (void)setLogfile:(NSString*)file
{
  NSString *path = [file stringByDeletingLastPathComponent];
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  [fm createDirectoryAtPath:path  withIntermediateDirectories:YES
      attributes:nil error:NULL];      // create path if necessary
  [fm release];

  [fileLock lock];
  if (file_pointer) { fclose(file_pointer); file_pointer=NULL; }
  if (_logfile) [_logfile release];
  _logfile = [file retain];
  [fileLock unlock];
  
  [self message:[file UTF8String] file:YES];
  NSString *str = [NSString stringWithFormat:@"build= %@ (%@)",p_build,p_version];
  [self append:[str UTF8String]];
  if (sharedPath) YfilePermissions([file UTF8String],6,6,6); // rw-,rw-,rw-
}

/* ---------------------------------------------------------------- */

- (void)setField:(NSTextField*)field
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,field);
#endif
  [messageLock lock];
  messageField = field;
  [messageLock unlock];
}

/* ---------------------------------------------------------------- */

- (void)setTable:(NSTableView*)table
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,table);
#endif
  
  [messageLock lock];
  messageTable = table;
  if (table) [table setDataSource:self];
  [messageLock unlock];    
}

/* ---------------------------------------------------------------- */

- (void)setAutoChange:(BOOL)autoChange
{
  dispatch_async(dispatch_get_main_queue(),^(void) {
    if (timer) {
      [timer invalidate];
      [timer release]; timer = nil;
    }
    if (autoChange) {
      timer = [NSTimer scheduledTimerWithTimeInterval:60.0
             target:self selector:@selector(fireTimer:)
             userInfo:nil repeats:YES];
      [timer retain];
    }
  });
}

/* ---------------------------------------------------------------- */

- (void)fireTimer:(NSTimer*)theTimer
{
#if (DEBUG > 0)
  BOOL r = [self changeLogfile];
  if (r) fprintf(stderr,"%s: changed logfile '%s'\n",PREFUN,[_logfile UTF8String]);
#else
  [self changeLogfile];
#endif
}

/* ---------------------------------------------------------------- */

- (BOOL)changeLogfile
{
  BOOL r=FALSE;
#if (DEBUG > 1)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

  NSString *string = [self logfileName];
  if ([_logfile compare:string] != NSOrderedSame) { // name changed
    [self setLogfile:string];                       // set new name
    r = TRUE;
  }

  return r;
}

/* ---------------------------------------------------------------- */

- (void)addMessage:(const char*)text
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(): '%s'\n",PREFUN,text);
#endif
  assert(messageLock);
  assert(text);

  NSString *str1 = @(text);            // sanitize 2017-09-13
  NSString *str2 = [str1 stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
  NSString *str3 = [str2 stringByReplacingOccurrencesOfString:@"\n" withString:@" "];

  if (messageField) {                  // overwrite text field
    [messageLock lock];
    [messageField performSelectorOnMainThread:@selector(setStringValue:)
                  withObject:str3 waitUntilDone:YES];
    [messageLock unlock];
  }
  if (messageTable) {                  // add to table
    [messageLock lock];
    if (_showUT) { char buf[32]; struct tm *now,res;
      time_t ut = time(NULL);
      now = gmtime_r(&ut,&res);
      strftime(buf,sizeof(buf),"%H:%M:%S",now);
      replaceElement(timeArray,@(buf),mesg_head);
    } else {
      assert(timeFormatter);
      replaceElement(timeArray,[timeFormatter stringFromDate:[NSDate date]],mesg_head);
    }
    assert([timeArray count] <= n_msg);
    replaceElement(mesgArray,str3,mesg_head);
    assert([mesgArray count] <= n_msg);
    mesg_head = (mesg_head+1) % n_msg;
    [messageLock unlock];
    [messageTable performSelectorOnMainThread:@selector(reloadData)
                  withObject:nil waitUntilDone:NO];
  }
}

/* ---------------------------------------------------------------- */

- (void)message:(const char*)text level:(int)level time:(int)dt
           file:(BOOL)file suppress:(BOOL)suppress
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s)\n",PREFUN,text);
#endif

  if (file) [self append:text];

  if (level >= 0) {                    // write to message window
    [self addMessage:text];
#ifdef INCLUDE_NOTICE_H
    if (level > 0) { BOOL show=YES;    // open notice?
      if (suppress) {                  // same text as last time?
        NSString *string = @(text);
        @synchronized(self) {
          if (!last_text || ([last_text compare:string] != NSOrderedSame) || (dt > 0)) {
            if (last_text) [last_text release];
            last_text = [string retain];
            show = YES;
          } else show = NO;
        }
      }
      if (show) {
        [Notice openNotice:text title:[p_exec UTF8String] time:dt level:level];
      }
    }
#endif
  }
}

/* --- */

- (void)message:(const char*)text level:(int)level time:(int)dt file:(BOOL)file
{
  [self message:text level:level time:dt file:file suppress:NO]; // bug-fix 2014-12-08
}

/* --- */

- (void)message:(const char*)text level:(int)level time:(int)dt
{
  [self message:text level:level time:dt file:NO];
}

/* --- */

- (void)message:(const char*)text level:(int)level file:(BOOL)file
{
  [self message:text level:level time:0 file:file];
}

/* --- */

- (void)message:(const char*)text file:(BOOL)file
{
  [self message:text level:0 time:0 file:file];
}

/* --- */

- (void)message:(const char*)text level:(int)level
{
  [self message:text level:level time:0 file:NO];
}

/* --- */

- (void)message:(const char*)text
{
  [self message:text level:0 time:0 file:NO];
}

/* ---------------------------------------------------------------- */

- (void)append:(const char*)text
{
  [self append:text toArray:nil];
}

/* ---------------------------------------------------------------- */

- (void)append:(const char*)text toArray:(NSMutableArray*)array
{
  NSString *string=nil;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s)\n",PREFUN,text);
#endif

  NSString *str1 = @(text);           // sanitize 2017-09-13
  NSString *str2 = [str1 stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
  NSString *str3 = [str2 stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
  text = [str3 UTF8String];
  
  [fileLock lock];
  
  if (!file_pointer) file_pointer = fopen([_logfile UTF8String],"a");
  if (!file_pointer) {                // append to logfile
    NSLog(@"cannot open file '%@'",_logfile);
  } else { char buf[32]; struct tm *now,res; time_t ut;
    ut = time(NULL);  
    if (_showUT) now = gmtime_r(&ut,&res);
    else         now = localtime_r(&ut,&res);
    strftime(buf,sizeof(buf),"%b %d %H:%M:%S",now);
    fprintf(file_pointer,"%s %s\n",buf,text);
    switch (_mode) {
    case LOGGER_FLUSH:
      fflush(file_pointer);
      break;
    case LOGGER_CLOSE:
      fclose(file_pointer); file_pointer = NULL;
      break;
    }
    if (array) string = [NSString stringWithFormat:@"%s %s",buf,text];
  }
  [fileLock unlock];
  
  if (string) {                      // append to array
    @synchronized(array) {
      [array addObject:string];
    }
  }
}

/* ---------------------------------------------------------------- */

- (void)sync 
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

  [fileLock lock];
  if (file_pointer) fflush(file_pointer);
  [fileLock unlock];
}

/* ---------------------------------------------------------------- */

- (void)close
{
  [fileLock lock];
  if (file_pointer) {
    fclose(file_pointer); file_pointer = NULL;
  }
  [fileLock unlock];
}

/* ---------------------------------------------------------------- */
#pragma mark "NSTableView dataSource"
/* ---------------------------------------------------------------- */

- (NSInteger)numberOfRowsInTableView:(NSTableView*)table
{
#if (DEBUG > 2)
  fprintf(stderr,"%s(%p): msg_n=%d\n",PREFUN,table,n_msg);
#endif
  return [mesgArray count];
}

/* ---------------------------------------------------------------- */

- (id)tableView:(NSTableView*)table
      objectValueForTableColumn:(NSTableColumn*)column
      row:(NSInteger)row
{
  int      i;
  NSString *string=nil;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p,%ld)\n",PREFUN,table,column,row);
#endif
  assert(row < [mesgArray count]);
  if (row >= [mesgArray count]) return nil;
  
#if (DEBUG > 0) && !defined(NDEBUG) && defined(INCLUDE_NOTICE_H)
  if (![NSThread isMainThread]) { char buf[128];
    sprintf(buf,"isMainThread=%d, row=%ld\n",[NSThread isMainThread],(long)row);
    [Notice openNotice:buf title:PREFUN time:600 level:1];
  }
#endif

  [messageLock lock];
  i = mesg_head-(int)row-1; if (i < 0) i += n_msg;
  switch ([[column identifier] intValue]) {
  case 0:                              // time
    string = timeArray[i];
    break;
  case 1:                              // text
    string = mesgArray[i];
    break;
  default:
    assert(0);
  }
  [messageLock unlock];

  return string;
}

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
