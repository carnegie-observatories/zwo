/* ----------------------------------------------------------------
 *
 * goffset_test.c -- bench for goffset.c
 *
 * Builds alone (make goffset_test in src/gcam).  A scripted stand-in
 * for the Swope app's command port listens on a free local port and
 * answers each line as the case asks: at once, late, in two pieces,
 * with or without a line end, never, or by closing.  Like the app it
 * keeps the connection until the client closes it.  Prints one line
 * per check and exits 1 if any failed.
 *
 * 2026-11-22  first version
 *
 * ---------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "goffset.h"

static int nchk=0,nfail=0;

/* ---------------------------------------------------------------- */

static void check(int ok,const char* what)
{
  nchk++;
  if (!ok) nfail++;
  printf("%s - %s\n",(ok) ? "ok  " : "FAIL",what);
}

static double now(void)
{
  struct timespec ts;
  (void)clock_gettime(CLOCK_MONOTONIC,&ts);
  return (double)ts.tv_sec + 1e-9*(double)ts.tv_nsec;
}

static void nap(double s)
{
  usleep((useconds_t)(s*1e6));
}

/* --- the stand-in ------------------------------------------------ */

enum { A_REPLY, A_SPLIT, A_SILENT, A_CLOSE };

static struct {
  int    action;
  char   reply[128],tail[128];         /* A_SPLIT: reply, 50 ms, tail */
  double delay;                        /* s before the reply */
} plan;

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static char   got[256];                /* the last line received */
static int    ngot=0;                  /* lines received */
static int    lsock=-1;
static volatile int stop=0;

static void serve(int c)               /* one connection */
{
  char   buf[256];
  int    n=0,r,action;
  struct pollfd p;

  while (n < (int)sizeof(buf)-1) {     /* the line */
    p.fd = c; p.events = POLLIN; p.revents = 0;
    if (poll(&p,1,2000) < 1) break;
    if ((r = (int)recv(c,buf+n,sizeof(buf)-1-n,0)) <= 0) break;
    n += r;
    if (memchr(buf,'\n',n)) break;
  }
  buf[n] = '\0';
  pthread_mutex_lock(&lock);
  snprintf(got,sizeof(got),"%s",buf);
  ngot++;
  action = plan.action;
  pthread_mutex_unlock(&lock);

  if (action == A_CLOSE) { (void)close(c); return; }
  if (action != A_SILENT) {
    nap(plan.delay);
    (void)send(c,plan.reply,strlen(plan.reply),0);
    if (action == A_SPLIT) {
      nap(0.05);
      (void)send(c,plan.tail,strlen(plan.tail),0);
    }
  }
  while (!stop) {                      /* as the app: open until the */
    p.fd = c; p.events = POLLIN; p.revents = 0; /* client closes */
    r = poll(&p,1,100);
    if (r < 0) break;
    if ((r == 1) && (recv(c,buf,sizeof(buf),0) <= 0)) break;
  }
  (void)close(c);
}

static void* run_app(void* arg)
{
  struct pollfd p;
  int c;
  (void)arg;

  while (!stop) {
    p.fd = lsock; p.events = POLLIN; p.revents = 0;
    if (poll(&p,1,100) < 1) continue;
    if ((c = accept(lsock,NULL,NULL)) < 0) continue;
    serve(c);
  }
  return NULL;
}

static int start_app(void)             /* the port, 0 on failure */
{
  struct sockaddr_in a;
  socklen_t len=sizeof(a);
  int on=1;

  if ((lsock = socket(AF_INET,SOCK_STREAM,0)) < 0) return 0;
  (void)setsockopt(lsock,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
  memset(&a,0,sizeof(a));
  a.sin_family      = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port        = 0;
  if (bind(lsock,(struct sockaddr*)&a,sizeof(a)) || listen(lsock,4)) return 0;
  if (getsockname(lsock,(struct sockaddr*)&a,&len)) return 0;
  return ntohs(a.sin_port);
}

static int free_port(void)             /* a port nobody listens on */
{
  struct sockaddr_in a;
  socklen_t len=sizeof(a);
  int s,port=0;

  if ((s = socket(AF_INET,SOCK_STREAM,0)) < 0) return 0;
  memset(&a,0,sizeof(a));
  a.sin_family      = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (!bind(s,(struct sockaddr*)&a,sizeof(a)) &&
      !getsockname(s,(struct sockaddr*)&a,&len)) port = ntohs(a.sin_port);
  (void)close(s);
  return port;
}

static void answer(int action,const char* reply,const char* tail,double delay)
{
  pthread_mutex_lock(&lock);
  plan.action = action;
  snprintf(plan.reply,sizeof(plan.reply),"%s",(reply) ? reply : "");
  snprintf(plan.tail,sizeof(plan.tail),"%s",(tail) ? tail : "");
  plan.delay = delay;
  got[0] = '\0';
  pthread_mutex_unlock(&lock);
}

static int last_line(char* buf,int len) /* lines received so far */
{
  int n;
  pthread_mutex_lock(&lock);
  snprintf(buf,len,"%s",got);
  n = ngot;
  pthread_mutex_unlock(&lock);
  return n;
}

/* ---------------------------------------------------------------- */

int main(int argc,char** argv)
{
  pthread_t tid;
  char   msg[GOFFSET_MSG],line[256],what[512],v[16];
  int    port,dead,err,n0;
  double t0,dt;
  (void)argc; (void)argv;

  printf("goffset bench\n");

  /* --- keys ------------------------------------------------------ */
  check(!goffset_enabled(),"off without offset_port");
  err = goffset_tele(1.0,1.0,msg,sizeof(msg));
  check(err == GOFFSET_OFF,"an offset with offset_port unset: GOFFSET_OFF");
  check(goffset_ini("offset_host","127.0.0.1") == 1,"offset_host is ours");
  check(goffset_ini("offset_wait","1") == 1,"offset_wait is ours");
  check(goffset_ini("probe_xsign","1") == 1,"probe_xsign is ours");
  check(goffset_ini("probe_ysign","1") == 1,"probe_ysign is ours");
  check(goffset_ini("gframe_port","10001") == 0,"gframe_port is not ours");
  check(goffset_ini("px","50") == 0,"px is not ours");
  check(goffset_ini("offset_port","0") == 1 && !goffset_enabled(),
        "offset_port 0: off");
  check(goffset_ini("offset_port","70000") == 1 && !goffset_enabled(),
        "offset_port 70000: off");

  if (!(port = start_app())) {
    printf("FAIL - cannot start the stand-in: %s\n",strerror(errno));
    return 1;
  }
  if (pthread_create(&tid,NULL,run_app,NULL)) {
    printf("FAIL - cannot start the stand-in's thread\n");
    return 1;
  }
  snprintf(v,sizeof(v),"%d",port);
  check(goffset_ini("offset_port",v) == 1 && goffset_enabled(),
        "offset_port set: on");

  /* --- the lines and ok ------------------------------------------ */
  answer(A_REPLY,"ok",NULL,0.0);
  err = goffset_probe(2,1.5,-2.25,msg,sizeof(msg));
  (void)last_line(line,sizeof(line));
  snprintf(what,sizeof(what),"probe line \"%.*s\"",(int)strcspn(line,"\n"),line);
  check(!strcmp(line,"guider_offset 2 1.50 -2.25\n"),what);
  check((err == GOFFSET_OK) && !strcmp(msg,"ok"),"probe: ok");

  answer(A_REPLY,"ok",NULL,0.0);
  err = goffset_tele(-10.0,3.2,msg,sizeof(msg));
  (void)last_line(line,sizeof(line));
  snprintf(what,sizeof(what),"tele line \"%.*s\"",(int)strcspn(line,"\n"),line);
  check(!strcmp(line,"tele_offset -10.00 3.20\n"),what);
  check(err == GOFFSET_OK,"tele: ok");

  answer(A_REPLY,"ok\n",NULL,0.0);     /* the app's answers end so */
  t0 = now();
  err = goffset_tele(0.5,0.5,msg,sizeof(msg));
  dt = now()-t0;
  snprintf(what,sizeof(what),"\"ok\\n\": ok at the line end, %.3f s",dt);
  check((err == GOFFSET_OK) && !strcmp(msg,"ok") && (dt < 0.15),what);
  answer(A_REPLY,"-Ea move is running\n",NULL,0.0);
  err = goffset_tele(0.5,0.5,msg,sizeof(msg));
  check((err == GOFFSET_REFUSED) && !strcmp(msg,"a move is running"),
        "\"-E...\\n\": the reason without the line end");

  answer(A_REPLY,"ok 12.50 -3.00 0",NULL,0.0);
  err = goffset_tele(0.5,0.5,msg,sizeof(msg));
  check((err == GOFFSET_OK) && !strcmp(msg,"ok 12.50 -3.00 0"),
        "\"ok\" with values: ok, kept in msg");

  /* --- signs ----------------------------------------------------- */
  (void)goffset_ini("probe_xsign","-1");
  answer(A_REPLY,"ok",NULL,0.0);
  (void)goffset_probe(2,1.5,-2.25,msg,sizeof(msg));
  (void)last_line(line,sizeof(line));
  check(!strcmp(line,"guider_offset 2 -1.50 -2.25\n"),"probe_xsign -1 flips x");
  (void)goffset_ini("probe_xsign","1");
  (void)goffset_ini("probe_ysign","-1");
  answer(A_REPLY,"ok",NULL,0.0);
  (void)goffset_probe(1,1.5,-2.25,msg,sizeof(msg));
  (void)last_line(line,sizeof(line));
  check(!strcmp(line,"guider_offset 1 1.50 2.25\n"),"probe_ysign -1 flips y");
  answer(A_REPLY,"ok",NULL,0.0);
  (void)goffset_tele(1.5,-2.25,msg,sizeof(msg));
  (void)last_line(line,sizeof(line));
  check(!strcmp(line,"tele_offset 1.50 -2.25\n"),"the probe signs leave tele alone");
  (void)goffset_ini("probe_ysign","1");

  /* --- refusals -------------------------------------------------- */
  answer(A_REPLY,"-Ea move is running",NULL,0.0);
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  check((err == GOFFSET_REFUSED) && !strcmp(msg,"a move is running"),
        "-E: refused, the reason in msg");
  answer(A_REPLY,"-E: guider 2 is moving",NULL,0.0);
  err = goffset_probe(2,5.0,5.0,msg,sizeof(msg));
  check((err == GOFFSET_REFUSED) && !strcmp(msg,"guider 2 is moving"),
        "-E with ': ': the reason trimmed");
  answer(A_REPLY,"-E",NULL,0.0);
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  check((err == GOFFSET_REFUSED) && !strcmp(msg,"refused"),"bare -E: \"refused\"");
  answer(A_SPLIT,"-Ethe telescope moved only ","0.2\" of 3.2\" in Dec",0.0);
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  check((err == GOFFSET_REFUSED) &&
        !strcmp(msg,"the telescope moved only 0.2\" of 3.2\" in Dec"),
        "an answer in two pieces 50 ms apart: whole");
  answer(A_REPLY,"hello",NULL,0.0);
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  check(err == GOFFSET_BADREPLY,"neither ok nor -E: GOFFSET_BADREPLY");
  answer(A_REPLY,"okay",NULL,0.0);
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  check(err == GOFFSET_BADREPLY,"\"okay\" is not ok");

  /* --- not a number: nothing sent -------------------------------- */
  n0 = last_line(line,sizeof(line));
  err = goffset_tele(NAN,1.0,msg,sizeof(msg));
  check((err == GOFFSET_REFUSED) && (last_line(line,sizeof(line)) == n0),
        "NaN: refused, nothing sent");
  err = goffset_probe(2,1.0,INFINITY,msg,sizeof(msg));
  check((err == GOFFSET_REFUSED) && (last_line(line,sizeof(line)) == n0),
        "inf: refused, nothing sent");

  /* --- timing: the answer comes when the move has ended ---------- */
  answer(A_REPLY,"ok",NULL,0.8);
  t0 = now();
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  dt = now()-t0;
  snprintf(what,sizeof(what),"an answer after a 0.8 s move: ok after %.2f s",dt);
  check((err == GOFFSET_OK) && (dt >= 0.8) && (dt < 0.8+0.6),what);

  answer(A_SILENT,NULL,NULL,0.0);
  t0 = now();
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  dt = now()-t0;
  snprintf(what,sizeof(what),"no answer, offset_wait 1: timeout after %.2f s (%s)",
           dt,msg);
  check((err == GOFFSET_TIMEOUT) && (dt >= 1.0) && (dt < 1.5),what);

  answer(A_CLOSE,NULL,NULL,0.0);
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  snprintf(what,sizeof(what),"closed without an answer: timeout (%s)",msg);
  check((err == GOFFSET_TIMEOUT) && strstr(msg,"closed"),what);

  /* --- no link --------------------------------------------------- */
  if ((dead = free_port())) {
    snprintf(v,sizeof(v),"%d",dead);
    (void)goffset_ini("offset_port",v);
    t0 = now();
    err = goffset_tele(5.0,5.0,msg,sizeof(msg));
    dt = now()-t0;
    snprintf(what,sizeof(what),"nobody listening: no link in %.2f s (%s)",dt,msg);
    check((err == GOFFSET_NOLINK) && (dt < 0.5),what);
  }
  (void)goffset_ini("offset_host","no-such-host.invalid");
  err = goffset_tele(5.0,5.0,msg,sizeof(msg));
  snprintf(what,sizeof(what),"unknown host: no link (%s)",msg);
  check(err == GOFFSET_NOLINK,what);

  stop = 1;
  pthread_join(tid,NULL);
  (void)close(lsock);

  printf("%d of %d checks passed\n",nchk-nfail,nchk);
  return (nfail) ? 1 : 0;
}

/* ---------------------------------------------------------------- */
