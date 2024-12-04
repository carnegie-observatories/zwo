/* ---------------------------------------------------------------- */
//
// Tcpip.h
//
// Created by Christoph Birk on 2013-08-14
// Copyright 2013 Carnegie Observatories. All rights reserved.
//
// 2014-09-25  remove tcpip_errors[] global array
// 2015-04-29  add 'server' errors
// 2018-11-21  include "main.h" instead of "Prefix.pch"
// 2019-02-11  removed dependency on main.h
//
/* ---------------------------------------------------------------- */

@import Cocoa;  // #import <Cocoa/Cocoa.h>

/* ---------------------------------------------------------------- */

enum tcpip_errors_enum { E_tcpip_error=1000,
                         E_tcpip_protocol,   // client
                         E_tcpip_hostbyname,
                         E_tcpip_socket,
                         E_tcpip_connect,
                         E_tcpip_send,
                         E_tcpip_recv,
                         E_tcpip_nodata,
                         E_tcpip_timeout,
                         E_tcpip_open,
                         E_tcpip_lock,
                         E_tcpip_select,
                         E_tcpip_start,     // server 
                         E_tcpip_hostname,
                         E_tcpip_bind,
                         E_tcpip_listen,
                         E_tcpip_last
};

int     TCPIP_Wait4Char (int sock,char* c,int timeout); // [ms]
int     TCPIP_Send      (int sock,const char* command,size_t size);
ssize_t TCPIP_Recv      (int sock,char* answer,size_t size,int timeout);
int     TCPIP_Select    (int sock,int timeout);
BOOL    TCPIP_ReadyForWrite(int sock);

/* ---------------------------------------------------------------- */

@interface TCPIP_Connection : NSObject
{ // IDEA move into implementation -- requires modification of derived classes
  char     *host;
  int      port;
  BOOL     online;
  char     terminator;
  NSRecursiveLock *lock;
  int      lock_timeout,sock_timeout;
  int      sock;
  volatile int  count;
  volatile BOOL up_and_running;
}

@property (readonly,nonatomic) BOOL online;
@property (readonly)           volatile BOOL up_and_running;
@property (readonly,nonatomic) char *host;
@property (readonly,nonatomic) int  port,sock;

+ (NSString*)errorString:(int)err;
+ (int)createClientSocket:(const char*)hostname port:(u_short)port;

- (id)initWithHost:(const char*)hstr port:(int)pint online:(BOOL)on;
- (int)open;
- (int)open:(int)timeout;
- (void)close;
- (BOOL)mylock:(double)timeout;
- (int)request:(const char*)cmd response:(char*)res max:(int)len;
- (int)sendBytes:(const char*)bytes size:(size_t)size;
- (void)shutdown;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

@protocol TCPIP_ServerDelegate
- (char*)tcpip_handler:(const char*)command from:(NSString*)ipNumber socket:(int)msgsock;
@end

@interface TCPIP_Server : NSObject
{
  u_short port;
  id <TCPIP_ServerDelegate> delegate;
  int     sock;
  BOOL    shutdown;
}

@property (readwrite,nonatomic) float throttle;

+ (NSString*)errorString:(int)err;
+ (int)createServerSocket:(u_short)port;

- (id)initWithPort:(u_short)pin delegate:(id)del;
- (int)start;
- (int)stop;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
