/* ----------------------------------------------------------------
 *
 * gframe.c -- guide corrections as 'G' frames for the Swope TCS
 *
 * gcamzwo adds each guide correction (arcsec) to a pending pair.  A
 * thread serves a TCP port and, at most once per gframe_period, turns
 * the pending pair into one frame for every connected client, keeping
 * what that frame could not carry for the next one.  The frame is the
 * one the DOS guider PC writes on its serial line: 'G', two signed
 * two-digit counts (a negative n is written as 100+n), CR, LF.
 * README.md has the format, the keys and what the TCS does with it.
 *
 * 2026-11-09  first version
 *
 * ---------------------------------------------------------------- */

#define _REENTRANT                     /* multi-threaded code */

#include <stdio.h>                     /* snprintf() */
#include <stdlib.h>                    /* atoi(),atof() */
#include <stdarg.h>                    /* va_list */
#include <string.h>                    /* strcmp(),strerror() */
#include <math.h>                      /* lround(),fabs(),floor() */
#include <errno.h>
#include <fcntl.h>                     /* fcntl() */
#include <poll.h>                      /* poll() */
#include <unistd.h>                    /* close(),pipe(),write() */
#include <time.h>                      /* clock_gettime() */
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>               /* TCP_NODELAY, keepalive knobs */
#include <arpa/inet.h>                 /* inet_ntop() */

#include "gframe.h"

/* DEFINEs -------------------------------------------------------- */

#define COUNT_MAX       48             /* the TCS drops a frame with -49 or +50 */
#define STALE           3.0            /* s without a correction: drop the
                                          pending pair (the TCS forgets its
                                          own after 3 s) */
#define PEND_FRAMES     4              /* pending held to 4 frames' worth */
#define TICK_MS         100            /* poll: how late a due frame can be */
#define KEEP_IDLE       10             /* s, TCP keepalive on a client */
#define KEEP_INTVL      5
#define KEEP_CNT        3

#ifndef MSG_NOSIGNAL                   /* macOS: SO_NOSIGPIPE instead */
#define MSG_NOSIGNAL    0
#endif

/* STATICs -------------------------------------------------------- */

static struct {                        /* the gframe_* keys */
  int    port;                         /* 0 = off */
  double period;                       /* s between frames, at least */
  double unit;                         /* arcsec per count */
  double max;                          /* arcsec per frame and axis */
  double min;                          /* arcsec: an axis whose frame would
                                          carry this much or less waits */
  int    xsign,ysign;                  /* +1 or -1 on the wire */
} cfg = { 0, 1.5, 0.05, 2.4, 0.1, 1, 1 };

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER; /* all below */
static double          pend_x=0,pend_y=0; /* arcsec, not yet sent */
static double          t_add=0;        /* uptime of the last gframe_add() */
static unsigned long   nsent=0,ndrop=0;
static int             client[GFRAME_CLIENTS_MAX];
static char            client_ip[GFRAME_CLIENTS_MAX][INET_ADDRSTRLEN];
static int             nclient=0;      /* changed by the thread only */

static int             lsock=-1;       /* listening socket */
static int             wake[2]={-1,-1}; /* gframe_stop() -> the thread */
static volatile int    running=0;
static pthread_t       tid;
static void          (*logfn)(int,const char*)=NULL;

/* ---------------------------------------------------------------- */
/* LOCAL */
/* ---------------------------------------------------------------- */

static void say(int level,const char* fmt,...)
{
  char    buf[256];
  va_list ap;

  if (!logfn) return;
  va_start(ap,fmt);
  (void)vsnprintf(buf,sizeof(buf),fmt,ap);
  va_end(ap);
  logfn(level,buf);
}

/* --- */

static double uptime(void)             /* monotonic seconds */
{
  struct timespec ts;

  (void)clock_gettime(CLOCK_MONOTONIC,&ts);
  return (double)ts.tv_sec + 1e-9*(double)ts.tv_nsec;
}

/* --- */

static int counts(double v)            /* what a frame carries of v [arcsec] */
{
  long lim = (long)floor(cfg.max/cfg.unit + 1e-9);
  long n   = lround(v/cfg.unit);

  if (lim > COUNT_MAX) lim = COUNT_MAX;
  if (n >  lim) n =  lim;
  if (n < -lim) n = -lim;
  if (fabs((double)n*cfg.unit) <= cfg.min + 1e-9) n = 0; /* the TCS would
                                          drop it: keep it pending */
  return (int)n;
}

/* --- */

static void cloexec(int fd)
{
  (void)fcntl(fd,F_SETFD,FD_CLOEXEC);
}

static void nonblock(int fd)
{
  int fl = fcntl(fd,F_GETFL,0);

  if (fl >= 0) (void)fcntl(fd,F_SETFL,fl | O_NONBLOCK);
}

/* --- */

static void accept_client(void)
{
  struct sockaddr_in a;
  socklen_t al=sizeof(a);
  char      ip[INET_ADDRSTRLEN]="?";
  int       s,on=1;

  s = accept(lsock,(struct sockaddr*)&a,&al);
  if (s < 0) return;
  (void)inet_ntop(AF_INET,&a.sin_addr,ip,sizeof(ip));
  if (nclient >= GFRAME_CLIENTS_MAX) {
    (void)close(s);
    say(1,"gframe: %s refused, %d clients already",ip,nclient);
    return;
  }
  cloexec(s);
  nonblock(s);                         /* a stuck client never blocks us */
#ifdef SO_NOSIGPIPE
  (void)setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
  (void)setsockopt(s,IPPROTO_TCP,TCP_NODELAY,&on,sizeof(on));
  (void)setsockopt(s,SOL_SOCKET,SO_KEEPALIVE,&on,sizeof(on));
  { int v;                             /* notice a TCS that vanished */
#if defined(TCP_KEEPIDLE)              /* Linux */
    v = KEEP_IDLE;  (void)setsockopt(s,IPPROTO_TCP,TCP_KEEPIDLE,&v,sizeof(v));
#elif defined(TCP_KEEPALIVE)           /* macOS */
    v = KEEP_IDLE;  (void)setsockopt(s,IPPROTO_TCP,TCP_KEEPALIVE,&v,sizeof(v));
#endif
#ifdef TCP_KEEPINTVL
    v = KEEP_INTVL; (void)setsockopt(s,IPPROTO_TCP,TCP_KEEPINTVL,&v,sizeof(v));
#endif
#ifdef TCP_KEEPCNT
    v = KEEP_CNT;   (void)setsockopt(s,IPPROTO_TCP,TCP_KEEPCNT,&v,sizeof(v));
#endif
    (void)v;
  }
  pthread_mutex_lock(&lock);
  client[nclient] = s;
  snprintf(client_ip[nclient],sizeof(client_ip[0]),"%s",ip);
  nclient++;
  pthread_mutex_unlock(&lock);
  say(0,"gframe: %s connected",ip);
}

/* --- */

static void drop_client(int i,const char* why) /* i: the thread's index */
{
  say(0,"gframe: %s disconnected (%s)",client_ip[i],why);
  (void)close(client[i]);
  pthread_mutex_lock(&lock);
  nclient--;                           /* the last one takes i's place */
  client[i] = client[nclient];
  memcpy(client_ip[i],client_ip[nclient],sizeof(client_ip[0]));
  pthread_mutex_unlock(&lock);
}

/* --- */

static void flush(double* t_frame)     /* send a frame if one is due */
{
  char   frame[GFRAME_LEN+1];
  int    n=0,i;
  double now=uptime();

  pthread_mutex_lock(&lock);
  if ((pend_x != 0.0) || (pend_y != 0.0)) {
    if ((now-t_add > STALE) || !nclient) { /* stopped, or nobody to send to */
      pend_x = pend_y = 0.0;
      ndrop++;
    }
  }
  if (now - *t_frame >= cfg.period) n = gframe_encode(&pend_x,&pend_y,frame);
  pthread_mutex_unlock(&lock);
  if (!n) return;

  *t_frame = now;
  for (i=nclient-1; i>=0; i--) {       /* from the end: drop_client() moves
                                          the last one into i */
    ssize_t k = send(client[i],frame,(size_t)n,MSG_NOSIGNAL);
    if (k != n) drop_client(i,(k < 0) ? strerror(errno) : "short write");
  }
  pthread_mutex_lock(&lock);
  nsent++;
  pthread_mutex_unlock(&lock);
}

/* --- */

static void* run(void* arg)            /* accept, read and forget, send */
{
  struct pollfd pf[2+GFRAME_CLIENTS_MAX];
  char   junk[256];
  double t_frame=0.0;
  int    i,n,quiet=0;

  (void)arg;
  while (running) {
    pf[0].fd = lsock;   pf[0].events = POLLIN; pf[0].revents = 0;
    pf[1].fd = wake[0]; pf[1].events = POLLIN; pf[1].revents = 0;
    for (i=0; i<nclient; i++) {
      pf[2+i].fd = client[i]; pf[2+i].events = POLLIN; pf[2+i].revents = 0;
    }
    n = 2+nclient;
    if (poll(pf,(nfds_t)n,TICK_MS) < 0) {
      if (errno != EINTR) {
        if (!quiet++) say(1,"gframe: poll failed (%s)",strerror(errno));
        usleep(1000*TICK_MS);
      }
      continue;
    }
    quiet = 0;
    if (!running || pf[1].revents) break;
    for (i=n-3; i>=0; i--) {           /* the TCS sends nothing: read, forget */
      if (pf[2+i].revents & (POLLIN | POLLHUP | POLLERR)) {
        ssize_t k = recv(client[i],junk,sizeof(junk),0);
        if (k == 0) drop_client(i,"closed");
        else if ((k < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK) &&
                 (errno != EINTR)) drop_client(i,strerror(errno));
      }
    }
    if (pf[0].revents & POLLIN) accept_client();
    flush(&t_frame);
  }

  pthread_mutex_lock(&lock);
  for (i=0; i<nclient; i++) (void)close(client[i]);
  nclient = 0;
  pthread_mutex_unlock(&lock);

  return NULL;
}

/* ---------------------------------------------------------------- */
/* GLOBAL */
/* ---------------------------------------------------------------- */

int gframe_ini(const char* key,const char* val)
{
  double f = atof(val);
  int    r=1;

  if (strncmp(key,"gframe_",7)) return 0;

  pthread_mutex_lock(&lock);
  if      (!strcmp(key,"gframe_port")) {   /* read at start-up only */
    if (!running) cfg.port = ((atoi(val) > 0) && (atoi(val) < 65536)) ? atoi(val) : 0;
  }
  else if (!strcmp(key,"gframe_period")) cfg.period = fmin(60.0,fmax(0.2,f));
  else if (!strcmp(key,"gframe_unit"))   cfg.unit   = fmin(1.0,fmax(0.001,f));
  else if (!strcmp(key,"gframe_max"))    cfg.max    = fmax(0.0,f);
  else if (!strcmp(key,"gframe_min"))    cfg.min    = fmax(0.0,f);
  else if (!strcmp(key,"gframe_xsign"))  cfg.xsign  = (f < 0) ? -1 : 1;
  else if (!strcmp(key,"gframe_ysign"))  cfg.ysign  = (f < 0) ? -1 : 1;
  else r = 0;
  pthread_mutex_unlock(&lock);

  return r;
}

/* --- */

int gframe_enabled(void)
{
  return (cfg.port != 0);
}

/* --- */

int gframe_start(void (*on_log)(int,const char*))
{
  struct sockaddr_in a;
  int    on=1,err=0;

  if (running || !cfg.port) return 0;
  logfn = on_log;

  if (pipe(wake)) {
    err = errno; wake[0] = wake[1] = -1;
    goto fail;
  }
  cloexec(wake[0]); cloexec(wake[1]);
  if ((lsock = socket(AF_INET,SOCK_STREAM,0)) < 0) { err = errno; goto fail; }
  cloexec(lsock);
  nonblock(lsock);                     /* accept() after poll() never waits */
  (void)setsockopt(lsock,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
  memset(&a,0,sizeof(a));
  a.sin_family      = AF_INET;
  a.sin_port        = htons((unsigned short)cfg.port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(lsock,(struct sockaddr*)&a,sizeof(a)) ||
      listen(lsock,GFRAME_CLIENTS_MAX)) { err = errno; goto fail; }

  running = 1;
  if ((err = pthread_create(&tid,NULL,run,NULL))) { running = 0; goto fail; }
  say(0,"gframe: G frames on port %d, at most one per %.1f s, %.3f\"/count",
      cfg.port,cfg.period,cfg.unit);
  return 0;

fail:
  say(1,"gframe: cannot serve port %d (%s)",cfg.port,strerror(err));
  if (lsock >= 0)   (void)close(lsock);
  if (wake[0] >= 0) (void)close(wake[0]);
  if (wake[1] >= 0) (void)close(wake[1]);
  lsock = wake[0] = wake[1] = -1;
  return err;
}

/* --- */

void gframe_stop(void)
{
  ssize_t w;

  if (!running) return;
  running = 0;
  w = write(wake[1],"x",1);            /* poll() returns at once */
  (void)w;
  pthread_join(tid,NULL);
  (void)close(lsock);   lsock = -1;
  (void)close(wake[0]); (void)close(wake[1]);
  wake[0] = wake[1] = -1;
  gframe_clear();
}

/* --- */

void gframe_add(double x,double y)
{
  double lim;

  if (!cfg.port) return;
  pthread_mutex_lock(&lock);
  lim = PEND_FRAMES*cfg.max;
  pend_x = fmin(lim,fmax(-lim,pend_x+x));
  pend_y = fmin(lim,fmax(-lim,pend_y+y));
  t_add  = uptime();
  pthread_mutex_unlock(&lock);
}

/* --- */

void gframe_clear(void)
{
  pthread_mutex_lock(&lock);
  pend_x = pend_y = 0.0;
  pthread_mutex_unlock(&lock);
}

/* --- */

int gframe_clients(void)
{
  int n;

  pthread_mutex_lock(&lock);
  n = nclient;
  pthread_mutex_unlock(&lock);
  return n;
}

/* --- */

char* gframe_status(char* buf,int len)
{
  if (len <= 0) return buf;
  pthread_mutex_lock(&lock);
  (void)snprintf(buf,(size_t)len," gframe=%d gpend=%+.2f,%+.2f gsent=%lu gdrop=%lu",
                 nclient,pend_x,pend_y,nsent,ndrop);
  pthread_mutex_unlock(&lock);
  return buf;
}

/* --- */

/* Turn *x,*y [arcsec] into one frame and subtract what it carries.
 * 'frame' takes GFRAME_LEN+1 bytes.  Returns GFRAME_LEN, or 0 when
 * neither axis has enough for the TCS to act on (nothing changes).
 * The thread calls it with the lock held; the bench calls it alone. */

int gframe_encode(double* x,double* y,char* frame)
{
  int      cx = counts(*x);
  int      cy = counts(*y);
  unsigned wx,wy;

  if (!cx && !cy) return 0;
  *x -= cx*cfg.unit;
  *y -= cy*cfg.unit;
  cx *= cfg.xsign;
  cy *= cfg.ysign;
  wx = (unsigned)((cx < 0) ? 100+cx : cx) % 100u;   /* -48..48: the % only */
  wy = (unsigned)((cy < 0) ? 100+cy : cy) % 100u;   /* tells gcc 2 digits  */
  (void)snprintf(frame,GFRAME_LEN+1,"G%02u%02u\r\n",wx,wy);
  return GFRAME_LEN;
}

/* ---------------------------------------------------------------- */
