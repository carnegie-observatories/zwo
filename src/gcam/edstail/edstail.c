/* ----------------------------------------------------------------
 *
 * edstail.c -- a tcsif client for the Magellan EDS stream
 *
 * Connects to tcsif (EDSTAIL_PORT), subscribes with a regexp, reads
 * one record per line and reconnects with a back-off when the stream
 * ends.  Each '~Z2' record is parsed by column: Z802 latches the
 * rotator of interest; Z81x is a command echo, handed to on_cmd while
 * the latch equals the host's own rotator and to on_drop otherwise.
 *
 * Shared by zwo (gcamzwo) and IMACS (mechgui): standard headers only,
 * nothing from either program, so the same file compiles in both.
 * Record layout and behaviour: README.md.
 *
 * 2026-10-31  first version: the core of IMACS v3.23's gpc.c
 *
 * ---------------------------------------------------------------- */

#define _REENTRANT                     /* multi-threaded code */

#include <stdio.h>                     /* snprintf() */
#include <stdarg.h>                    /* va_list */
#include <string.h>                    /* strncmp(),strerror() */
#include <ctype.h>                     /* isdigit(),isspace() */
#include <errno.h>
#include <fcntl.h>                     /* fcntl() */
#include <poll.h>                      /* poll() */
#include <unistd.h>                    /* close(),sleep() */
#include <pthread.h>
#include <netdb.h>                     /* getaddrinfo() */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "edstail.h"

/* DEFINEs -------------------------------------------------------- */

#define RBUF            4096           /* receive chunk */
#define READ_TOUT       30             /* s; Z802 arrives at 5 Hz */
#define CONN_TOUT       10             /* s for connect() */
#define BACKOFF_MIN     5              /* s between failed attempts */
#define BACKOFF_MAX     120

#define ROI_COL         51             /* Z802: rotator of interest */
#define SENDER_COL      17             /* Z81x: 0 console .. 4 network */
#define TEXT_COL        20             /* Z81x: echoed command, cols 20-72 */
#define TEXT_LEN        53

#ifndef MSG_NOSIGNAL                   /* macOS: SO_NOSIGPIPE instead */
#define MSG_NOSIGNAL    0
#endif

/* STATICs -------------------------------------------------------- */

static EdsTail         tail;           /* the host's settings, copied */
static char            tail_host[128];
static char            tail_regexp[EDSTAIL_LINE_MAX];
static volatile int    telroi=-1;      /* latched rotator of interest */
static volatile int    tail_on=0;      /* the reader should run */
static volatile int    reader_up=0;    /* the reader thread exists */
static int             tail_sock=-1;   /* the reader's socket, under 'lock' */
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;

/* ---------------------------------------------------------------- */
/* LOCAL */
/* ---------------------------------------------------------------- */

static void say(int level,const char* fmt,...)
{
  char    buf[512];
  va_list ap;

  if (!tail.on_log) return;
  va_start(ap,fmt);
  (void)vsnprintf(buf,sizeof(buf),fmt,ap);
  va_end(ap);
  tail.on_log(level,buf);
}

/* ---------------------------------------------------------------- */

static void latch(int now)             /* the rotator of interest */
{
  int old=telroi;

  if (now == old) return;
  telroi = now;
  if (tail.on_roi) tail.on_roi(old,now);
}

/* ---------------------------------------------------------------- */

static void nap(int seconds)           /* sleep, wake up for a stop */
{
  while ((seconds-- > 0) && tail_on) sleep(1);
}

/* ---------------------------------------------------------------- */

static int wait_fd(int sock,short events,int seconds)
{                                      /* 1 ready, 0 timeout, -1 error */
  struct pollfd p;
  int           r;

  p.fd = sock; p.events = events; p.revents = 0;
  do {
    r = poll(&p,1,1000*seconds);
  } while ((r < 0) && (errno == EINTR));
  return (r > 0) ? 1 : r;
}

/* ---------------------------------------------------------------- */

static int dial(const char* host,int port,char* why,size_t n)
{                                      /* connected socket, or -1 */
  struct addrinfo hints,*list=NULL,*a;
  char            service[16];
  int             sock=-1,r,flags,e=ECONNREFUSED;
  socklen_t       len;

  memset(&hints,0,sizeof(hints));
  hints.ai_family   = AF_INET;         /* tcsif checks IPv4 peers */
  hints.ai_socktype = SOCK_STREAM;
  snprintf(service,sizeof(service),"%d",port);
  if ((r=getaddrinfo(host,service,&hints,&list)) != 0) {
    snprintf(why,n,"%s",gai_strerror(r));
    return -1;
  }
  for (a=list; a!=NULL; a=a->ai_next) {
    if ((sock=socket(a->ai_family,a->ai_socktype,a->ai_protocol)) < 0) {
      e = errno; continue;
    }
    (void)fcntl(sock,F_SETFD,FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    { int on=1; (void)setsockopt(sock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on)); }
#endif
    flags = fcntl(sock,F_GETFL,0);     /* connect with a timeout */
    (void)fcntl(sock,F_SETFL,flags | O_NONBLOCK);
    r = connect(sock,a->ai_addr,a->ai_addrlen);
    if (r < 0) e = errno;
    if ((r < 0) && (e == EINPROGRESS)) {
      r = wait_fd(sock,POLLOUT,CONN_TOUT);
      if (r == 0) { e = ETIMEDOUT; r = -1; }
      else if (r < 0) e = errno;
      else {
        len = sizeof(e);
        if (getsockopt(sock,SOL_SOCKET,SO_ERROR,&e,&len) < 0) e = errno;
        r = (e) ? -1 : 0;
      }
    }
    if (r == 0) { (void)fcntl(sock,F_SETFL,flags); break; }
    (void)close(sock); sock = -1;
  }
  freeaddrinfo(list);
  if (sock < 0) snprintf(why,n,"%s",strerror(e));
  return sock;
}

/* ---------------------------------------------------------------- */

static int send_all(int sock,const char* p,size_t n)
{
  ssize_t r;

  while (n > 0) {
    r = send(sock,p,n,MSG_NOSIGNAL);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    p += r; n -= (size_t)r;
  }
  return 0;
}

/* ---------------------------------------------------------------- */

static ssize_t recv_tout(int sock,char* buf,size_t n,int seconds)
{                                      /* bytes, 0 closed, -1 errno */
  ssize_t r;
  int     w;

  for (;;) {
    if ((w=wait_fd(sock,POLLIN,seconds)) == 0) { errno = ETIMEDOUT; return -1; }
    if (w < 0) return -1;
    r = recv(sock,buf,n,0);
    if ((r < 0) && (errno == EINTR)) continue;
    return r;
  }
}

/* ---------------------------------------------------------------- */

static void* reader(void* param)       /* the EDS reader thread */
{
  int     sock,i,li,lines,closed,down=0,backoff=BACKOFF_MIN;
  int     port=(tail.port > 0) ? tail.port : EDSTAIL_PORT;
  ssize_t r;
  char    rbuf[RBUF],line[EDSTAIL_LINE_MAX],why[128];

  (void)param;

  while (tail_on) {
    if ((sock=dial(tail_host,port,why,sizeof(why))) < 0) {
      if (!down) say(1,"cannot reach %s:%d (%s), retrying",tail_host,port,why);
      down = 1; nap(backoff);
      backoff = (2*backoff < BACKOFF_MAX) ? 2*backoff : BACKOFF_MAX;
      continue;
    }
    /* subscribe: the regexp followed by a NUL byte (tcscl handshake) */
    if (send_all(sock,tail_regexp,strlen(tail_regexp)+1) < 0) {
      if (!down) say(1,"subscription to %s:%d failed (%s), retrying",
                     tail_host,port,strerror(errno));
      (void)close(sock);
      down = 1; nap(backoff);
      backoff = (2*backoff < BACKOFF_MAX) ? 2*backoff : BACKOFF_MAX;
      continue;
    }
    pthread_mutex_lock(&lock); tail_sock = sock; pthread_mutex_unlock(&lock);

    li = lines = closed = 0;
    while (tail_on) {                  /* read records */
      r = recv_tout(sock,rbuf,sizeof(rbuf),READ_TOUT);
      if (r <= 0) {                    /* refused peers see a close or reset */
        closed = (r == 0) || (errno == ECONNRESET);
        if      (r == 0)              snprintf(why,sizeof(why),"closed by the server");
        else if (errno == ECONNRESET) snprintf(why,sizeof(why),"reset by the server");
        else if (errno == ETIMEDOUT)  snprintf(why,sizeof(why),"silent for %d s",READ_TOUT);
        else                          snprintf(why,sizeof(why),"%s",strerror(errno));
        break;
      }
      for (i=0; (i<(int)r) && tail_on; i++) {
        if (rbuf[i] == '\n') {
          while ((li > 0) && isspace((unsigned char)line[li-1])) li--;
          line[li] = '\0';
          if (li > 0) {
            if (lines++ == 0) {        /* the stream is up */
              say(0,"tailing %s:%d, own rotator %d",tail_host,port,tail.rotator);
              down = 0; backoff = BACKOFF_MIN;
            }
            edstail_line(line);
          }
          li = 0;
        } else if (li < EDSTAIL_LINE_MAX-1) line[li++] = rbuf[i];
      }
    }
    pthread_mutex_lock(&lock); tail_sock = -1; pthread_mutex_unlock(&lock);
    (void)close(sock);
    if (!tail_on) break;

    if (lines > 0) {                   /* reconnect at once */
      say(1,"stream from %s lost (%s), reconnecting",tail_host,why);
    } else {                           /* refused: count as a failure */
      if (!down) say(1,"stream from %s:%d %s before the first record%s, retrying",
                     tail_host,port,why,
                     (closed) ? " (is this host in tcsif's hosts5601?)" : "");
      down = 1;
    }
    latch(-1);                         /* unknown until the next Z802 */
    if (lines == 0) {
      nap(backoff);
      backoff = (2*backoff < BACKOFF_MAX) ? 2*backoff : BACKOFF_MAX;
    }
  }
  telroi = -1;                         /* stopped: unknown, no hook */
  reader_up = 0;
  return NULL;
}

/* ---------------------------------------------------------------- */
/* GLOBAL */
/* ---------------------------------------------------------------- */

void edstail_line(const char* line)    /* handle one EDS record */
{
  size_t len,n;
  int    sender;
  char   text[TEXT_LEN+1],*p;

  if (line == NULL) return;
  len = strlen(line);
  if ((len < 18) || strncmp(line,"~Z2",3)) return;  /* TCS records only */

  if (!strncmp(line+15,"802",3)) {     /* rotator of interest */
    if ((len > ROI_COL) && isdigit((unsigned char)line[ROI_COL])) {
      latch(line[ROI_COL]-'0');
    }
    return;
  }
  if (strncmp(line+15,"81",2)) return; /* not a command echo */

  sender = (isdigit((unsigned char)line[SENDER_COL])) ? line[SENDER_COL]-'0' : -1;
  n = (len > TEXT_COL) ? len-TEXT_COL : 0;
  if (n > TEXT_LEN) n = TEXT_LEN;
  memcpy(text,line+TEXT_COL,n); text[n] = '\0';
  while ((n > 0) && isspace((unsigned char)text[n-1])) text[--n] = '\0';
  for (p=text; isspace((unsigned char)*p); p++) ;
  if (*p == '\0') return;

  if (telroi == tail.rotator) {        /* the gate */
    if (tail.on_cmd)  tail.on_cmd(sender,p);
  } else {
    if (tail.on_drop) tail.on_drop(sender,p);
  }
}

/* ---------------------------------------------------------------- */

int edstail_start(const EdsTail* t)
{
  pthread_t      tid;
  pthread_attr_t attr;
  int            err;

  if (t == NULL) return EINVAL;
  if (reader_up) return EBUSY;         /* one reader per program */
  tail = *t;
  snprintf(tail_host,sizeof(tail_host),"%s",(t->host) ? t->host : "");
  snprintf(tail_regexp,sizeof(tail_regexp),"%s",(t->regexp) ? t->regexp : "");
  tail.host = tail_host; tail.regexp = tail_regexp;
  telroi = -1;
  if (*tail_host == '\0') return 0;    /* tail off: edstail_line() works */

  tail_on = reader_up = 1;
  (void)pthread_attr_init(&attr);
  (void)pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
  err = pthread_create(&tid,&attr,reader,NULL);
  (void)pthread_attr_destroy(&attr);
  if (err) tail_on = reader_up = 0;
  return err;
}

/* ---------------------------------------------------------------- */

void edstail_stop(void)
{
  tail_on = 0;
  pthread_mutex_lock(&lock);
  if (tail_sock >= 0) (void)shutdown(tail_sock,SHUT_RDWR);  /* wake up */
  pthread_mutex_unlock(&lock);
}

/* ---------------------------------------------------------------- */

int edstail_telroi(void)
{
  return telroi;
}

/* ---------------------------------------------------------------- */

int edstail_active(void)               /* may the host act now ? */
{
  if (!tail_on) return 1;              /* no tail: nothing to gate on */
  return (telroi == tail.rotator);
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
