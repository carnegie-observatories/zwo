/* ----------------------------------------------------------------
 *
 * gcmd.c -- poll tcsis for the TCS's command echoes
 *
 * One of two copies of the gcmd client: zwo src/gcam/gcmd.c and imacs
 * src/gcmd.c, the protocol in imacs docs/plans/guider-architecture.md,
 * Appendix A.  Keep them the same: a change to the loop, the answer's
 * parsing or the back-off in one is made in the other, in pull
 * requests of the two repositories opened together.
 *
 * tcsis keeps the TCS's last 64 command echoes in a ring, each tagged
 * with the rotator of interest when the TCS took it, and answers
 * 'gcmd <rotator> <after>' with one line:
 *
 *   <roi> <seq> <sender> <text>   the oldest echo for <rotator> after <after>
 *   <roi> <latest> -              none newer (always for <after> -1)
 *   -1                            no ring (an older edsshm), or a bad request
 *
 * and a bare 'gcmd' with '<roi> <latest> -' (or -1).  <roi> is the
 * rotator of interest now, '-' when unknown.  A thread asks on a socket
 * of its own.  Its first request on each connection is the bare 'gcmd':
 * to a tcsis older than gcmd a request without a space is a status
 * request, answered with an empty line, so nothing reaches the TCS, and
 * the thread asks only the bare form again, every GCMD_NORING_MS.  Once
 * it is answered, the thread asks 'gcmd <rotator> <after>', again at
 * once after an echo and every GCMD_TICK_MS otherwise, with <after> the
 * number of the last answer.  A program starts at <latest>, so nothing
 * older is replayed; after a reconnect the thread goes on from its
 * cursor, so what the TCS took while tcsis was away is not lost (the
 * ring's 64 entries bound it), or from 0 when <latest> is below the
 * cursor: the ring started again (edsshm on a new segment) and all of
 * it is new.  A new ring whose <latest> has already reached the cursor
 * looks like the old one (the answer carries no generation number), and
 * its first echoes are passed over.  An echo goes to on_cmd.  The
 * rotator of interest is taken from the answers without an echo, so
 * that a client with echoes still to come acts on all of them first, in
 * the order the TCS took them, and only then sees the rotator change
 * (on_roi).  A lost tcsis is warned about once, the rotator of interest
 * becomes unknown (-1) and the thread reconnects with a back-off.
 * Standard headers only (POSIX sockets, poll, pthreads), so that a bench
 * can link this file alone.
 *
 * ---------------------------------------------------------------- */

#include <stdio.h>                     /* snprintf() */
#include <stdlib.h>                    /* strtoll() */
#include <stdarg.h>                    /* va_list */
#include <string.h>                    /* memset(), strlen() */
#include <errno.h>
#include <ctype.h>                     /* isdigit() */
#include <time.h>                      /* clock_gettime() */
#include <unistd.h>                    /* close(), pipe() */
#include <fcntl.h>                     /* fcntl() */
#include <poll.h>
#include <netdb.h>                     /* getaddrinfo() */
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "gcmd.h"

#ifndef MSG_NOSIGNAL                   /* macOS: SO_NOSIGPIPE instead */
#define MSG_NOSIGNAL 0
#endif

/* STATICs -------------------------------------------------------- */

static Gcmd          cfg;              /* the caller's settings, copied */
static char          host[128];
static int           configured=0;     /* a host to poll */
static volatile int  telroi=-1;        /* last answered rotator of interest */
static long long     after=-1;         /* the next request's <after> */
static volatile int  running=0;        /* a poll thread is up */
static volatile int  stopping=0;       /* gcmd_stop() was called */
static int           wake[2]={-1,-1};  /* a pipe: gcmd_stop() -> every wait */

/* ---------------------------------------------------------------- */
/* LOCAL */
/* ---------------------------------------------------------------- */

static void say(int level,const char* fmt,...)
{
  char    buf[256];
  va_list ap;

  if ((cfg.on_log == NULL) || stopping) return;  /* no hook after gcmd_stop() */
  va_start(ap,fmt);
  (void)vsnprintf(buf,sizeof(buf),fmt,ap);
  va_end(ap);
  cfg.on_log(level,buf);
}

/* ---------------------------------------------------------------- */

static void set_roi(int now)
{
  int old=telroi;

  if (now == old) return;
  telroi = now;
  if (cfg.on_roi && !stopping) cfg.on_roi(old,now);
}

/* ---------------------------------------------------------------- */

static const char* fields(const char* line,int* roi,long long* seq)
{                                      /* past '<roi> <seq> ', or NULL */
  char* end;

  if (!(((line[0] == '-') || isdigit((unsigned char)line[0])) && (line[1] == ' '))) {
    return NULL;                       /* "-1", or not an answer */
  }
  *roi = ((line[0] >= '0') && (line[0] <= '5')) ? line[0]-'0' : -1;
  if (!isdigit((unsigned char)line[2])) return NULL;
  errno = 0;
  *seq = strtoll(line+2,&end,10);
  if (errno || (*end != ' ')) return NULL;
  return end+1;
}

/* --- */

static int answer(const char* line)    /* 1 an echo, 0 none newer, -1 none */
{
  const char* p;
  long long   seq;
  int         roi;

  if ((p=fields(line,&roi,&seq)) == NULL) return -1;
  if ((p[0] == '-') && (p[1] == '\0')) {  /* nothing newer: none for us */
    after = seq;                       /* up to <latest>, so take it */
    set_roi(roi);
    return 0;
  }
  if ((p[0] == '\0') || (p[0] == ' ') || ((p[1] != ' ') && (p[1] != '\0'))) {
    return -1;
  }
  after = seq;                         /* an echo; <roi> is taken once caught up */
  if ((p[1] != ' ') || (p[2] == '\0')) return 1;  /* blank: nothing to do */
  if (strlen(p+2) >= GCMD_TEXT_MAX) {  /* longer than tcsis ever sends */
    say(1,"echo %lld longer than %d characters, passed over",seq,GCMD_TEXT_MAX-1);
    return 1;
  }
  if (cfg.on_cmd && !stopping) {
    cfg.on_cmd(isdigit((unsigned char)p[0]) ? p[0]-'0' : -1,p+2);
  }
  return 1;
}

/* --- */

static int greet(const char* line)     /* the bare gcmd's answer: 0, or -1 */
{
  const char* p;
  long long   seq;
  int         roi;

  if (((p=fields(line,&roi,&seq)) == NULL) || (p[0] != '-') || (p[1] != '\0')) {
    return -1;                         /* no ring, or no gcmd at all */
  }
  if (after < 0) {                     /* a start: from <latest> on */
    after = seq;
    set_roi(roi);
  } else if (seq < after) {            /* a new ring: all of it is new */
    after = 0;
  }                                    /* else from the cursor */
  return 0;
}

/* ---------------------------------------------------------------- */

static long ms_now(void)
{
  struct timespec t;

  (void)clock_gettime(CLOCK_MONOTONIC,&t);
  return (long)t.tv_sec*1000L + t.tv_nsec/1000000L;
}

/* --- */

static int nap(int ms)                 /* 1 if gcmd_stop() woke it */
{
  struct pollfd p;

  p.fd = wake[0]; p.events = POLLIN; p.revents = 0;
  return (poll(&p,1,ms) > 0) || stopping;
}

/* ---------------------------------------------------------------- */

static int tcp_connect(void)           /* a socket, or -1 */
{
  struct addrinfo hints,*res=NULL,*a;
  struct pollfd   p[2];
  socklen_t       len;
  char            service[16];
  int             sock=-1,flags,err;

  memset(&hints,0,sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(service,sizeof(service),"%d",cfg.port);
  if (getaddrinfo(host,service,&hints,&res) != 0) return -1;

  for (a=res; (a != NULL) && (sock < 0) && !stopping; a=a->ai_next) {
    if ((sock=socket(a->ai_family,a->ai_socktype,a->ai_protocol)) < 0) continue;
    (void)fcntl(sock,F_SETFD,FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    { int on=1; (void)setsockopt(sock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on)); }
#endif
    flags = fcntl(sock,F_GETFL,0);
    (void)fcntl(sock,F_SETFL,flags | O_NONBLOCK);
    if (connect(sock,a->ai_addr,a->ai_addrlen) < 0) {
      err = (errno == EINPROGRESS) ? 0 : errno;
      if (!err) {                      /* wait for it, or gcmd_stop() */
        p[0].fd = sock;    p[0].events = POLLOUT; p[0].revents = 0;
        p[1].fd = wake[0]; p[1].events = POLLIN;  p[1].revents = 0;
        len = sizeof(err);
        if ((poll(p,2,GCMD_CONNECT_MS) <= 0) || p[1].revents ||
            (getsockopt(sock,SOL_SOCKET,SO_ERROR,&err,&len) < 0)) err = ETIMEDOUT;
      }
      if (err) { (void)close(sock); sock = -1; continue; }
    }
    (void)fcntl(sock,F_SETFL,flags);   /* blocking: the waits are poll()s */
  }
  freeaddrinfo(res);
  return sock;
}

/* ---------------------------------------------------------------- */

static int request(int sock,int bare,char* line,size_t n)  /* 0, or -1: lost */
{
  char          req[64];
  size_t        len=0;
  ssize_t       r;
  long          left,deadline;
  struct pollfd p[2];
  char*         nl=NULL;

  if (bare) snprintf(req,sizeof(req),"gcmd\n");  /* a status to any tcsis */
  else      snprintf(req,sizeof(req),"gcmd %d %lld\n",cfg.rotator,after);
  if (send(sock,req,strlen(req),MSG_NOSIGNAL) != (ssize_t)strlen(req)) return -1;

  deadline = ms_now() + GCMD_ANSWER_MS;
  while (nl == NULL) {
    if ((left=deadline-ms_now()) <= 0) return -1;     /* no answer */
    p[0].fd = sock;    p[0].events = POLLIN; p[0].revents = 0;
    p[1].fd = wake[0]; p[1].events = POLLIN; p[1].revents = 0;
    if ((poll(p,2,(int)left) <= 0) || p[1].revents) return -1;
    if (len+1 >= n) return -1;                         /* not an answer */
    if ((r=recv(sock,line+len,n-1-len,0)) <= 0) return -1;  /* closed */
    len += (size_t)r; line[len] = '\0';
    nl = strchr(line,'\n');
  }
  *nl = '\0';
  if ((nl > line) && (nl[-1] == '\r')) nl[-1] = '\0';
  return 0;
}

/* ---------------------------------------------------------------- */

static void* poller(void* arg)         /* the poll thread */
{
  char line[2*GCMD_TEXT_MAX];
  int  sock,r,ok,hello,noring,warned=0,backoff=GCMD_BACKOFF_MIN;

  (void)arg;
  while (!stopping) {
    ok = noring = 0; hello = 1;        /* each connection: the bare gcmd */
    if ((sock=tcp_connect()) >= 0) {
      while (!stopping && (request(sock,hello,line,sizeof(line)) == 0)) {
        if (!ok) {                     /* it answers */
          ok = 1; warned = 0; backoff = GCMD_BACKOFF_MIN;
        }
        r = (hello) ? greet(line) : answer(line);
        if (r < 0) {
          if (!noring) say(1,"tcsis %s:%d answers '%.40s', %s: asking every %d s",
                           host,cfg.port,line,(!strcmp(line,"-1")) ? "no echo ring" :
                           (hello) ? "no gcmd" : "not an echo",GCMD_NORING_MS/1000);
          noring = 1;
          if (nap(GCMD_NORING_MS)) break;
          continue;
        }
        if (noring) say(0,"tcsis %s:%d answers again",host,cfg.port);
        noring = 0;
        if (hello) {                   /* it knows gcmd: poll */
          say(0,"polling %s:%d for port %d",host,cfg.port,cfg.rotator);
          hello = 0;
          continue;
        }
        if (r > 0) continue;           /* an echo: ask again at once */
        if (nap(GCMD_TICK_MS)) break;
      }
      (void)close(sock);
    }
    if (stopping) break;
    set_roi(-1);                       /* not known while it is away; */
    if (!warned) {                     /* the cursor stays.  Once per outage */
      if (ok) say(1,"tcsis %s:%d lost, retrying",host,cfg.port);
      else    say(1,"tcsis %s:%d not answering, retrying",host,cfg.port);
      warned = 1;
    }
    if (nap(1000*backoff)) break;
    backoff = (2*backoff > GCMD_BACKOFF_MAX) ? GCMD_BACKOFF_MAX : 2*backoff;
  }
  running = 0;
  return NULL;
}

/* ---------------------------------------------------------------- */
/* GLOBAL */
/* ---------------------------------------------------------------- */

int gcmd_start(const Gcmd* g)
{
  pthread_t      tid;
  pthread_attr_t attr;
  int            err;

  if (g == NULL) return EINVAL;
  if (running) return EBUSY;           /* one poll per program */
  cfg = *g;
  snprintf(host,sizeof(host),"%s",(g->host) ? g->host : "");
  cfg.host = host;
  telroi = -1; after = -1; stopping = 0;
  configured = (host[0] != '\0');
  if (!configured) return 0;           /* no poll: gcmd_line() works */
  if ((cfg.rotator < 0) || (cfg.rotator > 5) || (cfg.port <= 0)) {
    configured = 0;
    return EINVAL;
  }
  if (wake[0] >= 0) { (void)close(wake[0]); (void)close(wake[1]); }
  if (pipe(wake) != 0) { wake[0] = wake[1] = -1; configured = 0; return errno; }
  (void)fcntl(wake[0],F_SETFD,FD_CLOEXEC);
  (void)fcntl(wake[1],F_SETFD,FD_CLOEXEC);

  running = 1;
  (void)pthread_attr_init(&attr);
  (void)pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
  err = pthread_create(&tid,&attr,poller,NULL);
  (void)pthread_attr_destroy(&attr);
  if (err) running = configured = 0;
  return err;
}

/* ---------------------------------------------------------------- */

void gcmd_stop(void)                   /* also from inside a hook */
{
  ssize_t r;

  stopping = 1;
  if (wake[1] >= 0) {                  /* wake every wait at once */
    r = write(wake[1],"x",1);
    (void)r;
  }
}

/* ---------------------------------------------------------------- */

int gcmd_telroi(void)
{
  return telroi;
}

/* ---------------------------------------------------------------- */

int gcmd_active(void)
{
  return (!configured) || (telroi == cfg.rotator);
}

/* ---------------------------------------------------------------- */

static int fed(const char* text,int bare)  /* one answer, for benches */
{
  char line[2*GCMD_TEXT_MAX];
  char *e;

  if (text == NULL) return -1;
  snprintf(line,sizeof(line),"%s",text);
  if ((e=strpbrk(line,"\r\n")) != NULL) *e = '\0';
  return (bare) ? greet(line) : answer(line);
}

/* --- */

int gcmd_line(const char* text)        /* the thread's parser, for benches */
{
  return fed(text,0);
}

/* --- */

int gcmd_hello(const char* text)       /* the bare gcmd's answer, for benches */
{
  return fed(text,1);
}

/* ---------------------------------------------------------------- */

long long gcmd_after(void)
{
  return after;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
