/* ---------------------------------------------------------------- */
//
//  EFW.m
//  ZWOFinder
//
//  Created by Christoph Birk on 12/17/18.
//  Copyright © 2018 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

#import  "EFW.h"
#import  "Yutils.h"

#include "EFW_filter.h"

/* ---------------------------------------------------------------- */

#define DEBUG        1

#undef  SIM_ONLY

/* ---------------------------------------------------------------- */

@implementation EFW
{
  ZWO *zwoServer;
  int identity,handle,count;
}

/* ---------------------------------------------------------------- */
#pragma mark "Class"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

// NOTE: there's no way to tell the wheels apart

+ (NSArray*)scan:(ZWO*)server
{
  int ident,num,ret=EFW_SUCCESS;
  EFW_INFO info;

  if (server) num = [server getNumEFW];
  else        num = EFWGetNum();
  if (num <= 0) return nil;
  
  NSMutableArray *a = [NSMutableArray arrayWithCapacity:1];
  for (int i=0; i<num; i++) {
    if (server) {                      // b0029
      ret = [server openEFW];
      if (ret != EFW_SUCCESS) continue;
      ret = [server getProperty];
      if (ret == EFW_SUCCESS) {
        info.ID = server.efw_id;
        info.slotNum = server.numSlots;
        strcpy(info.Name,[server.efwName UTF8String]);
      }
      [server closeEFW];
    } else {
      ret = EFWGetID(i,&ident);
      if (ret != EFW_SUCCESS) continue;
      ret = EFWOpen(ident);
      if (ret == EFW_SUCCESS) {
        EFWGetProperty(ident,&info);
        assert(info.ID == ident);
        EFWClose(ident);
      }
    }
    if (ret != EFW_SUCCESS) continue;
    // printf("%d %s %d\n",info.ID,info.Name,info.slotNum);
    NSDictionary *d = @{@"ID"      : @(info.ID),
                        @"Name"    : @(info.Name),
                        @"slotNum" : @(info.slotNum)};
    [a addObject:d];                   // b0029
  }
  
#if 0 // NOTE: just 1 product ID even with 2 wheels connected
  int n = EFWGetProductIDs(NULL);
  int *pids = malloc((1+n)*sizeof(int));
  int m = EFWGetProductIDs(pids);
  printf("n=%d, m=%d\n",n,m);
  assert(n == m);
  for (int i=0; i<n; i++) printf("\t%d\n",pids[i]);
  assert(pids[0] == 7937);
  free(pids);
#endif

  return a;
}

/* --- */

+ (NSString*)errorString:(int)err
{
  NSString *string=nil;
  
  switch (err) {
    case E_efw_error: string = @"ZWO/EFW: error"; break;
    case E_efw_timeout: string = @"ZWO/EFW: timeout"; break;
    // no Super to call
  }
  return string;
}

/* ---------------------------------------------------------------- */
#pragma mark "Instance"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (id)initWithServer:(ZWO*)server      // b0015
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,server);
#endif

  if (!(self = [super init])) return nil;

  identity = handle = -1;  
  assert(count == 0);
  zwoServer = [server retain]; 

  return self;
}

/* ---------------------------------------------------------------- */

- (void)dealloc
{
  [_filter release];
  [super dealloc];
}

/* ---------------------------------------------------------------- */

- (int)open
{
  int err=0,ret;
#if (DEBUG > 2)
  fprintf(stderr,"%s:%p ident=%d count=%d\n",PREFUN,self,identity,count);
#endif
  assert(identity >= 0);

  @synchronized(self) {
    if (handle == identity) {   // already open
      count++;
    } else {                    // not yet open
      assert(handle < 0);
      assert(count == 0);
      if (zwoServer) ret = [zwoServer openEFW]; 
      else           ret = EFWOpen(identity);
      if (ret == EFW_SUCCESS) {
        handle = identity;
        count = 1;
      } else {
        err = E_efw_error;
      } // open failed
    } // already open
  } // synch

  return err;
}

/* ---------------------------------------------------------------- */

- (void)close
{
  assert(identity >= 0);

  @synchronized(self) {
    if (count > 0) {
      assert(handle >= 0);
      count--;
      if (count == 0) {
        if (zwoServer) [zwoServer closeEFW];
        else           EFWClose(handle);
        handle = -1;
      }
    }
  }
}

/* ---------------------------------------------------------------- */

- (int)setup:(int)ident
{
  int ret;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,ident);
#endif

  identity = ident;           // before the first 'open'
  int err = [self open]; if (err) return err;
  assert(handle == ident);

  @synchronized(self) { EFW_INFO info;
    if (zwoServer) {
      ret = [zwoServer getProperty];
      if (!ret) {
        _numPos = zwoServer.numSlots;
      }
    } else {
      ret = EFWGetProperty(handle,&info);
      if (ret == EFW_SUCCESS) {
        // printf("%d %s %d\n",info.ID,info.Name,info.slotNum);
        assert(handle == info.ID);
        _numPos = info.slotNum;
      }
    }
    if (ret) err = E_efw_error;
  }

#if (DEBUG > 1)
  if (zwoServer) {
    ret = [zwoServer getDirection];
    printf("ret=%d, uni=%d\n",ret,zwoServer.uniDir);
  } else { bool unidir;
    ret = EFWGetDirection(handle,&unidir);
    printf("ret=%d, uni=%d\n",ret,unidir);
  }
#endif
  
  if (!err) [self updatePosition];

  [self close];
  assert(count == 0);
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)calibrate
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p: ident=%d, handle=%d\n",PREFUN,self,identity,handle);
#endif
  assert(identity >= 0);

  int err = [self open]; if (err) return err;
  
  @synchronized(self) { int ret;
    if (zwoServer) ret = [zwoServer calibrate];
    else           ret = EFWCalibrate(handle); 
    if (ret != EFW_SUCCESS) err = E_efw_error;
  }
  if (!err) err = [self waitForMove:40];  // 27 sec max.
  [self close];
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)updatePosition
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p: ident=%d, handle=%d, count=%d\n",PREFUN,self,identity,handle,count);
#endif
  assert(identity >= 0);

  int err = [self open]; if (err) return err;

  @synchronized(self) { int ret;
    if (zwoServer) {
      ret = [zwoServer getPosition];
      if (!ret) _curPos = zwoServer.position;
    } else { int pos;
      ret = EFWGetPosition(handle,&pos);
      if (!ret) _curPos = pos;
    }
    if (ret != EFW_SUCCESS) err = E_efw_error;
  }
  [self close];
  
  return err;
}

/* ---------------------------------------------------------------- */

- (int)waitForMove:(int)timeout
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d) ident=%d\n",PREFUN,timeout,identity);
#endif
  assert(identity >= 0);
  
  int err = [self open]; if (err) return err;

  NSDate *date = [NSDate date];
  while (!err) {
    [NSThread sleepForTimeInterval:1.0];
    err = [self updatePosition]; if (err) break;
    if (self.curPos >= 0) break;
    if (YsecSince(date) > timeout) err = E_efw_timeout;
  }
  [self close];

  return err;
}

/* ---------------------------------------------------------------- */

- (int)moveToPosition:(int)pos
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d): ident=%d\n",PREFUN,pos,identity);
#endif
  assert(identity >= 0);

  int err = [self open]; if (err) return err;

  @synchronized(self) { int ret;
    if (zwoServer) ret = [zwoServer setPosition:pos];
    else           ret = EFWSetPosition(handle,pos);
    if (ret != EFW_SUCCESS) err = E_efw_error;
  }
  if (!err) err = [self waitForMove:20];
  [self close];
  
  return err;
}

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
