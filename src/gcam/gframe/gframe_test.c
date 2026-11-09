/* ----------------------------------------------------------------
 *
 * gframe_test.c -- bench for gframe.c
 *
 * Builds alone (make gframe_test in src/gcam): the encoding of the
 * frame, then the server through real sockets on a free local port.
 * Prints one line per check and exits 1 if any failed.
 *
 * 2026-11-09  first version
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gframe.h"

static int nchk=0,nfail=0,verbose=0;

/* ---------------------------------------------------------------- */

static void check(int ok,const char* what)
{
  nchk++;
  if (!ok) nfail++;
  printf("%s - %s\n",(ok) ? "ok  " : "FAIL",what);
}

static void on_log(int level,const char* text)
{
  if (verbose || level) printf("       log%d: %s\n",level,text);
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

static const char* shown(const char* f)  /* "G0300" without CR LF */
{
  static char b[16];
  snprintf(b,sizeof(b),"%.5s",f);
  return b;
}

/* --- encoding ----------------------------------------------------- */

static void enc(double x,double y,const char* want,double rx,double ry)
{
  char   f[GFRAME_LEN+1]="",what[160];
  int    n = gframe_encode(&x,&y,f);
  int    ok;

  if (want) ok = (n == GFRAME_LEN) && !strcmp(f,want);
  else      ok = (n == 0);
  ok = ok && (fabs(x-rx) < 1e-9) && (fabs(y-ry) < 1e-9);
  snprintf(what,sizeof(what),"encode -> %s, %+.3f,%+.3f left (got %s, %+.3f,%+.3f)",
           (want) ? shown(want) : "no frame",rx,ry,(n) ? shown(f) : "no frame",x,y);
  check(ok,what);
}

static void test_encoding(void)
{
  double x,y;
  char   f[GFRAME_LEN+1],what[96];
  int    i,bad=0;

  printf("# encoding, defaults: 0.05\"/count, 2.4\" per frame, 0.1\" dead band\n");
  enc( 0.15, 0.00,"G0300\r\n", 0.00, 0.00);
  enc(-0.15, 0.20,"G9704\r\n", 0.00, 0.00);
  enc( 2.40,-2.40,"G4852\r\n", 0.00, 0.00);
  enc(-2.45, 0.00,"G5200\r\n",-0.05, 0.00);      /* -49 is never written */
  enc( 0.10, 0.10,NULL,        0.10, 0.10);      /* the TCS drops <= 0.1" */
  enc( 0.05,-0.05,NULL,        0.05,-0.05);
  enc( 0.125,0.00,"G0300\r\n",-0.025,0.00);
  enc( 0.00,-0.16,"G0097\r\n", 0.00,-0.01);

  x = 3.0; y = 0.0;                              /* carried over */
  i = gframe_encode(&x,&y,f);
  check(i && !strcmp(f,"G4800\r\n") && fabs(x-0.6) < 1e-9,"3.0\" -> G4800, 0.6\" left");
  i = gframe_encode(&x,&y,f);
  check(i && !strcmp(f,"G1200\r\n") && fabs(x) < 1e-9,"then G1200, nothing left");

  for (x=-5.0; x<=5.0; x+=0.01) {                /* never 50 or 51 on the wire */
    double a=x,b=-x;
    if (gframe_encode(&a,&b,f)) {
      int xx = (f[1]-'0')*10 + (f[2]-'0');
      int yy = (f[3]-'0')*10 + (f[4]-'0');
      if ((xx == 50) || (xx == 51) || (yy == 50) || (yy == 51) ||
          (f[0] != 'G') || (f[5] != '\r') || (f[6] != '\n')) bad++;
    }
  }
  snprintf(what,sizeof(what),"sweep -5..+5\": no 50/51, always G..CR LF (%d bad)",bad);
  check(!bad,what);

  printf("# signs\n");
  (void)gframe_ini("gframe_xsign","-1");
  enc( 0.15, 0.15,"G9703\r\n", 0.00, 0.00);
  (void)gframe_ini("gframe_ysign","-1");
  enc( 0.15, 0.15,"G9797\r\n", 0.00, 0.00);
  (void)gframe_ini("gframe_xsign","1");
  (void)gframe_ini("gframe_ysign","+1");
  enc( 0.15, 0.15,"G0303\r\n", 0.00, 0.00);

  printf("# hundredths (the DOS guider PC's own unit)\n");
  (void)gframe_ini("gframe_unit","0.01");
  (void)gframe_ini("gframe_max","0.49");
  enc( 0.30,-0.20,"G3080\r\n", 0.00, 0.00);
  enc( 0.70, 0.00,"G4800\r\n", 0.22, 0.00);
  (void)gframe_ini("gframe_unit","0.05");
  (void)gframe_ini("gframe_max","2.4");

  printf("# keys\n");
  check(gframe_ini("gframe_min","0.1") == 1,"gframe_min is taken");
  check(gframe_ini("gframe_bogus","1") == 0,"an unknown gframe_ key is not");
  check(gframe_ini("gain","hi") == 0,"another program's key is not");
  check(!gframe_enabled(),"no gframe_port: off");
}

/* --- server ------------------------------------------------------- */

static int free_port(void)
{
  struct sockaddr_in a;
  socklen_t al=sizeof(a);
  int s = socket(AF_INET,SOCK_STREAM,0),p=0;

  memset(&a,0,sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (!bind(s,(struct sockaddr*)&a,sizeof(a)) &&
      !getsockname(s,(struct sockaddr*)&a,&al)) p = ntohs(a.sin_port);
  close(s);
  return p;
}

static int tcs_connect(int port)       /* what the Swope TCS does */
{
  struct sockaddr_in a;
  int s = socket(AF_INET,SOCK_STREAM,0);

  memset(&a,0,sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(s,(struct sockaddr*)&a,sizeof(a))) { close(s); return -1; }
  return s;
}

static int wait_clients(int n,double tout)
{
  double t0=now();
  while (now()-t0 < tout) {
    if (gframe_clients() == n) return 1;
    nap(0.02);
  }
  return (gframe_clients() == n);
}

/* one frame within tout: 1 and 'f' filled, 0 none, -1 closed */
static int read_frame(int s,char* f,double tout)
{
  int    got=0;
  double t0=now();

  while (got < GFRAME_LEN) {
    struct pollfd p = { s,POLLIN,0 };
    int ms = (int)(1000.0*(tout-(now()-t0)));
    if (ms <= 0) return 0;
    if (poll(&p,1,ms) <= 0) return 0;
    ssize_t k = recv(s,f+got,(size_t)(GFRAME_LEN-got),0);
    if (k <= 0) return -1;
    got += (int)k;
  }
  f[GFRAME_LEN] = '\0';
  return 1;
}

static int pending_zero(void)
{
  char st[128];
  gframe_status(st,sizeof(st));
  return (strstr(st,"gpend=+0.00,+0.00") != NULL);
}

static void test_server(void)
{
  char   f[GFRAME_LEN+1],g[GFRAME_LEN+1],st[128],buf[16],what[128];
  int    port=free_port(),c1,c2,extra[GFRAME_CLIENTS_MAX],i,r;
  double t1,t2,t0;

  printf("# server on 127.0.0.1:%d, period 0.5 s\n",port);
  snprintf(buf,sizeof(buf),"%d",port);
  check(gframe_ini("gframe_port",buf) == 1 && gframe_enabled(),"gframe_port turns it on");
  (void)gframe_ini("gframe_period","0.5");
  check(gframe_start(on_log) == 0,"starts");
  check(gframe_start(on_log) == 0,"a second start is harmless");
  check(gframe_clients() == 0,"no clients yet");

  gframe_add(1.0,1.0);                           /* nobody to send to */
  nap(0.3);
  gframe_status(st,sizeof(st));
  check(pending_zero() && strstr(st,"gdrop=1"),"with no client the pending pair is dropped");

  c1 = tcs_connect(port);
  check((c1 >= 0) && wait_clients(1,1.0),"the TCS connects");

  gframe_add(0.30,-0.20);
  r = read_frame(c1,f,1.0);
  t1 = now();
  check((r == 1) && !strcmp(f,"G0696\r\n"),"0.30,-0.20 arrives as G0696");

  gframe_add(0.30,0.0);                          /* at once: waits a period */
  r = read_frame(c1,f,1.5);
  t2 = now();
  snprintf(what,sizeof(what),"the next frame waits the period (%.2f s)",t2-t1);
  check((r == 1) && !strcmp(f,"G0600\r\n") && (t2-t1 >= 0.45),what);

  c2 = tcs_connect(port);
  check((c2 >= 0) && wait_clients(2,1.0),"a second client (a listener) connects");
  nap(0.5);
  gframe_add(3.0,0.0);                           /* 2.4" now, 0.6" next */
  r  = read_frame(c1,f,1.5);
  i  = read_frame(c2,g,0.5);
  check((r == 1) && (i == 1) && !strcmp(f,"G4800\r\n") && !strcmp(g,f),
        "both clients get the frame, G4800");
  r = read_frame(c1,f,1.5);
  check((r == 1) && !strcmp(f,"G1200\r\n"),"the 0.6\" left follows, G1200");
  (void)read_frame(c2,g,0.5);

  gframe_add(1.0,1.0);                           /* inside the period */
  gframe_clear();                                /* F1 */
  r = read_frame(c1,f,1.0);
  check((r == 0) && pending_zero(),"gframe_clear() drops it: nothing is sent");

  gframe_add(0.05,0.0);                          /* below the dead band */
  nap(0.7);
  gframe_status(st,sizeof(st));
  check(strstr(st,"gpend=+0.05,+0.00") != NULL,"0.05\" waits for more");
  t0 = now();
  while (!pending_zero() && (now()-t0 < 4.0)) nap(0.1);
  snprintf(what,sizeof(what),"and is dropped after 3 s without corrections (%.1f s)",
           0.7+now()-t0);
  check(pending_zero() && (0.7+now()-t0 > 2.9),what);

  close(c2);
  check(wait_clients(1,1.0),"a client that closes is dropped");

  for (i=0; i<GFRAME_CLIENTS_MAX; i++) extra[i] = tcs_connect(port);
  nap(0.3);
  r = read_frame(extra[GFRAME_CLIENTS_MAX-1],f,0.5);
  check((gframe_clients() == GFRAME_CLIENTS_MAX) && (r == -1),
        "clients beyond the limit are closed at once");
  for (i=0; i<GFRAME_CLIENTS_MAX; i++) if (extra[i] >= 0) close(extra[i]);
  check(wait_clients(1,1.0),"back to one client");

  t0 = now();
  gframe_stop();
  snprintf(what,sizeof(what),"gframe_stop() returns at once (%.2f s)",now()-t0);
  check(now()-t0 < 0.5,what);
  r = read_frame(c1,f,1.0);
  check((r == -1) && (gframe_clients() == 0),"and closes the TCS's connection");
  close(c1);
  check(tcs_connect(port) < 0,"the port is closed");
}

/* ---------------------------------------------------------------- */

int main(int argc,char** argv)
{
  verbose = (argc > 1) && !strcmp(argv[1],"-v");
  test_encoding();
  test_server();
  printf("%d checks, %d failed\n",nchk,nfail);
  return (nfail) ? 1 : 0;
}

/* ---------------------------------------------------------------- */
