/* ---------------------------------------------------------------- */
//
// Tcpip.m
//
// Created by Christoph Birk on 2013-08-14
// Copyright 2013 Carnegie Observatories All rights reserved.
//
// 2014-04-07  fixed locking around gethostbyname(),getprotobyname()
// 2014-09-25  +errorString
// 2015-03-17  E_tcpip_select
// 2015-04-29  +createServerSocket
// 2015-05-07  TCPIP_Server class
// 2015-05-11  -sendBytes
// 2015-05-18  TCPIP_ReadyForWrite
// 2016-01-27  open/lock instead of lock/open
// 2017-03-01  suppress SIGPIPE on client sockets
// 2017-04-11  fixed memory leak in run_server
// 2017-07-05  suppress SIGPIPE on server sockets
// 2018-04-09  CONNECTIONTIMEOUT on client sockets
// 2018-04-13  lowercase methods
//
/* ---------------------------------------------------------------- */

#define DEBUG           0              // debug level

/* ---------------------------------------------------------------- */

#import  "Tcpip.h"

#include <netdb.h>
#include <netinet/tcp.h>

#if (__clang_major__ > 5)
#include <sys/_select.h>
#endif

#if (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12)
#include <stdatomic.h>
#endif

/* ---------------------------------------------------------------- */

@implementation TCPIP_Connection

@synthesize online;
@synthesize up_and_running;
@dynamic    host,port,sock;

/* ---------------------------------------------------------------- */
#pragma mark "Class"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

+ (void)initialize
{
  if (self != [TCPIP_Connection class]) return; // prevent sub-classes
#if (DEBUG > 1)
  fprintf(stderr,"%s:%p\n",__func__,self);
#endif  
}

/* ---------------------------------------------------------------- */

+ (NSString*)errorString:(int)err
{
  NSString *string=nil;
  
  switch (err) {
    case E_tcpip_error: string = @"TCPIP: error"; break;
    case E_tcpip_protocol: string = @"TCPIP: unknown protocol"; break;
    case E_tcpip_hostbyname: string = @"TCPIP: hostbyname() failed"; break;
    case E_tcpip_socket: string = @"TCPIP: creating socket failed"; break;
    case E_tcpip_connect: string = @"TCPIP: connecting to socket failed"; break;
    case E_tcpip_send: string = @"TCPIP: writing to socket failed"; break;
    case E_tcpip_recv: string = @"TCPIP: reading from socket failed"; break;
    case E_tcpip_nodata: string = @"TCPIP: no data on socket"; break;
    case E_tcpip_timeout: string = @"TCPIP: timeout on socket"; break;
    case E_tcpip_open: string = @"TCPIP: opening socket failed"; break;
    case E_tcpip_lock: string = @"TCPIP: internal locking error"; break;
    case E_tcpip_select: string = @"TCPIP: select() error"; break;
    // no Super to call
  }
  return string;
}

/* ---------------------------------------------------------------- */

+ (int)createClientSocket:(const char*)hostname port:(u_short)port
{
  int                sock=0,err=0,protocol=0,on=1,to=9;
  struct sockaddr_in saddr;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%d)\n",__func__,hostname,(int)port);
#endif
  assert([TCPIP_Connection class] == self);

  memset((void*)&saddr,0,sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  
  @synchronized(self) {               // protects MT-unsafe functions
    struct hostent *hostp = gethostbyname(hostname); // IP address (not MT-safe)
    if (hostp == NULL) {
      err = E_tcpip_hostbyname;
    } else {
      memcpy(&saddr.sin_addr,hostp->h_addr_list[0],hostp->h_length);
    }
    struct protoent *proto = getprotobyname("tcp"); // protocol by name (not MT-safe)
    if (proto == NULL) {
      err = E_tcpip_protocol;
    } else {
      protocol = proto->p_proto;
    }
  } // synchronized
 
  if (!err) {                          // create socket
    for (int i=0; i<3; i++) {
      if (i > 0) [NSThread sleepForTimeInterval:0.3];
      if ((sock = socket(PF_INET,SOCK_STREAM,protocol)) < 0) {
        err = E_tcpip_socket; break;
      }
      (void)fcntl(sock,F_SETFD,FD_CLOEXEC);
      (void)setsockopt(sock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on)); // 2017-03-01
      (void)setsockopt(sock,IPPROTO_TCP,TCP_CONNECTIONTIMEOUT,&to,sizeof(to)); // 2018-04-09
      if (connect(sock,(struct sockaddr *)&saddr,sizeof(saddr)) < 0) {
        (void)close(sock);
        err = E_tcpip_connect; if (errno != EINTR) break;
      } else {
        err = 0; break;
      }
    } // endfor(retry)
  }
  
  return (err) ? -err : sock;
}

/* ---------------------------------------------------------------- */
#pragma mark "Instance"
/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (instancetype)initWithHost:(const char*)hstr port:(int)pint online:(BOOL)on
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%d,%d)\n",__func__,hstr,pint,on);
#endif

  if ((self = [super init]) == nil) return nil;

  host = (char*)malloc((1+strlen(hstr))*sizeof(char));
  strcpy(host,hstr);
  port = pint;
  online = on;
  count = 0;
  sock = -1;
  sock_timeout = 5;
  lock_timeout = sock_timeout+2;       // >sock_timeout 2014-12-09
  terminator = '\n';                   // default terminator
  
  lock = [[NSRecursiveLock alloc] init];
  NSString *name = [NSString stringWithFormat:@"tcpip-lock-%s-%d",host,port];
  [lock setName:name];

  up_and_running = TRUE;
  
  return self;
}

/* ---------------------------------------------------------------- */

- (BOOL)mylock:(double)timeout
{
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

/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (int)open:(int)timeout
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d): count=%d (%s:%d)\n",__func__,timeout,count,host,port);
#endif

  if (!up_and_running) return E_tcpip_error;

  if (![self mylock:timeout]) return E_tcpip_lock;
   
  if (count == 0) {
    assert(sock < 0);
    if (online) {
      sock = [TCPIP_Connection createClientSocket:host port:port];
    } else {
      [NSThread sleepForTimeInterval:0.020];
      sock = 0;
    }
  }
  if (sock >= 0) count += 1;
#if (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12)
  atomic_thread_fence(memory_order_relaxed); // 2018-11-05
#else
  OSMemoryBarrier();                   // deprecated 10.12 (Sierra)
#endif
#if (DEBUG > 1)
  fprintf(stderr,"%s(): count=%d, sock=%d\n",__func__,count,sock);
#endif
  [lock unlock];                       // should have an implicit memory barrier
  
  return (sock >= 0) ? 0 : E_tcpip_open;
}

/* --- */

- (int)open
{
  return [self open:lock_timeout];
}

/* ---------------------------------------------------------------- */

- (void)close
{
  BOOL r = [self mylock:lock_timeout];

#if (DEBUG > 1)
  fprintf(stderr,"%s(): count=%d\n",__func__,count);
#endif 
  
  if (count > 0) {
    count--;
    if (count == 0) {
      if (online) {
        if (sock > 0) close(sock);
      }
      sock = -1;
    }
  }
  
  if (r) [lock unlock];
}

/* ---------------------------------------------------------------- */

- (int)request:(const char*)cmd response:(char*)res max:(int)len
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d)\n",__func__,cmd,len);
#endif
  assert(res);

  int err = [self open]; if (err) return err;

  if (![self mylock:lock_timeout]) { // open/lock more MT-efficient
    err = E_tcpip_lock;
  } else {
    if (online) {
      assert(sock > 0);
      if (send(sock,cmd,strlen(cmd),0) < 0) err = E_tcpip_send;
      if (!err) { int i;
        for (i=0; i<len-1; i++) {
          err = TCPIP_Wait4Char(sock,&res[i],1000*sock_timeout);
          if (err) break;
          if (res[i] == terminator) break; // strip 'terminator'
        }
        res[i] = '\0';
      }
    } else {                           // Simulator
      assert(sock == 0);
      [NSThread sleepForTimeInterval:0.050];
    }
    [lock unlock];
  }
  [self close];

  return err;
}

/* ---------------------------------------------------------------- */

- (int)sendBytes:(const char*)bytes size:(size_t)size  // 2015-05-11
{
  int err=0;
  
  assert(sock >= 0);
  if (online) {
    if (TCPIP_ReadyForWrite(sock)) err = TCPIP_Send(sock,bytes,size);
    else                           err = E_tcpip_send;
  }
  return err;
}

/* ---------------------------------------------------------------- */

- (void)shutdown
{
  BOOL r;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",__func__,self);
#endif
  @synchronized(self) {
    up_and_running = FALSE;
  }

  r = [self mylock:lock_timeout];
  while (count > 0) {                  // active connection
    if (r) [lock unlock];
    [self close];
    r = [self mylock:lock_timeout];
  }
  if (r) [lock unlock];  
}

/* ---------------------------------------------------------------- */

- (void)dealloc
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",__func__,self);
#endif

  if (host) free((void*)host);
  [lock release];  
  [super dealloc];
}

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

@implementation TCPIP_Server

/* ---------------------------------------------------------------- */
#pragma mark "Class"
/* ---------------------------------------------------------------- */

+ (void)initialize
{
  if (self != [TCPIP_Server class]) return; // prevent sub-classes
#if (DEBUG > 1)
  fprintf(stderr,"%s:%p\n",__func__,self);
#endif  
}

/* ---------------------------------------------------------------- */

+ (NSString*)errorString:(int)err
{
  NSString *string=nil;
  
  switch (err) {
    case E_tcpip_start: string = @"TCPIP: starting server failed"; break;
    case E_tcpip_hostname: string = @"TCPIP: gethostname() failed"; break;
    case E_tcpip_bind: string = @"TCPIP: bind() failed"; break;
    case E_tcpip_listen: string = @"TCPIP: listen() failed"; break;
  }
  if (!string) string = [TCPIP_Connection errorString:err];

  return string;
}

/* ---------------------------------------------------------------- */

+ (int)createServerSocket:(u_short)port
{
  int     sock=-1,protocol=0,err=0;
  struct  sockaddr_in saddr;

  memset((void*)&saddr,0,sizeof(saddr));
  saddr.sin_family = AF_INET; 
  saddr.sin_port = htons(port);

  @synchronized(self) {
    if (!err) { struct protoent *proto;
      proto = getprotobyname("tcp");       /* get protocol by name */
      if (proto == NULL) err = E_tcpip_protocol;
      else               protocol = proto->p_proto;
    }
#if 0 // with '1st interface' only
    if (!err) {  char hostname[256];
      if (gethostname(hostname,sizeof(hostname))) err = E_tcpip_hostname;
    }
#endif
    if (!err) {
#if 0 /* 1st interface */
      struct hostent *hostp;
      hostp = gethostbyname(hostname);     /* get host IP address */
      if (hostp == NULL) err = E_tcpip_hostbyname;
      else memcpy(&saddr.sin_addr,hostp->h_addr_list[0],hostp->h_length);
#else /* any interface */
      saddr.sin_addr.s_addr = INADDR_ANY;
#endif
    }
  }

  if (!err) {                     /* create socket */
    if ((sock = socket(PF_INET,SOCK_STREAM,protocol)) < 0) {
      err = E_tcpip_socket;
    }
  }
  if (!err) { int on=1;           /* close-on-exec & reuse-address */
    (void)fcntl(sock,F_SETFD,FD_CLOEXEC);
    (void)setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
    (void)setsockopt(sock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
  }
  if (!err) {
    if (bind(sock,(struct sockaddr *)&saddr,sizeof(saddr)) == -1) {
      (void)close(sock);
      err = E_tcpip_bind;
      fprintf(stderr,"%s: bind() failed, errno=%d\n",__func__,errno);
    }
  }
  if (!err) {
    if (listen(sock,SOMAXCONN) == -1) {  /* listen to socket */
      (void)close(sock);
      err = E_tcpip_listen;
    }
  }
  
  return (err) ? -err : sock;
}


/* ---------------------------------------------------------------- */
#pragma mark "Instance"
/* ---------------------------------------------------------------- */

- (instancetype)initWithPort:(u_short)pin delegate:(id)del
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%p)\n",__func__,pin,del);
#endif

  if ((self = [super init]) == nil) return nil;

  port = pin;
  delegate = del;
  _throttle = 0.0f;
  
  // IDEA: property 'terminator' , default = <LF>
  
  return self;
}

/* ---------------------------------------------------------------- */

- (int)start
{
  shutdown = FALSE;

  sock = [TCPIP_Server createServerSocket:port];
  if (sock < 0) return E_tcpip_start; 

  [self performSelectorInBackground:@selector(run_server) withObject:nil];
  
  return 0;
}

/* ---------------------------------------------------------------- */

- (int)stop
{
  int err;
  const char *cmd="shutdown\n";

  shutdown = TRUE;
  
  TCPIP_Connection *connection = [[TCPIP_Connection alloc] initWithHost:"localhost"
                                                           port:port online:YES];
  err = [connection open];
  if (!err) {
    err = [connection sendBytes:cmd size:strlen(cmd)];
    [connection close];
  }
  [connection release];
  
  return err;
}

/* ---------------------------------------------------------------- */

- (void)run_server
{
  int       msgsock,on=1;
  socklen_t size;
  struct sockaddr saddr;
  NSString *ipNumber;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",__func__,self);
#endif
  assert(![NSThread isMainThread]);

  while (!shutdown) {
    size = sizeof(saddr);
    msgsock = accept(sock,&saddr,&size);  // accept connection (blocking)
    if (msgsock < 0) continue;
    if (shutdown) break;
    setsockopt(msgsock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on)); // 2017-07-05
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    ipNumber = [NSString stringWithFormat:@"%u.%u.%u.%u",
                         (u_char)saddr.sa_data[2],
                         (u_char)saddr.sa_data[3],
                         (u_char)saddr.sa_data[4],
                         (u_char)saddr.sa_data[5]];
    // NSLog(@"%s(%@)",__func__,ipNumber);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),^(void) {
      char command[512],*answer; ssize_t rval; int drop=0;
      do {                             // handed off connection (msgsock)
        answer = NULL;
        rval = TCPIP_Recv(msgsock,command,sizeof(command)-1,0); // blocking
        if (shutdown) break;
        if (rval > 0) {
          command[rval] = '\0';        // nul-terminate
          @autoreleasepool {           // fix leak 2017-04-11
            answer = [delegate tcpip_handler:command from:ipNumber socket:msgsock];
          }
          if (answer) {
            if (*answer) {             // send response
              (void)TCPIP_Send(msgsock,answer,strlen(answer)); // IDEA: handle error
            } else {                   // empty response
              drop = 1;
            }
            free((void*)answer);
          } // endif(answer)
        } // endif(rval)
        if (shutdown) break;
        if (!answer) break;            // no answer -> handover
        if (drop) break;
      } while (rval > 0);              // loop while there's something
      if ((rval <= 0) || drop || shutdown) close(msgsock);
    });
    [pool drain];
    if (!shutdown) {
      if (_throttle > 0) [NSThread sleepForTimeInterval:_throttle];
    }
  } // while(!shutdown)
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p ... shutdown\n",__func__,self);
#endif
}

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

int TCPIP_Wait4Char(int sock,char* c,int timeout) // [ms]
{
  ssize_t r;
  fd_set  rfd;
  struct timeval tout;
#if (DEBUG > 2)
  fprintf(stderr,"%s(%d,%p,%d)\n",__func__,sock,c,timeout);
#endif

  do {
    FD_ZERO(&rfd); FD_SET(sock,&rfd); 
    tout.tv_sec = timeout/1000; tout.tv_usec = 1000*(timeout%1000);
    r = select(sock+1,&rfd,NULL,NULL,&tout);
    if (FD_ISSET(sock,&rfd)) break;
  } while (r > 0);
  if (r == 0) return E_tcpip_timeout;
  if (r <  0) return E_tcpip_select;   // 2015-03-17
  r = recv(sock,c,1,0);
  if (r <  0) return E_tcpip_recv;
  if (r == 0) return E_tcpip_nodata;
  return 0;
}

/* ---------------------------------------------------------------- */

int TCPIP_Send(int sock,const char* command,size_t size)
{
  ssize_t r = send(sock,command,size,0);
  if (r != size) return E_tcpip_send;
  
  return 0;
}

/* ---------------------------------------------------------------- */

ssize_t TCPIP_Recv(int sock,char *answer,size_t size,int timeout)
{
  int     r=1;
  ssize_t rval=0;
  
  if (timeout > 0) r = TCPIP_Select(sock,timeout);
  if (r > 0) rval = recv(sock,answer,size,0);
  
  return (r < 0) ? r : rval;
}

/* ---------------------------------------------------------------- */

int TCPIP_Select(int sock,int timeout) // [ms]
{
  int r;
  fd_set  rfd;
  struct timeval tout;

  do {
    FD_ZERO(&rfd); FD_SET(sock,&rfd);
    tout.tv_sec = timeout/1000; tout.tv_usec = 1000*(timeout%1000);
    r = select(sock+1,&rfd,NULL,NULL,&tout);
    if (FD_ISSET(sock,&rfd)) break;
  } while (r > 0);

  return r;
}

/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

BOOL TCPIP_ReadyForWrite(int sock)     // 2015-05-18
{
  int r;
  fd_set  wfd;
  struct timeval tout;

  FD_ZERO(&wfd); FD_SET(sock,&wfd);
  tout.tv_sec = 0; tout.tv_usec = 0;   // just poll
  r = select(sock+1,NULL,&wfd,NULL,&tout);
  if (r > 0) {
    if (FD_ISSET(sock,&wfd)) return YES;
  }

  return NO;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
