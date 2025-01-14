/* ---------------------------------------------------------------- */
//
// Yutils.h
//
// Created by Christoph Birk on 2011-04-19
// Copyright 2011 Carnegie Observatories. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import <Cocoa/Cocoa.h>

/* ---------------------------------------------------------------- */

#define PREFUN       __func__

#define GLOBAL_QUEUE dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0)
#define MAIN_QUEUE   dispatch_get_main_queue()

/* ---------------------------------------------------------------- */
/* Utility functions */
/* ---------------------------------------------------------------- */

BOOL          YbundleInfo      (char**,char**,char**,char**,char**,char**);
const char*   YbundleResource  (const char*,const char*);
NSDictionary* YbundleDictionary(const char*);
NSArray*      YbundleArray     (const char*);
char*         YappSupportCreate(char**,BOOL);
BOOL          YappNapDisabled  (void);

char*       Ystr2chr(NSString*,char*,int);
const char* Ystr2tmp(NSString*) __OSX_AVAILABLE_BUT_DEPRECATED(__MAC_10_6,__MAC_10_13,__IPHONE_NA,__IPHONE_NA);
NSString*   Ychr2str(const char*) __OSX_AVAILABLE_BUT_DEPRECATED(__MAC_10_6,__MAC_10_10,__IPHONE_NA,__IPHONE_NA);

NSArray*  YarrayFromChars  (const char*);
NSArray*  YarrayFromCharsQ (const char*);
NSArray*  YarrayFromFloats (const float*,int);
int       YarrayToFloats   (NSArray*,float*,int);
NSArray*  YarrayFromInts   (const int*,int);
int       YarrayToInts     (NSArray*,int*,int);

id   YplistWithContentsOfFile (NSString *file);
BOOL YplistWriteToFile        (id plist,NSString *file);

void         YeditSet    (NSTextField*,const char*);
char*        YeditGet    (NSTextField*,char*,int);
void         YeditColor  (NSTextField*,float,float,float,float);

NSArray* YpopupFromXML    (NSPopUpButton*,NSString*);
void     YpopupFromArray  (NSPopUpButton*,NSArray*);
void     YpopupSelectItem (NSPopUpButton*,int);
void     YpopupSetTitle   (NSPopUpButton*,const char*);
void     YpopupItemRename (NSPopUpButton* popup,int item,const char* title);
void     YpopupItemEnable (NSPopUpButton* popup,int i,BOOL enable);
void     YcontrolEnable   (NSControl*,BOOL);
void     YviewNeedsDisplay(NSView* view);

void YwindowTitle    (NSWindow*,const char*);
void YwindowFrameSet (NSWindow*,const char*);
void YwindowFrameSave(NSWindow*,const char*);

void        YmenuEnableTitles (const char*,BOOL);
NSMenuItem* YmenuItem         (const char*);

NSImage* YimageFile (NSImageView*,const char*);
NSImage* YimageData (NSImageView*,const u_int*,int,int);

BOOL      YprefSetupShared(void);
void      YprefPutInt     (const char*,int,BOOL);
int       YprefGetInt     (const char*,int,BOOL);
void      YprefPutDouble  (const char*,double,BOOL);
double    YprefGetDouble  (const char*,double,BOOL);
void      YprefPutChar    (const char*,const char*,BOOL);
char*     YprefGetChar    (const char*,const char*,char*,int,BOOL);
void      YprefPutString  (NSString*,NSString*,BOOL);
NSString* YprefGetString  (NSString*,NSString*,BOOL);
void      YprefPutArray   (const char*,NSArray*,BOOL);
NSArray*  YprefGetArray   (const char*,BOOL);
NSString* YsharedLogPath  (void);

NSInteger YalertPanel(const char*,const char*,int,const char*,const char*,const char*);
BOOL      YalertQuestion (const char*,const char*,int,const char*,const char*);
void      YsheetNotify   (NSWindow*,const char*,const char*);
void      YsheetError    (NSWindow*,const char*,const char*);
int       YnotifyNotice  (const char*,const char*,float,int);

typedef NSRecursiveLock* Ymutex;
Ymutex  YmutexCreate(const char*);
BOOL    YmutexLock(Ymutex,double);
void    YmutexRelease(Ymutex);

typedef NSDistributedLock* Yglobal;
Yglobal YglobalCreate(const char*);
BOOL    YglobalLock(Yglobal,int,int);
void    YglobalRelease(Yglobal);

typedef struct { NSLock *lock; int count; } Ysemaphore; 
Ysemaphore* YsemaphoreCreate(const char*);
int YsemaphoreInc(Ysemaphore*);
int YsemaphoreDec(Ysemaphore*);
int YsemaphoreAdd(Ysemaphore*,int);
int YsemaphoreGet(Ysemaphore*);
int YsemaphoreIncLimit(Ysemaphore*,int);

BOOL YfileExists      (const char*);
BOOL YfileReadable    (const char*);
BOOL YpathReadable    (const char*);
BOOL YfileWritable    (const char*);
BOOL YpathWritable    (const char*);
BOOL YpathCreate      (const char*,BOOL);
BOOL YfileRemove      (const char*);
void YfileTrash       (const char*);
BOOL YfilePermissions (const char* file,short u,short g,short o);
BOOL YfileCopy        (const char* src, const char* trg);

void     Ysleep        (int);
double   YsecSince     (NSDate *date);
double   YmsSince      (NSDate *date);
NSTask*  Ytask         (const char*,const char*);
void     Ywww          (const char*);
NSArray* YdirectoryURL (NSUInteger);
void     YosVersion    (int* vMaj,int* vMin,int* vFix);

void CtextSetColorText(const char* text,CTFontRef font,CGContextRef c,
                       float x,float y,float r,float g,float b,float a);
CTFontRef CtextGetFont(const char* family,float size);
CTLineRef CtextGetLine(const char* text,CTFontRef font);
void      CtextSetText(const char*,CTFontRef,CGContextRef,float,float);
void      CtextCenterText(const char*,CTFontRef,CGContextRef,float,float);
void      CtextRightText(const char*,CTFontRef,CGContextRef,float,float);

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
