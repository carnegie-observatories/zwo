/* ---------------------------------------------------------------- */
//
// Yutils.m
//
// Created by Christoph Birk on 2011-04-19
// Copyright 2011 Carnegie Observatories. All rights reserved.
//
// 2011-04-19  initial version
// 2013-07-11  Yutils class
// 2013-09-11  replace Yutils class with GCD
// 2014-03-00  Shared Preferences
// 2014-04-02  fix permission on Shared Preference
// 2014-04-03  Ystr2tmp()
// 2014-04-07  combine Ypref and Yshared
// 2014-04-10  YprefSetupShared()
// 2014-04-18  YbundleInfo(char**,...)
// 2014-04-21  YappSupportCreate(char**)
//             YbundleResource()
// 2014-05-09  YimageFile() accepts NULL-pointer
// 2014-05-13  YarrayToInts()
// 2014-07-28  YpopupItemRename()
// 2014-08-12  YpopupItemEnable()
// 2014-09-10  YbundleDictionary(name,ext)
// 2014-10-15  YappNapDisabled()
// 2014-10-27  YnotifyNotice()
// 2014-12-10  YappSupportCreate(char** path,BOOL shared)
//             YsharedLogPath()
// 2015-01-20  YbundleDictionary(name)
// 2015-02-24  YsheetNotify() removed unused parameters
// 2015-02-26  fixed Ystr2chr()
// 2015-02-27  MAC_OS_X_VERSION_MIN_REQUIRED
// 2015-05-03  YeditSet() (replaces YeditText)
//             mark some functions as "deprecated"
// 2016-03-11  NSWorkspace recycleURLs:completionHandler:
// 2016-03-14  YosVersion()
// 2016-06-27  mark some functions as "deprecated"
// 2017-05-04  require macOS 10.9+
//             removed some deprecated functions
// 2017-12-08  enforce mainThread for YeditGet()
// 2018-04-23  avoid use of Ystr2tmp(),Ychr2str() here
// 2019-05-01  Yplist..()
//
/* ---------------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG      1
#endif

/* ---------------------------------------------------------------- */

#import  "Yutils.h"

/* ---------------------------------------------------------------- */

#define SHARED_BASE  "/Users/Shared/Library"
#define SHARED_LOGS  "Logs"
#define SHARED_APPS  "Application Support"
#define SHARED_PREF  "Preferences"
#define SHARED_FILE  "%s/%s/%@.plist"

/* ---------------------------------------------------------------- */
#pragma mark "Bundle"
/* ---------------------------------------------------------------- */

static char* copy_rescource(NSString *string,char** copy)
{
  if (string) {
    const char *temp = [string UTF8String];
    *copy = (char*)malloc((1+strlen(temp))*sizeof(char));
    strcpy(*copy,temp);
  } else {
    *copy = NULL;
  }
  return *copy;
}

/* --- */

BOOL YbundleInfo(char** exec,char** build,char** version,
                 char** info,char** path,char** ident)
{
  BOOL     r=TRUE;
  NSString *string;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p,%p,%p,%p,%p)\n",__func__,exec,build,version,
          info,path,ident);
#endif
  assert([NSThread isMainThread]);
  
  NSBundle *bundle = [NSBundle mainBundle]; if (!bundle) return FALSE;

  if (exec) {
    string = [bundle objectForInfoDictionaryKey:@"CFBundleExecutable"];
    if (!copy_rescource(string,exec)) r = FALSE;
  }
  if (build) {
    string = [bundle objectForInfoDictionaryKey:@"CFBundleVersion"];
    if (!copy_rescource(string,build)) r = FALSE;
  }
  if (version) { 
    string = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    if (!copy_rescource(string,version)) r = FALSE;
  }
  if (info) {
    string = [bundle objectForInfoDictionaryKey:@"NSHumanReadableCopyright"];
    if (!string) string = [bundle objectForInfoDictionaryKey:@"CFBundleGetInfoString"];
    if (!copy_rescource(string,info)) r = FALSE;
  }
  if (path) {
    if (!copy_rescource([bundle resourcePath],path)) r = FALSE;
  }
  if (ident) {
    if (!copy_rescource([bundle bundleIdentifier],ident)) r = FALSE;
  }
  
  return r;
}

/* ---------------------------------------------------------------- */

const char* YbundleResource(const char* name,const char* ext)
{
  // assert([NSThread isMainThread]);

  NSString *string = [[NSBundle mainBundle] pathForResource:@(name)
                                            ofType:@(ext)];
  if (!string) return NULL;
  
  return [string UTF8String];
}

/* ---------------------------------------------------------------- */

NSDictionary* YbundleDictionary(const char* name)
{
  NSString *file = [[NSBundle mainBundle] pathForResource:@(name)
                                          ofType:@"xml"];
  NSDictionary *dict = [NSDictionary dictionaryWithContentsOfFile:file];
  
  return dict;
}

/* ---------------------------------------------------------------- */

NSArray* YbundleArray(const char* name)
{
  NSString *file = [[NSBundle mainBundle] pathForResource:@(name)
                                          ofType:@"xml"];
  NSArray *array = [NSArray arrayWithContentsOfFile:file];
  
  return array;
}

/* ---------------------------------------------------------------- */

char* YappSupportCreate(char **path,BOOL shared)
{
  NSString *name,*dir=nil;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d)\n",__func__,path,shared);
#endif

  NSFileManager *fm = [[NSFileManager alloc] init];  // MT-safe

  if (shared) {
    dir = [NSString stringWithFormat:@"%s/%s",SHARED_BASE,SHARED_APPS];
  } else {
    NSArray *array = [fm URLsForDirectory:NSApplicationSupportDirectory
                         inDomains:NSUserDomainMask];
    if ([array count] > 0) {           // ~/Library/Application Support
      dir = [[array firstObject] path];
    }
  }
  if (!dir) {                          // fall-back to $HOME directory
    dir = NSHomeDirectory();
  }
  
  name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleExecutable"];
  NSString *string = [NSString stringWithFormat:@"%@/%@",dir,name];
  
  if (![fm fileExistsAtPath:string]) { NSDictionary *attr=nil;
    BOOL inter = (shared) ? YES : NO;
    if (shared) {
      NSNumber *perm = [NSNumber numberWithShort:((7*64)+(7*8)+7)]; // rwx,rwx,rwx
      attr = [NSDictionary dictionaryWithObjectsAndKeys:perm,NSFilePosixPermissions,nil];
    }
    [fm createDirectoryAtPath:string withIntermediateDirectories:inter
        attributes:attr error:NULL];
  }
  [fm release];
  
  const char *temp = [string UTF8String];
  *path = (char*)malloc((1+strlen(temp))*sizeof(char));
  strcpy(*path,temp);
  
  return *path;
}

/* ---------------------------------------------------------------- */

BOOL YappNapDisabled(void)
{
  int vMaj,vMin,vFix;
  NSString *key=@"NSAppSleepDisabled";

  YosVersion(&vMaj,&vMin,&vFix);
  if (vMin < 9) return TRUE;  // AppNap was introduced in Mavericks (10.9)
  
  BOOL nonap = [[NSUserDefaults standardUserDefaults] boolForKey:key];
  if (!nonap) {
    [[NSUserDefaults standardUserDefaults] setBool:TRUE forKey:key];
  }
  return nonap;
}

/* ---------------------------------------------------------------- */
#pragma mark "String"
/* ---------------------------------------------------------------- */

char* Ystr2chr(NSString* string,char* buf,int bufsize)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p,%d)\n",__func__,string,buf,bufsize);
#endif
  assert(string);

  if ([string length] >= bufsize) {  // 2015-02-26
    char *buffer = (char*)malloc(1+[string length]);
    Ystr2chr(string,buffer,1+(int)[string length]);
    buffer[bufsize-1] = '\0';        // truncate
    strcpy(buf,buffer);
    free((void*)buffer);
  } else {
    if (![string getCString:buf maxLength:bufsize encoding:NSASCIIStringEncoding]) {
      if (![string getCString:buf maxLength:bufsize encoding:NSUTF8StringEncoding]) {
        assert(0);
        *buf = '\0';
      }
    }
  }
  return buf;
}

/* --- */

const char* Ystr2tmp(NSString* string)
{
  const char *r=NULL;
  assert(string);                      

  if (!(r=[string cStringUsingEncoding:NSASCIIStringEncoding])) {
    if (!(r=[string cStringUsingEncoding:NSUTF8StringEncoding])) {
      static const char *empty="";
      assert(0);
      return empty;
    }
  }
  return r;
}

/* ---------------------------------------------------------------- */

NSString* Ychr2str(const char *text)
{
  assert(text);
  NSString *string = [[NSString alloc] initWithCString:text 
                                       encoding:NSASCIIStringEncoding];
                                       
  return [string autorelease];         // NSString is MT-safe
}

/* ---------------------------------------------------------------- */
#pragma mark "Array"
/* ---------------------------------------------------------------- */

NSArray* YarrayFromChars(const char* chars)
{
#if 1                          // Note: 4 times slower than strsep()
  NSString *string = @(chars);
  NSArray *a = [string componentsSeparatedByString:@" "];
  assert([a count]);
  NSMutableArray *r = [NSMutableArray arrayWithCapacity:[a count]];
  for (NSString *str in a) {
    if ([str compare:@""] != NSOrderedSame) [r addObject:str];
  }
#else
  char *buffer,*b,*p;
  assert(chars);
  NSMutableArray *r = [NSMutableArray arrayWithCapacity:8];
  buffer = b = (char*)malloc(1+strlen(chars));
  strcpy(buffer,chars);
  while ((p=strsep(&b," "))) {
    if (*p) [r addObject:@(p)];
  }  
  free((void*)buffer);
#endif
  return [NSArray arrayWithArray:r];
}

/* ---------------------------------------------------------------- */

NSArray* YarrayFromCharsQ(const char* string) // '' separates items
{
  char *buffer,*b,*p;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",__func__,string);
#endif
  assert(string);

  NSMutableArray *ary = [NSMutableArray arrayWithCapacity:8];
  buffer = b = (char*)malloc(1+strlen(string));
  strcpy(buffer,string);
  while (*b) {
    while ((*b) && (*b != '\'')) b++;  // find first quote
    if (!(*b)) break;
    p = ++b;
    while ((*b) && (*b != '\'')) b++;  // find second quote
    if (!(*b)) break;
    *b = '\0'; b++;                    // terminate sub-string
    [ary addObject:@(p)];       // append to array
  }  
  free((void*)buffer);
  
  return [NSArray arrayWithArray:ary];
}

/* ---------------------------------------------------------------- */

NSArray* YarrayFromFloats(const float* values,int n)
{
  assert(values);
  NSMutableArray *ary = [NSMutableArray arrayWithCapacity:n];
  for (int i=0; i<n; i++) {
    [ary addObject:[NSNumber numberWithFloat:values[i]]];
  }
  
  return [NSArray arrayWithArray:ary];
}

/* --- */

int YarrayToFloats(NSArray* array,float* values,int n)
{
  int i;
  
  for (i=0; i<[array count]; i++) {
    if (i == n) break;
    values[i] = [array[i] floatValue];
  }

  return i;
}

/* --- */

NSArray* YarrayFromInts(const int* values,int n)
{
  assert(values);
  NSMutableArray *ary = [NSMutableArray arrayWithCapacity:n];
  for (int i=0; i<n; i++) {
    [ary addObject:[NSNumber numberWithInt:values[i]]];
  }
  
  return [NSArray arrayWithArray:ary];
}

/* --- */

int YarrayToInts(NSArray* array,int* values,int n)
{
  int i;
  
  for (i=0; i<[array count]; i++) {
    if (i == n) break;
    values[i] = [array[i] intValue];
  }

  return i;
}

/* ---------------------------------------------------------------- */
#pragma mark "PropertyList"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

id YplistWithContentsOfFile(NSString *file) // 2019-05-01
{
  NSData *data = [NSData dataWithContentsOfFile:file];
  if (!data) return nil;
  
  id plist = [NSPropertyListSerialization
                propertyListWithData:data
                options:NSPropertyListImmutable
                format:nil error:nil];
  return plist;
}

/* --- */

BOOL YplistWriteToFile(id plist,NSString *file) // 2019-05-01
{
  BOOL r = [NSPropertyListSerialization
              propertyList:plist
              isValidForFormat:NSPropertyListXMLFormat_v1_0];
  if (r) {
    NSData *data = [NSPropertyListSerialization
                      dataWithPropertyList:plist
                      format:NSPropertyListXMLFormat_v1_0
                      options:0 error:nil];
    if (data) r = [data writeToFile:file atomically:NO];
  }
  return r;
}

/* ---------------------------------------------------------------- */
#pragma mark "Edit"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

void YeditSet(NSTextField *field,const char* text)
{
  NSString *string;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p)\n",__func__,field,text);
#endif

  string = [NSString stringWithCString:text encoding:NSASCIIStringEncoding];
  if ([NSThread isMainThread]) {
    [field setStringValue:string];
  } else {
    [field performSelectorOnMainThread:@selector(setStringValue:)
           withObject:string waitUntilDone:YES];
  }
}

/* ---------------------------------------------------------------- */

char* YeditGet(NSTextField *field,char* text,int bufsize)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p): mT=%d\n",__func__,field,text,[NSThread isMainThread]);
#endif
  __block NSString *string=nil;        // 2017-12-08

  if ([NSThread isMainThread]) {
    string = [field stringValue];
  } else {
    dispatch_sync(dispatch_get_main_queue(),^(void) {
      string = [field stringValue];
    });
  }
  assert(string);
  return Ystr2chr(string,text,bufsize);
}

/* ---------------------------------------------------------------- */

void YeditColor(NSTextField* field,float r,float g,float b,float a)
{  
  NSColor *color = [NSColor colorWithCalibratedRed:r green:g blue:b alpha:a];
  if ([NSThread isMainThread]) {
    [field setTextColor:color]; 
  } else {
    [field performSelectorOnMainThread:@selector(setTextColor:)
           withObject:color waitUntilDone:YES];
  }
}

/* ---------------------------------------------------------------- */
#pragma mark "Popup"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

NSArray* YpopupFromXML(NSPopUpButton* popup,NSString* name)
{
  NSString *file = [[NSBundle mainBundle] pathForResource:name ofType:@"xml"];
  NSArray *ary = [NSArray arrayWithContentsOfFile:file];
  YpopupFromArray(popup,ary);
  
  return ary;
}

/* --- */

void YpopupFromArray(NSPopUpButton* popup,NSArray* ary)
{
  assert([NSThread isMainThread]);
  
  [popup addItemsWithTitles:ary];
  NSMenu *menu = [popup menu];
  for (int i=0; i<[menu numberOfItems]; i++) {
    NSMenuItem *item = [menu itemAtIndex:i];  
    NSRange range = [[item title] rangeOfString:@"-empty" 
                                  options:NSCaseInsensitiveSearch];
    if (range.location != NSNotFound) {
      [item setHidden:YES];
    }
  }
}

/* ---------------------------------------------------------------- */

void YpopupSelectItem(NSPopUpButton* popup,int item)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d)\n",__func__,popup,item);
#endif

  if ([NSThread isMainThread]) {
    [popup selectItemAtIndex:item];
  } else {
    dispatch_sync(dispatch_get_main_queue(),^(void) {
      [popup selectItemAtIndex:item];
    });
  }
}

/* ---------------------------------------------------------------- */

void YpopupSetTitle(NSPopUpButton* popup,const char* title)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%s)\n",__func__,popup,title);
#endif

  if ([NSThread isMainThread]) {
    [popup setTitle:@(title)];
  } else {
    [popup performSelectorOnMainThread:@selector(setTitle:)
           withObject:@(title) waitUntilDone:YES];
  }
}

/* ---------------------------------------------------------------- */

void YpopupItemRename(NSPopUpButton* popup,int i,const char* title)
{
  if ([NSThread isMainThread]) {
    [popup removeItemAtIndex:i];
    [popup insertItemWithTitle:@(title) atIndex:i];
    [popup selectItemAtIndex:i];
  } else {
    dispatch_sync(dispatch_get_main_queue(),^(void) {
      YpopupItemRename(popup,i,title);
    });
  }
}

/* ---------------------------------------------------------------- */

void YpopupItemEnable(NSPopUpButton* popup,int i,BOOL enable)
{
  if ([NSThread isMainThread]) {
    [[popup itemAtIndex:i] setEnabled:enable];
  } else {
    dispatch_sync(dispatch_get_main_queue(),^(void) {
      YpopupItemEnable(popup,i,enable);
    });
  }
}

/* ---------------------------------------------------------------- */
#pragma mark "Control"
/* ---------------------------------------------------------------- */

void YcontrolEnable(NSControl* control,BOOL enable)
{
  if ([NSThread isMainThread]) {
    [control setEnabled:enable];
  } else {
    dispatch_sync(dispatch_get_main_queue(),^(void) {
      [control setEnabled:enable];
    });
  }
}

/* ---------------------------------------------------------------- */
#pragma mark "View"
/* ---------------------------------------------------------------- */

void YviewNeedsDisplay(NSView* view)
{
  if ([NSThread isMainThread]) {
    [view setNeedsDisplay:YES];
  } else {
    dispatch_async(dispatch_get_main_queue(),^(void) {
      [view setNeedsDisplay:YES];
    });
  }
}

/* ---------------------------------------------------------------- */
#pragma mark "Window"
/* ---------------------------------------------------------------- */

void YwindowTitle(NSWindow* window,const char* title)
{
  if ([NSThread isMainThread]) {       // execute on mainThread
    [window setTitle:@(title)];
  } else {
    [window performSelectorOnMainThread:@selector(setTitle:)
            withObject:@(title) waitUntilDone:NO];
  }
}

/* ---------------------------------------------------------------- */

void YwindowFrameSet(NSWindow* window,const char* name)
{
  assert([NSThread isMainThread]);
  [window setFrameUsingName:@(name)];
}

/* ---------------------------------------------------------------- */

void YwindowFrameSave(NSWindow* window,const char* name)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%s) isVisible=%d\n",__func__,window,name,[window isVisible]);
#endif
  assert([NSThread isMainThread]);

  if ([window isVisible]) {
    [window saveFrameUsingName:@(name)];
    [[NSUserDefaults standardUserDefaults] synchronize];
  }
}

/* ---------------------------------------------------------------- */
#pragma mark "Menu"
/* ---------------------------------------------------------------- */

void YmenuEnableTitles(const char* list,BOOL e)
{
  char *b,*buffer,*p;
  NSMenuItem *item=nil;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%d)\n",__func__,list,(int)e);
#endif
  assert([NSThread isMainThread]);
  
  buffer = b = (char*)malloc(1+strlen(list));
  strcpy(buffer,list);
  NSMenu *menu = [NSApp mainMenu];
  while ((p=strsep(&b,","))) {
    if (*p) {
      item = [menu itemWithTitle:@(p)];
      if (e) [item setEnabled:e];
      menu = [item submenu];
    }
  }
  [item setEnabled:e];
  free((void*)buffer);
}

/* ---------------------------------------------------------------- */

NSMenuItem* YmenuItem(const char* list)
{
  char *b,*buffer,*p;
  NSMenuItem *item=nil;
  
  assert([NSThread isMainThread]);
  buffer = b = (char*)malloc(1+strlen(list));
  strcpy(buffer,list);
  NSMenu *menu = [NSApp mainMenu];
  while ((p=strsep(&b,","))) {
    if (*p) {
      item = [menu itemWithTitle:@(p)];
      menu = [item submenu];
    }
  }  
  free((void*)buffer);

  return item;
}

/* ---------------------------------------------------------------- */
#pragma mark "Image"
/* ---------------------------------------------------------------- */

NSImage* YimageFile(NSImageView *view,const char* file)
{
  NSString *string;
  NSImage  *image;                     // not MT-safe
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%s)\n",__func__,view,file);
#endif

  if (!file) return nil;
  
  string = [NSString stringWithCString:file encoding:NSASCIIStringEncoding];
  image  = [[NSImage alloc] initWithContentsOfFile:string];

  if (view) {  
    if ([NSThread isMainThread]) {
      [view setImage:image];
    } else {
      [view performSelectorOnMainThread:@selector(setImage:)
            withObject:image waitUntilDone:YES];   
    }
  }
  return [image autorelease];
}

/* ---------------------------------------------------------------- */

NSImage* YimageData(NSImageView *view,const u_int *data,int w,int h)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p,%d,%d)\n",__func__,view,data,w,h);
#endif
  
  NSBitmapImageRep *bitmap = [NSBitmapImageRep alloc];

  bitmap = [bitmap initWithBitmapDataPlanes:NULL
                   pixelsWide:w
                   pixelsHigh:h
                   bitsPerSample:8
                   samplesPerPixel:3
                   hasAlpha:NO
                   isPlanar:NO
                   colorSpaceName:NSDeviceRGBColorSpace
                   bytesPerRow:sizeof(u_int)*w
                   bitsPerPixel:32];
  memcpy([bitmap bitmapData],data,sizeof(u_int)*w*h);
  NSImage *image = [[NSImage alloc] init];
  [image addRepresentation:bitmap];
  
  if ([NSThread isMainThread]) {
    [view setImage:image];
  } else {
    [view performSelectorOnMainThread:@selector(setImage:)
          withObject:image waitUntilDone:YES];   
  }
  [bitmap release];
  
  return [image autorelease];
}

/* ---------------------------------------------------------------- */
#pragma mark "Preferences"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

BOOL YprefSetupShared(void)
{
  BOOL r=TRUE;
  NSString *exec,*path;
  NSDictionary *attr;
  
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  
  NSNumber *perm = [NSNumber numberWithShort:((7*64)+(7*8)+7)]; // rwx,rwx,rwx
  attr = [NSDictionary dictionaryWithObjectsAndKeys:perm,NSFilePosixPermissions,nil];
  exec = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleExecutable"];
  
  if (r) {                             // /Users/Shared/Library
    path = [NSString stringWithFormat:@"%s",SHARED_BASE];
    [fm createDirectoryAtPath:path withIntermediateDirectories:NO
        attributes:attr error:nil];
  }
  if (r) {                             // ../Preferences
    path = [NSString stringWithFormat:@"%s/%s",SHARED_BASE,SHARED_PREF];
    r = [fm createDirectoryAtPath:path withIntermediateDirectories:YES
            attributes:attr error:nil];
  }
  if (r) {                             // ../Application Support
    path = [NSString stringWithFormat:@"%s/%s",SHARED_BASE,SHARED_APPS];
    [fm createDirectoryAtPath:path withIntermediateDirectories:NO
        attributes:attr error:nil];
    path = [NSString stringWithFormat:@"%s/%s/%@",SHARED_BASE,SHARED_APPS,exec];
    r = [fm createDirectoryAtPath:path withIntermediateDirectories:YES
            attributes:attr error:nil];
  }
  if (r) {                             // ../Logs
   path = [NSString stringWithFormat:@"%s/%s",SHARED_BASE,SHARED_LOGS];
   [fm createDirectoryAtPath:path withIntermediateDirectories:NO
       attributes:attr error:nil];
    path = [NSString stringWithFormat:@"%s/%s/%@",SHARED_BASE,SHARED_LOGS,exec];
    r = [fm createDirectoryAtPath:path withIntermediateDirectories:YES
            attributes:attr error:nil];
  }
  
  [fm release];

  return r;
}

/* ---------------------------------------------------------------- */

static NSMutableDictionary* YsharedRead(void)
{
  NSString *ident = [[NSBundle mainBundle] bundleIdentifier];
  NSString *file = [NSString stringWithFormat:@SHARED_FILE,SHARED_BASE,SHARED_PREF,ident];
  
  NSMutableDictionary *dict = [NSMutableDictionary dictionaryWithContentsOfFile:file];
  
  return dict;
}

/* --- */

static NSObject* YsharedGetObject(NSString* key)
{
  NSMutableDictionary *dict=nil;
  NSObject *object=nil;
  
  @synchronized([NSUserDefaults standardUserDefaults]) {
    dict = YsharedRead();
  }
  if (dict) {
    object = [dict objectForKey:key];
  }
  return object;
}

/* --- */

static BOOL YsharedWrite(NSMutableDictionary* dict)
{
  NSString *ident = [[NSBundle mainBundle] bundleIdentifier];
  NSString *file = [NSString stringWithFormat:@SHARED_FILE,SHARED_BASE,SHARED_PREF,ident];

  BOOL r = [dict writeToFile:file atomically:NO];
  if (r) {
    YfilePermissions([file UTF8String],6,6,6);  // rw-,rw-,rw-
  }
  
  return r;
}

/* --- */

static BOOL YsharedPutObject(NSString* key,NSObject* object)
{
  BOOL r=FALSE;
  assert(object);
  
  @synchronized([NSUserDefaults standardUserDefaults]) {
    NSMutableDictionary *dict = YsharedRead();
    if (!dict) dict = [NSMutableDictionary dictionaryWithCapacity:1];
    assert(dict);
    [dict setObject:object forKey:key];
    r = YsharedWrite(dict);
  }
  return r;
}

#undef SHARED_FILE

/* ---------------------------------------------------------------- */

void YprefPutInt(const char* key,int value,BOOL shared)
{
#if (DEBUG > 1)
  NSLog(@"%s(%s,%d,%d)",__func__,key,value,(int)shared);
#endif
  if (shared) {
    if (!YsharedPutObject(@(key),[NSNumber numberWithInt:value])) {
      NSLog(@"%s(%s,%d) failed",__func__,key,value);
    }
  } else {
    [[NSUserDefaults standardUserDefaults] setInteger:value forKey:@(key)];
  }
}

/* --- */

int YprefGetInt(const char* key,int value,BOOL shared)
{
  NSNumber *number;

  if (shared) {
    number = (NSNumber*)YsharedGetObject(@(key));
  } else {
    number = [[NSUserDefaults standardUserDefaults] objectForKey:@(key)];
  }
  if (number != nil) value = [number intValue];
  
  return value;
}

/* ---------------------------------------------------------------- */

void YprefPutDouble(const char* key,double value,BOOL shared)
{
  if (shared) {
    if (!YsharedPutObject(@(key),[NSNumber numberWithDouble:value])) {
      NSLog(@"%s(%s,%f) failed",__func__,key,value);
    }
  } else {
    [[NSUserDefaults standardUserDefaults] setDouble:value forKey:@(key)];
  }
}

/* --- */

double YprefGetDouble(const char* key,double value,BOOL shared)
{
  NSNumber *number;

  if (shared) {
    number = (NSNumber*)YsharedGetObject(@(key));
  } else {
    number = [[NSUserDefaults standardUserDefaults] objectForKey:@(key)];
  }
  if (number != nil) value = [number doubleValue];

  return value;
}

/* ---------------------------------------------------------------- */

void YprefPutChar(const char* key,const char* value,BOOL shared)
{
  YprefPutString(@(key),@(value),shared);
}

/* --- */

char* YprefGetChar(const char* key,const char* defstr,char* value,int len,BOOL shared)
{
  NSString *string = YprefGetString(@(key),@(defstr),shared);
  Ystr2chr(string,value,len);

  return value;
}

/* ---------------------------------------------------------------- */

void YprefPutString(NSString* key,NSString* value,BOOL shared)
{
  if (shared) {
    if (!YsharedPutObject(key,value)) {
      NSLog(@"%s(%@,%@) failed",__func__,key,value);
    }
  } else {
    [[NSUserDefaults standardUserDefaults] setObject:value forKey:key];
  }
}

/* --- */

NSString* YprefGetString(NSString* key,NSString* defstr,BOOL shared)
{
  NSString *string;
  
  if (shared) {
    string = (NSString*)YsharedGetObject(key);
  } else {
    string = [[NSUserDefaults standardUserDefaults] objectForKey:key];
  }
  if (string) return string;
  return defstr;
}

/* ---------------------------------------------------------------- */

void YprefPutArray(const char* key,NSArray* array,BOOL shared)
{
  if (shared) {
    if (!YsharedPutObject(@(key),array)) {
      NSLog(@"%s(%s,%p) failed",__func__,key,array);
    }
  } else {
    [[NSUserDefaults standardUserDefaults] setObject:array forKey:@(key)];
  }
}

/* --- */

NSArray* YprefGetArray(const char* key,BOOL shared)
{
  NSArray *array;
  
  if (shared) {
    array = (NSArray*)YsharedGetObject(@(key));
  } else {
    array = [[NSUserDefaults standardUserDefaults] objectForKey:@(key)];
  }
  return array;
}

/* ---------------------------------------------------------------- */

NSString* YsharedLogPath(void)
{
  return [NSString stringWithFormat:@"%s/%s",SHARED_BASE,SHARED_LOGS];
}

/* ---------------------------------------------------------------- */
#pragma mark "Alert,Sheet,Notification"
/* ---------------------------------------------------------------- */

// NSAlertFirstButtonReturn=1000
// NSAlertSecondButtonReturn=1001
// NSAlertThirdButtonReturn=1002

NSInteger YalertPanel(const char* title,const char* message,int level,
                const char* but1,const char* but2,const char* but3)
{
  assert([NSThread isMainThread]);

  NSAlert *alert = [[NSAlert alloc] init];
  
#if (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12) 
  if (level) [alert setAlertStyle:NSAlertStyleCritical];
#else
  if (level) [alert setAlertStyle:NSCriticalAlertStyle];
#endif
  [alert setMessageText:@(title)];
  [alert setInformativeText:@(message)];
  if (but1) [alert addButtonWithTitle:@(but1)];
  if (but2) [alert addButtonWithTitle:@(but2)];
  if (but3) [alert addButtonWithTitle:@(but3)];
  
  NSInteger i = [alert runModal]; // blocks the main-thread (application modal)
  [alert release];
                                      
  return i;
}

/* ---------------------------------------------------------------- */

BOOL YalertQuestion(const char* title,const char* message,int level,
                    const char* okbut,const char* ccbut)
{ // Note: this blocks the main-thread (application modal)
  
  NSInteger i = YalertPanel(title,message,level,okbut,ccbut,NULL);
  return ((i == NSAlertFirstButtonReturn) ? YES : NO);
}

/* ---------------------------------------------------------------- */

void YsheetNotify(NSWindow* window,const char* mess,const char* info)
{ // keeps the main-thread (timers) running
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%s)\n",__func__,mess,info);
#endif
  assert([NSThread isMainThread]);

  assert(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_9);
  NSAlert *sheet = [[NSAlert alloc] init];  // available since 10.9 (Mavericks)
  if (mess) [sheet setMessageText:@(mess)];
  if (info) [sheet setInformativeText:@(info)];
  [sheet addButtonWithTitle:@"OK"];
  [sheet beginSheetModalForWindow:window    // 2015-02-24
         completionHandler:^(NSModalResponse returnCode)
    {
      [sheet release];
    }
  ];
}

/* ---------------------------------------------------------------- */

void YsheetError(NSWindow* window,const char* title,const char* text)
{
#if (DEBUG > 0)
  if (![NSThread isMainThread]) {
    fprintf(stderr,"%s(%s) called from secondary thread\n",__func__,text);
  }
#endif
  NSDictionary *dict = [NSDictionary dictionaryWithObjectsAndKeys:
                        @(title),NSLocalizedDescriptionKey,
                        @(text),NSLocalizedRecoverySuggestionErrorKey,
                        nil];
  NSError *error = [NSError errorWithDomain:@"myErrorDomain" code:0 userInfo:dict];
  [window presentError:error modalForWindow:window   // window modal
          delegate:nil didPresentSelector:nil contextInfo:NULL];
}

/* ---------------------------------------------------------------- */

int YnotifyNotice(const char* title,const char* message,float timeout,int level)
{
  SInt32        err,cfl;
  CFStringRef   tstring,mstring=NULL;
  
  switch (abs(level)) {
    case 0:  cfl = kCFUserNotificationNoteAlertLevel; break;
    case 1:  cfl = kCFUserNotificationCautionAlertLevel; break;
    default: cfl = kCFUserNotificationStopAlertLevel; break;
  }

  tstring = CFStringCreateWithCString(kCFAllocatorDefault,title,kCFStringEncodingASCII);
  mstring = CFStringCreateWithCString(kCFAllocatorDefault,message,kCFStringEncodingASCII);
  // Note: rounds-up to next 30 seconds, 0=infinite
  err = CFUserNotificationDisplayNotice(timeout,
          cfl | kCFUserNotificationNoDefaultButtonFlag,
          NULL,NULL,NULL,tstring,mstring,CFSTR("OK"));
   
  CFRelease(tstring); if (mstring) CFRelease(mstring);

  return err;
}

/* ---------------------------------------------------------------- */
#pragma mark "Mutex,Semaphore,CriticalSection"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

Ymutex YmutexCreate(const char* name)
{
  NSRecursiveLock *lock = [[NSRecursiveLock alloc] init];

  [lock setName:@(name)];

  return (Ymutex)lock;
}

/* --- */

BOOL YmutexLock(Ymutex mutex,double timeout)
{
  NSRecursiveLock *lock = (NSRecursiveLock*)mutex;

  if (timeout > 0.0) {                 // timeout interval
    NSDate *date = [NSDate dateWithTimeIntervalSinceNow:timeout];
    return [lock lockBeforeDate:date];
  } else
  if (timeout < 0.0) {                 // infinite wait
    [lock lock];
    return YES;
  }
  
  return [lock tryLock];               // poll
}

/* --- */

void YmutexRelease(Ymutex mutex)
{
  [(NSRecursiveLock*)mutex unlock];
}

/* ---------------------------------------------------------------- */

Yglobal YglobalCreate(const char* path)
{
  NSDistributedLock *lock = [[NSDistributedLock alloc] initWithPath:@(path)];

  return (Yglobal)lock;
}

/* --- */

BOOL YglobalLock(Yglobal mutex,int timeout,int delta)
{
  BOOL r=NO;
  
  assert((timeout <= 0) || (delta > 0));
  
  do { 
    r = [(NSDistributedLock*)mutex tryLock]; if (r) break;
    timeout -= delta; 
    if (timeout > 0) Ysleep(delta);
  } while (timeout > 0);
  
  return r;
}

/* --- */

void YglobalRelease(Yglobal mutex)
{
  [(NSDistributedLock*)mutex unlock];
}

/* ---------------------------------------------------------------- */

Ysemaphore* YsemaphoreCreate(const char* name)
{
  Ysemaphore *sem;
  
  sem = (Ysemaphore*)malloc(sizeof(Ysemaphore));
  sem->lock = [[NSLock alloc] init];
  [sem->lock setName:@(name)];
  sem->count = 0;
  
  return sem;
}

/* --- */

int YsemaphoreInc(Ysemaphore* sem)
{
  return YsemaphoreAdd(sem,+1); 
}

/* --- */

int YsemaphoreDec(Ysemaphore* sem)
{
  return YsemaphoreAdd(sem,-1); 
}

/* --- */

int YsemaphoreAdd(Ysemaphore* sem,int add)
{
  int count;
  
  [sem->lock lock];
  sem->count += add; count = sem->count;
  [sem->lock unlock];
  
  return count;
}

/* --- */

int YsemaphoreGet(Ysemaphore* sem)
{
  int count;
  
  [sem->lock lock];
  count = sem->count;
  [sem->lock unlock];

  return count;
}

/* --- */

int YsemaphoreIncLimit(Ysemaphore* sem,int limit)
{                                      
  int count;
  
  assert(limit > 0);

  while (1) {                          // Warning: no timeout
    [sem->lock lock];
    if (sem->count < limit) {
      sem->count += 1; break;
    }
    [sem->lock unlock];
    Ysleep(250);
  }
  count = sem->count;
  [sem->lock unlock];
  assert(count >  0);
  assert(count <= limit);

  return count;  
}

/* ---------------------------------------------------------------- */
#pragma mark "File Operations"
/* ---------------------------------------------------------------- */

BOOL YfileExists(const char* file)     // any file or directory
{
  BOOL r;
  
  if (*file == '\0') return YES;       // Note: special case
  
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  NSString *path = [fm stringWithFileSystemRepresentation:file length:strlen(file)];
  r = [fm fileExistsAtPath:path];
  [fm release];
  
  return r;
}

/* ---------------------------------------------------------------- */

BOOL YfileReadable(const char* file)   // any file
{
  BOOL r;
    
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  NSString *path = [fm stringWithFileSystemRepresentation:file length:strlen(file)];
  r = [fm isReadableFileAtPath:path];
  [fm release];
  
  return r;
}

/* --- */

BOOL YpathReadable(const char* file)   // directory only
{
  BOOL r=NO,e,d;
    
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  NSString *path = [fm stringWithFileSystemRepresentation:file length:strlen(file)];
  e = [fm fileExistsAtPath:path isDirectory:&d];
  if (e && d) {                        // exists and is directory
    r = [fm isReadableFileAtPath:path];
  }
  [fm release];
  
  return r;
}

/* ---------------------------------------------------------------- */

BOOL YfileWritable(const char* file)
{
  BOOL r;

  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  NSString *path = [fm stringWithFileSystemRepresentation:file length:strlen(file)];
  r = [fm isWritableFileAtPath:path];
  [fm release];
  
  return r;
}

/* --- */

BOOL YpathWritable(const char* file)   // directory only
{
  BOOL r=NO,e,d;

  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  NSString *path = [fm stringWithFileSystemRepresentation:file length:strlen(file)];
  e = [fm fileExistsAtPath:path isDirectory:&d];
  if (e && d) {                        // exists and is directory
    r = [fm isWritableFileAtPath:path];
  }
  [fm release];
  
  return r;
}

/* ---------------------------------------------------------------- */

BOOL YpathCreate(const char* path,BOOL intermediate)
{
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  NSString *string = [fm stringWithFileSystemRepresentation:path length:strlen(path)];
  BOOL r = [fm createDirectoryAtPath:string 
               withIntermediateDirectories:intermediate
               attributes:nil error:NULL];
  [fm release];
  
  return r;
}

/* ---------------------------------------------------------------- */

BOOL YfileRemove(const char* path)
{
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  NSString *string = [fm stringWithFileSystemRepresentation:path length:strlen(path)];
  BOOL r = [fm removeItemAtPath:string error:NULL];
  [fm release];
  
  return r;
}

/* ---------------------------------------------------------------- */

void YfileTrash(const char* filename)
{
  NSURL *url = [NSURL fileURLWithPath:@(filename)];
  [[NSWorkspace sharedWorkspace] recycleURLs:[NSArray arrayWithObject:url]
                                 completionHandler:nil];
}

/* ---------------------------------------------------------------- */

BOOL YfilePermissions(const char* file,short u,short g,short o)
{
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  NSNumber *perm = [NSNumber numberWithShort:((u*64)+(g*8)+o)];
  NSDictionary *dict = [NSDictionary dictionaryWithObjectsAndKeys:perm,NSFilePosixPermissions,nil];
  BOOL r = [fm setAttributes:dict ofItemAtPath:@(file) error:nil];
  [fm release];
  
  return r;
}

/* ---------------------------------------------------------------- */

BOOL YfileCopy(const char* src, const char* trg)
{
  NSFileManager *fm = [[NSFileManager alloc] init]; // MT-safe
  BOOL r = [fm copyItemAtPath:@(src) toPath:@(trg) error:NULL];
  [fm release];
  
  return r;
}

/* ---------------------------------------------------------------- */
#pragma mark "Miscellaneous"
/* ---------------------------------------------------------------- */

void Ysleep(int msec)                  /* typically 0.2 msec over */
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d)\n",__func__,msec);
#endif
  [NSThread sleepForTimeInterval:(double)msec/1000.0]; 
}

/* ---------------------------------------------------------------- */

double YsecSince(NSDate *date)
{
  return -[date timeIntervalSinceNow];
}

/* --- */

double YmsSince(NSDate *date)
{
  return -1000.0*[date timeIntervalSinceNow];
}

/* ---------------------------------------------------------------- */

NSTask* Ytask(const char* cmd,const char* par)
{
#if (DEBUG > 0)
  { char *p = strstr(cmd,".app");
    fprintf(stderr,"%s(%s,%s)\n",__func__,(p) ? p : cmd,par); }
#endif

  NSString *path = @(cmd);
  NSString *pars = @(par);
  NSArray *args = [pars componentsSeparatedByString:@" "];
  NSTask *task = [NSTask launchedTaskWithLaunchPath:path arguments:args];
  
  return task;
}

/* ---------------------------------------------------------------- */

void Ywww(const char* address)
{
  NSString *string = @(address);
  NSURL *url = [NSURL URLWithString:string];
  [[NSWorkspace sharedWorkspace] openURL:url];
}

/* ---------------------------------------------------------------- */

NSArray* YdirectoryURL(NSUInteger type)
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%lu)\n",__func__,(u_long)type);
#endif
  const NSUInteger mask=NSLocalDomainMask|NSUserDomainMask;

  NSFileManager *fm = [[NSFileManager alloc] init];  // MT-safe
  NSArray *array = [fm URLsForDirectory:type inDomains:mask];
  for (NSURL *url in array) NSLog(@"%@",[url path]);
  [fm release];
  
  return array;
}

/* ---------------------------------------------------------------- */

void YosVersion(int* vMaj,int* vMin,int* vFix)
{
#if (MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_9) 
  NSOperatingSystemVersion osv;        // since 10.10
  osv = [[NSProcessInfo processInfo] operatingSystemVersion];
  *vMaj = (int)osv.majorVersion;
  *vMin = (int)osv.minorVersion;
  *vFix = (int)osv.patchVersion;
#else
  *vMaj = *vMin = *vFix = 0;
  NSString *file = @"/System/Library/CoreServices/SystemVersion.plist";
  NSDictionary *dict = [NSDictionary dictionaryWithContentsOfFile:file];
  NSString *version = [dict objectForKey:@"ProductVersion"];
  if (version) {
    (void)sscanf([version UTF8String],"%d.%d.%d",vMaj,vMin,vFix);
  }
#endif
}

/* ---------------------------------------------------------------- */
#pragma mark "CoreText"
/* ---------------------------------------------------------------- */

CTFontRef CtextGetFont(const char* family,float size)
{
  NSDictionary *fontAttr = [NSDictionary dictionaryWithObjectsAndKeys:
    @(family),(NSString*)kCTFontFamilyNameAttribute,
    [NSNumber numberWithFloat:size],(NSString*)kCTFontSizeAttribute,
    nil];
  CTFontDescriptorRef fontDesc = CTFontDescriptorCreateWithAttributes((CFDictionaryRef)fontAttr);
  CTFontRef font = CTFontCreateWithFontDescriptor(fontDesc,0.0,NULL);
  CFRelease(fontDesc);

  return font;                   // Note: needs CFRelease()
}

/* ---------------------------------------------------------------- */

CTLineRef CtextGetLine(const char* text,CTFontRef font)
{
  CFStringRef keys[] = { kCTFontAttributeName };
  CFTypeRef values[] = { font };
  CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault,text,kCFStringEncodingASCII);

  CFDictionaryRef attributes =
    CFDictionaryCreate(kCFAllocatorDefault, (const void**)&keys,
        (const void**)&values, sizeof(keys) / sizeof(keys[0]),
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
 
  CFAttributedStringRef attrString =
      CFAttributedStringCreate(kCFAllocatorDefault, string, attributes);
  CFRelease(string);
  CFRelease(attributes);
 
  CTLineRef line = CTLineCreateWithAttributedString(attrString);
  CFRelease(attrString);
  
  return line;
}

/* ---------------------------------------------------------------- */

static CTLineRef CtextCreateColorLine(const char* text,CTFontRef font,
                               float r,float g,float b,float a)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%p,%.3f,%.3f,%.3f,%.3f)\n",__func__,text,font,r,g,b,a);
#endif

  NSColor *nscol = [NSColor colorWithCalibratedRed:r green:g blue:b alpha:a];
  CGColorRef color = [nscol CGColor];
  CFStringRef keys[] = { kCTFontAttributeName,kCTForegroundColorAttributeName };
  CFTypeRef values[] = { font,color };
  CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault,text,kCFStringEncodingASCII);

  CFDictionaryRef attributes =
    CFDictionaryCreate(kCFAllocatorDefault, (const void**)&keys,
        (const void**)&values, sizeof(keys) / sizeof(keys[0]),
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
 
  CFAttributedStringRef attrString =
      CFAttributedStringCreate(kCFAllocatorDefault, string, attributes);
  CFRelease(string);
  CFRelease(attributes);
 
  CTLineRef line = CTLineCreateWithAttributedString(attrString);
  CFRelease(attrString);

  return line;
}

/* --- */

void CtextSetColorText(const char* text,CTFontRef font,CGContextRef c,
                       float x,float y,float r,float g,float b,float a)
{
  CTLineRef line = CtextCreateColorLine(text,font,r,g,b,a);
  CGContextSetTextPosition(c,x,y);
  CTLineDraw(line,c);
  CFRelease(line);
}

/* ---------------------------------------------------------------- */

void CtextSetText(const char* text,CTFontRef font,CGContextRef c,float x,float y)
{
  CTLineRef line = CtextGetLine(text,font);
  CGContextSetTextPosition(c,x,y);
  CTLineDraw(line,c);
  CFRelease(line);
}

/* ---------------------------------------------------------------- */

void CtextCenterText(const char* text,CTFontRef font,CGContextRef c,float x,float y)
{
  CTLineRef line = CtextGetLine(text,font);
  CGRect rect = CTLineGetImageBounds(line,c);
  CGContextSetTextPosition(c,x-rect.size.width/2,y-rect.size.height/2);
  CTLineDraw(line,c);
  CFRelease(line);
}

/* ---------------------------------------------------------------- */

void CtextRightText(const char* text,CTFontRef font,CGContextRef c,float x,float y)
{
  CTLineRef line = CtextGetLine(text,font);
  CGRect rect = CTLineGetImageBounds(line,c);
  CGContextSetTextPosition(c,x-rect.size.width,y-rect.size.height/2);
  CTLineDraw(line,c);
  CFRelease(line);
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
