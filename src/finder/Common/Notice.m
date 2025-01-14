/* ---------------------------------------------------------------- */
//
// Notice.m
//
// Created by Christoph Birk on 2012-10-18
// Copyright 2012 Carnegie Observatories. All rights reserved.
//
// Icons: /System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/
//
// 2013-09-10  use GCD
// 2014-04-14  property 'closed'
// 2014-04-24  start timer on first openNotice
// 2014-11-17  fix deadlock when called from main & background thread
//             all access to 'n_array' in mainThread
//             use '@sychronized' to protect open_notice return value
// 2018-10-04  chr2str() --> @()
// 2018-11-01  removed iVARs from interface
//
/* ---------------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           0              // debug level
#endif

/* ---------------------------------------------------------------- */

#import  "Notice.h"

/* ---------------------------------------------------------------- */

static NSTimer        *timer;
static NSMutableArray *n_array=nil;   // NOTE: all access on mainThread
static NSString       *p_exec=nil;
static       float    pos_x,pos_y;
static const float    def_w=380,def_h=100;

/* ---------------------------------------------------------------- */

static NSArray* arrayFromString(const char* string,const char* sep)
{
  char *buffer,*b,*p;
  
  assert(string);
  NSMutableArray *ary = [[NSMutableArray alloc] initWithCapacity:4];
  buffer = b = (char*)malloc(1+strlen(string));
  strcpy(buffer,string);
  while ((p=strsep(&b,sep))) {
    if (*p) [ary addObject:@(p)];
  }  
  free((void*)buffer);
  
  return [ary autorelease];
}

/* ---------------------------------------------------------------- */

@interface Notice ()                   // private class extension
@property (assign)             time_t auto_close;
@property (assign,nonatomic)   id     target;
@property (assign,nonatomic)   SEL    selector;
@end

@implementation Notice
{
}

/* ---------------------------------------------------------------- */

+ (void)initialize
{
  if (self != [Notice class]) return;  // avoid call by sub-class
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p: mainThread=%d\n",__func__,self,[NSThread isMainThread]);
#endif
  
  p_exec = [[[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleExecutable"] 
                                   retain];
  n_array = [[NSMutableArray alloc] initWithCapacity:2];
  
  NSRect rect = [[NSScreen mainScreen] visibleFrame];
  pos_x = (rect.size.width-def_w)/2;
  pos_y = (2*(rect.size.height+def_h))/3;
}

/* ---------------------------------------------------------------- */

+ (void)fireTimer:(NSTimer*)theTimer
{
  int    i;
  Notice *note;
#if (DEBUG > 1)
  if ([n_array count]) {
    fprintf(stderr,"%s(%p): n_array.count=%u\n",__func__,theTimer,(u_int)[n_array count]);
  }
#endif
  assert([NSThread isMainThread]);
  
  for (i=0; i<[n_array count]; i++) {  // check list of open notes
    note = n_array[i];
    assert(note);
    if (note.closed) {                 // closed by GUI
      [n_array removeObjectAtIndex:i];
    } else {                           // auto-close ?
#ifndef NDEBUG
      [note check_me];
#endif
      if (time(NULL) >= [note auto_close]) {
        [[note window] close];
      }
    }
  }
}

/* ---------------------------------------------------------------- */

+ (Notice*)open_notice:(NSArray*)lines title:(NSString*)title
                  time:(int)wait level:(int)level
                  target:(id)object selector:(SEL)sel
{
  int         i;
  float       w=def_w,h=def_h,d;
  NSTextField *edit;
  NSString    *string,*file=nil;
  NSSize      size;

#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",__func__,lines);
#endif
  assert([NSThread isMainThread]);

  Notice *note = [[Notice alloc] init];
  
  NSFont *font0 = [NSFont boldSystemFontOfSize:14];
  NSDictionary *attrs0 = [[NSDictionary alloc] 
                           initWithObjectsAndKeys:font0,NSFontAttributeName,nil];
  NSFont *font1 = [NSFont labelFontOfSize:12];
  NSDictionary *attrs1 = [[NSDictionary alloc] 
                           initWithObjectsAndKeys:font1,NSFontAttributeName,nil];

  for (i=0; i<[lines count]; i++) {    // find max. text width
    string = lines[i];
    size = [string sizeWithAttributes:((i==0) ? attrs0 : attrs1)];
    if (10+size.width > def_w-120) {
      w = fmax(w,120+size.width+10);
    }
    if (i > 0) h += size.height+4;
  }

  NSRect rect = [[NSScreen mainScreen] visibleFrame]; // lower left corner
  if ([n_array count] == 0) {          // reset default position
    pos_x = (rect.size.width-def_w)/2;
    pos_y = (2*(rect.size.height+def_h))/3;
  }
  if (pos_x+w > rect.origin.x+rect.size.width) pos_x = rect.origin.x;
  if (pos_y <= h) pos_y = rect.origin.y+(3*rect.size.height)/4;

  NSWindow *window = [[NSWindow alloc] 
                  initWithContentRect:NSMakeRect(pos_x,pos_y-h,w,h)
#if (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12) 
                  styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
#else
                  styleMask:NSTitledWindowMask|NSClosableWindowMask
#endif
                  backing:NSBackingStoreBuffered
                  defer:NO];
  [window setLevel:NSFloatingWindowLevel];
  [window setTitle:title];
  [window setDelegate:note];
  note.window = window;            // Note: increases reference count

  for (i=0; i<[lines count]; i++) {
    string = lines[i];
    size = [string sizeWithAttributes:((i==0) ? attrs0 : attrs1)];
    d = size.height+4;
    edit = [[NSTextField alloc] initWithFrame:NSMakeRect(95,h-40-d*i,w-120,d-1)];
    [edit setFont:((i==0) ? font0 : font1)];
    [edit setSelectable:NO];
    [edit setBezeled:NO];
    [edit setDrawsBackground:NO];
    [edit setStringValue:lines[i]];
    [[note.window contentView] addSubview:edit]; // increases ref.count
    [edit release];
  }
  [attrs0 release];
  [attrs1 release];
 
  NSButton *button = [[NSButton alloc] initWithFrame:NSMakeRect(105+w/4,15,60,25)];
  [button setTitle:@"OK"];             // button almost centered
  [button setBezelStyle:NSBezelStyleRounded];
  [button setButtonType:NSButtonTypeMomentaryPushIn];
  [button setTarget:note];
  [button setAction:@selector(action_ok:)];
  [[note.window contentView] addSubview:button]; // increases ref.count
  [button release];

  NSImageView *view = [[NSImageView alloc] initWithFrame:NSMakeRect(17,h-90,70,70)];
  [[note.window contentView] addSubview:view]; // increases ref.count
  NSBundle *bundle = [NSBundle mainBundle];
  switch (level) {                     // set proper icon
  case 1:
    file = [bundle pathForResource:@"AlertCautionIcon" ofType:@"icns"];
    break;
  case 2:
    file = [bundle pathForResource:@"AlertStopIcon" ofType:@"icns"];
    break;
  default:
    file = [bundle pathForResource:[p_exec lowercaseString] ofType:@"icns"];
    if (!file) {
      file = [bundle pathForResource:@"AlertNoteIcon" ofType:@"icns"];
    }
    break;
  }
  if (file) {
    NSImage *image  = [[NSImage alloc] initWithContentsOfFile:file];
    [view setImage:image];
    [image release];
  }
  [view release];

  note.auto_close = (wait) ? time(NULL)+wait : 0x7fffffff;  // NOTE: breaks in 2038

  [window makeKeyAndOrderFront:self];  
  [window release];

  note.target = object;            // install callback
  note.selector = sel;
  
  [n_array addObject:note];
  [note release];
  pos_x += 20; pos_y -= 20;

  if (!timer) {
    assert([NSThread isMainThread]);
    timer =  [NSTimer scheduledTimerWithTimeInterval:5.0
                      target:self selector:@selector(fireTimer:)
                      userInfo:nil repeats:YES];
  }
  
  return note;
}

/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

+ (Notice*)openNotice:(const char*)text title:(const char*)title 
              time:(int)wait level:(int)level target:(id)object selector:(SEL)sel
{
  Notice *note=nil;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p,%d,%d,%p,%p)\n",__func__,text,title,wait,level,object,sel);
#endif

  if (!text) return nil;               // allows initialization
  assert(title);
  
  NSArray *lines = arrayFromString(text,"\r\n");
  NSString *tstr = @(title);

  if ([NSThread isMainThread]) {
    note = [self open_notice:lines title:tstr time:wait level:level
                 target:object selector:sel];
  } else { static Notice *g_note=nil;
    @synchronized(self) {
      dispatch_sync(dispatch_get_main_queue(),^(void) {
        g_note = [self open_notice:lines title:tstr time:wait level:level
                       target:object selector:sel];
      });
      note = g_note;
    }
  }
  
  return note;
}

/* ---------------------------------------------------------------- */

+ (Notice*)openNotice:(const char*)text title:(const char*)title 
                level:(int)level target:(id)object selector:(SEL)sel
{  
  return [self openNotice:text title:title time:0 level:level 
               target:object selector:sel];
}

/* ---------------------------------------------------------------- */

+ (Notice*)openNotice:(const char*)text title:(const char*)title time:(int)wait 
                level:(int)level
{ 
  return [self openNotice:text title:title time:wait level:level 
               target:nil selector:nil];
}

/* ---------------------------------------------------------------- */

+ (Notice*)openNotice:(const char*)text title:(const char*)title time:(int)wait
{
  return [self openNotice:text title:title time:wait level:0];
}

/* ---------------------------------------------------------------- */

+ (Notice*)openNotice:(const char*)text title:(const char*)title
{
  return [self openNotice:text title:title time:0 level:0];
}

/* ---------------------------------------------------------------- */

- (void)windowWillClose:(NSNotification*)notification
{
#if (DEBUG > 1) 
  fprintf(stderr,"%s(%p)\n",__func__,notification);
#endif
  assert([NSThread isMainThread]);
  if (_target) {
    [_target performSelector:_selector withObject:nil afterDelay:0];
  }
  _closed = TRUE;
}

/* ---------------------------------------------------------------- */

- (void)close
{
#if (DEBUG > 1)
  fprintf(stderr,"%s:%p: mainThread=%d\n",__func__,self,[NSThread isMainThread]);
#endif

  if ([NSThread isMainThread]) {
    if ([n_array containsObject:self]) {
      [self.window close];
    }
  } else {                               // call myself on mainThread
    [self performSelectorOnMainThread:@selector(close)
          withObject:nil waitUntilDone:YES];
  }
}

/* ---------------------------------------------------------------- */

#ifndef NDEBUG
- (void)check_me
{
#if (DEBUG > 1)
  fprintf(stderr,"%s:%p: isVisible=%d\n",__func__,self,[[self window] isVisible]);
#endif
}
#endif

/* ---------------------------------------------------------------- */

- (void)action_ok:(id)sender
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",__func__,sender);
#endif
  [[self window] close];
}

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
