/* ----------------------------------------------------------------
 *
 * goffset.c -- mouse-mode offsets through the Swope app
 *
 * One connection per offset: connect to the app's command port, send
 * the line, read the answer, close.  The app answers when the move
 * has ended, with a line end, and keeps the connection open until the
 * client closes it: the answer ends at the line end, or once the port
 * has been quiet for a moment (some of the app's answers have none).
 * README.md has the commands, the keys and the hooks.
 *
 * 2026-11-22  first version
 *
 * ---------------------------------------------------------------- */

#define _REENTRANT                     /* multi-threaded code */

#include <stdio.h>                     /* snprintf() */
#include <stdlib.h>                    /* atoi(),atof() */
#include <string.h>                    /* strcmp(),strerror() */
#include <math.h>                      /* isfinite() */
#include <errno.h>
#include <fcntl.h>                     /* fcntl() */
#include <poll.h>                      /* poll() */
#include <unistd.h>                    /* close() */
#include <time.h>                      /* clock_gettime() */
#include <pthread.h>
#include <netdb.h>                     /* getaddrinfo() */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "goffset.h"

/* DEFINEs -------------------------------------------------------- */

#define CONNECT_S       2.0            /* s to connect */
#define QUIET_MS        200            /* the answer is complete once the
                                          port has been quiet this long */

#ifndef MSG_NOSIGNAL                   /* macOS: SO_NOSIGPIPE instead */
#define MSG_NOSIGNAL    0
#endif

/* STATICs -------------------------------------------------------- */

static struct {                        /* the offset_* and probe_* keys */
  int    port;                         /* 0 = off */
  char   host[128];
  double wait;                         /* s for the answer */
  int    xsign,ysign;                  /* +1 or -1, guider_offset only */
} cfg = { 0, "127.0.0.1", 120.0, 1, 1 };

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER; /* cfg */

/* ---------------------------------------------------------------- */
/* LOCAL */
/* ---------------------------------------------------------------- */

static double uptime(void)             /* monotonic seconds */
{
  struct timespec ts;

  (void)clock_gettime(CLOCK_MONOTONIC,&ts);
  return (double)ts.tv_sec + 1e-9*(double)ts.tv_nsec;
}

/* --- */

static int ms_left(double t_end)       /* for poll() */
{
  double s = t_end - uptime();

  return (s <= 0.0) ? 0 : (int)(1000.0*s + 0.5);
}

/* --- */

static int open_link(const char* host,int port,char* msg,int len)
{
  struct addrinfo hints,*res=NULL,*a;
  struct pollfd   p;
  char   service[16];
  int    s=-1,err=0,on=1;
  socklen_t elen=sizeof(err);
  double t_end = uptime() + CONNECT_S;

  memset(&hints,0,sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(service,sizeof(service),"%d",port);
  if ((err = getaddrinfo(host,service,&hints,&res)) || !res) {
    snprintf(msg,len,"no answer from the Swope app (%s: %s)",host,
             (err) ? gai_strerror(err) : "no address");
    return -1;
  }
  for (a=res; a; a=a->ai_next) {
    if ((s = socket(a->ai_family,a->ai_socktype,a->ai_protocol)) < 0) {
      err = errno;
      continue;
    }
    (void)fcntl(s,F_SETFD,FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    (void)setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
    (void)on;
    (void)fcntl(s,F_SETFL,fcntl(s,F_GETFL,0) | O_NONBLOCK);
    if (connect(s,a->ai_addr,a->ai_addrlen) == 0) break;
    err = errno;
    if (err == EINPROGRESS) {
      p.fd = s; p.events = POLLOUT; p.revents = 0;
      if (poll(&p,1,ms_left(t_end)) == 1) {
        elen = sizeof(err);
        if (getsockopt(s,SOL_SOCKET,SO_ERROR,&err,&elen)) err = errno;
        if (!err) break;
      } else {
        err = ETIMEDOUT;
      }
    }
    (void)close(s); s = -1;
  }
  freeaddrinfo(res);
  if (s < 0) {
    snprintf(msg,len,"no answer from the Swope app (%s:%d: %s)",host,port,
             strerror(err));
  }
  return s;
}

/* --- */

static int send_all(int s,const char* buf,size_t n,double t_end)
{
  struct pollfd p;
  ssize_t w;

  while (n > 0) {
    w = send(s,buf,n,MSG_NOSIGNAL);
    if (w > 0) { buf += w; n -= (size_t)w; continue; }
    if ((w < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK) &&
        (errno != EINTR)) return errno;
    p.fd = s; p.events = POLLOUT; p.revents = 0;
    if (poll(&p,1,ms_left(t_end)) < 1) return ETIMEDOUT;
  }
  return 0;
}

/* --- */

static int read_answer(int s,char* buf,int len,double t_end,int* closed)
{                                      /* bytes */
  struct pollfd p;
  ssize_t r;
  int     n=0,ms;

  *closed = 0;
  while (n < len-1) {
    ms = (n) ? QUIET_MS : ms_left(t_end); /* first byte: the move's time */
    if (!n && !ms) break;
    p.fd = s; p.events = POLLIN; p.revents = 0;
    r = poll(&p,1,ms);
    if (r < 0) { if (errno == EINTR) continue; break; }
    if (r == 0) break;                 /* quiet, or no answer in time */
    r = recv(s,buf+n,(size_t)(len-1-n),0);
    if (r < 0) { if ((errno == EINTR) || (errno == EAGAIN)) continue; break; }
    if (r == 0) { *closed = 1; break; } /* the app closed */
    n += (int)r;
    if (memchr(buf,'\n',(size_t)n)) break;
  }
  buf[n] = '\0';
  return n;
}

/* --- */

static void trim(char* s)
{
  char *p=s,*e;

  while ((*p == ' ') || (*p == '\t') || (*p == ':')) p++;
  if (p != s) memmove(s,p,strlen(p)+1);
  e = s + strlen(s);
  while ((e > s) && ((e[-1] == '\n') || (e[-1] == '\r') || (e[-1] == ' ')))
    *--e = '\0';
}

/* --- */

static int request(const char* line,char* msg,int len)
{
  char   host[128],answer[GOFFSET_MSG],spare[GOFFSET_MSG];
  int    port,s,n,err,closed;
  double wait,t_end;

  pthread_mutex_lock(&lock);
  port = cfg.port;
  wait = cfg.wait;
  snprintf(host,sizeof(host),"%s",cfg.host);
  pthread_mutex_unlock(&lock);

  if (!msg || (len <= 0)) { msg = spare; len = sizeof(spare); }
  *msg = '\0';
  if (!port) {
    snprintf(msg,len,"offset_port is not set");
    return GOFFSET_OFF;
  }

  if ((s = open_link(host,port,msg,len)) < 0) return GOFFSET_NOLINK;

  t_end = uptime() + wait;
  if ((err = send_all(s,line,strlen(line),t_end))) {
    (void)close(s);
    snprintf(msg,len,"no answer from the Swope app (%s)",strerror(err));
    return GOFFSET_NOLINK;             /* the app took none of it */
  }
  n = read_answer(s,answer,sizeof(answer),t_end,&closed);
  (void)close(s);

  trim(answer);
  if (!n || !*answer) {
    if (closed) snprintf(msg,len,"the Swope app closed without an answer");
    else        snprintf(msg,len,"no answer from the Swope app in %.0f s",wait);
    return GOFFSET_TIMEOUT;
  }
  if (!strncmp(answer,"ok",2) && ((answer[2] == '\0') || (answer[2] == ' '))) {
    snprintf(msg,len,"%s",answer);
    return GOFFSET_OK;
  }
  if (!strncmp(answer,"-E",2)) {
    snprintf(msg,len,"%s",answer+2);
    trim(msg);
    if (!*msg) snprintf(msg,len,"refused");
    return GOFFSET_REFUSED;
  }
  snprintf(msg,len,"unexpected answer from the Swope app: %.60s",answer);
  return GOFFSET_BADREPLY;
}

/* ---------------------------------------------------------------- */
/* GLOBAL */
/* ---------------------------------------------------------------- */

int goffset_ini(const char* key,const char* val)
{
  double f = atof(val);
  int    i = atoi(val),r=1;

  pthread_mutex_lock(&lock);
  if      (!strcmp(key,"offset_port")) {
    cfg.port = ((i > 0) && (i < 65536)) ? i : 0;
  }
  else if (!strcmp(key,"offset_host")) {
    if (*val) snprintf(cfg.host,sizeof(cfg.host),"%s",val);
  }
  else if (!strcmp(key,"offset_wait")) cfg.wait = fmin(600.0,fmax(0.5,f));
  else if (!strcmp(key,"probe_xsign")) cfg.xsign = (f < 0) ? -1 : 1;
  else if (!strcmp(key,"probe_ysign")) cfg.ysign = (f < 0) ? -1 : 1;
  else r = 0;
  pthread_mutex_unlock(&lock);

  return r;
}

/* --- */

int goffset_enabled(void)
{
  int on;

  pthread_mutex_lock(&lock);
  on = (cfg.port != 0);
  pthread_mutex_unlock(&lock);
  return on;
}

/* --- */

int goffset_probe(int gnum,double x,double y,char* msg,int len)
{
  char line[96];
  int  xs,ys;

  if (!isfinite(x) || !isfinite(y)) {
    if (msg && (len > 0)) snprintf(msg,len,"no offset (not a number)");
    return GOFFSET_REFUSED;
  }
  pthread_mutex_lock(&lock);
  xs = cfg.xsign; ys = cfg.ysign;
  pthread_mutex_unlock(&lock);

  snprintf(line,sizeof(line),"guider_offset %d %.2f %.2f\n",gnum,xs*x,ys*y);
  return request(line,msg,len);
}

/* --- */

int goffset_tele(double x,double y,char* msg,int len)
{
  char line[96];

  if (!isfinite(x) || !isfinite(y)) {
    if (msg && (len > 0)) snprintf(msg,len,"no offset (not a number)");
    return GOFFSET_REFUSED;
  }
  snprintf(line,sizeof(line),"tele_offset %.2f %.2f\n",x,y);
  return request(line,msg,len);
}

/* ---------------------------------------------------------------- */
