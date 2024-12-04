/* ----------------------------------------------------------------
 *
 * telio.c
 * 
 * Project: GcamZWO  (OCIW, Pasadena, CA)
 *
 * Magellan TCS (TCP/IP) I/O functions ...  
 *
 * 2002-03-08  Christoph C. Birk, birk@obs.carnegiescience.edu
 * 2002-04-12  [CCB]
 * 2012-01-11  telio_port=5810 [CCB]
 * 2013-05-21  generic Magellan version
 *
 * http://espejo.lco.cl/operations/TCS_communication.html
 *
 * ---------------------------------------------------------------- */

#define IS_TELIO_C

/* DEFINEs -------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           1              /* debug level */
#endif

#define GUIDER_HOST     "localhost"
#define GUIDER_PORT     5903

#define TELIO_TIMEOUT   5              /* open/lock [seconds] */

#define PREFUN          __func__

/* INCLUDES ------------------------------------------------------- */

#define _REENTRANT

#include <stdlib.h>                    /* atoi() */
#include <string.h>                    /* strcpy() */
#include <stdio.h>                     /* fprintf() */
#include <unistd.h>                    /* sleep() */
#include <time.h>                      /* time() */
#include <math.h>                      /* fabs() */
#include <ctype.h>                     /* isspace() */
#include <pthread.h>                   /* mutex and thread creation */
#include <assert.h>

#include "telio.h"
#include "utils.h"
#include "tcpip.h"                     /* TCP/IP lib */

/* GLOBALs -------------------------------------------------------- */


/* EXTERNs -------------------------------------------------------- */

/* none */

/* STATICs -------------------------------------------------------- */

static pthread_mutex_t telio_lock;
static int             telio_sock=-1;
static volatile int    telio_count=0;
static u_short         telio_port=0;

/* function prototype(s) */

static int  tcs_command (const char*);
static int  tcs_request (const char*,char*);

/* ---------------------------------------------------------------- */
/* LOCAL */
/* ---------------------------------------------------------------- */

static int tcs_lock(int timeout)       /* seconds */
{
#if (DEBUG > 3)
  fprintf(stderr,"%s(%d)\n",PREFUN,timeout);
  assert(timeout > 0);
  assert(timeout < 100);
#endif
  timeout *= 1000;                     /* milli-seconds */

  while (pthread_mutex_trylock(&telio_lock)) {
    if (timeout <= 0) return E_telio_lock;
    msleep(250); timeout -= 250;
  }
  return 0;
}

static void tcs_unlock(void)
{
#if (DEBUG > 3)
  fprintf(stderr,"%s()\n",PREFUN);
#endif
  
  pthread_mutex_unlock(&telio_lock);
}

/* ---------------------------------------------------------------- */
 
static int tcs_command(const char* command)
{
  int  err;
  char cmd[128],buf[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s)\n",PREFUN,command);
#endif
 
  err = telio_open(TELIO_TIMEOUT); if (err) return err;
  err = tcs_lock(TELIO_TIMEOUT); 
  if (err) { telio_close(); return err; }

  if (telio_online) {
    sprintf(cmd,"%s\n",command);        /* append <LF> */
    err = TCPIP_Request(telio_sock,cmd,buf,sizeof(buf));
  } else msleep(20);
  tcs_unlock();
  telio_close();

  if (!err && telio_online) {
    if      (atoi(buf) == 1) err = E_telio_tcsbusy;
    else if (atoi(buf) <  0) err = E_telio_tcserror;
  }
 
  return err;
}
 
/* ---------------------------------------------------------------- */
 
static int tcs_request(const char* command,char* answer)
{
  int  err;
  char cmd[128],buf[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%p)\n",PREFUN,command,answer);
#endif
 
  err = telio_open(TELIO_TIMEOUT); if (err) return err;
  err = tcs_lock(TELIO_TIMEOUT); 
  if (err) { telio_close(); return err; }

  if (telio_online) {
    sprintf(cmd,"%s\n",command);         /* append <LF> */
    err = TCPIP_Request(telio_sock,cmd,buf,sizeof(buf));
  } else msleep(20);
  tcs_unlock();
  telio_close();
 
  if (!err && telio_online) {
    while (strlen(buf) > 0) {            /* strip white-space */
      if (!isspace(buf[strlen(buf)-1])) break;
      buf[strlen(buf)-1] = '\0';
    }
    if (answer) strcpy(answer,buf);
  }
 
  return err;
}
 
/* ---------------------------------------------------------------- */
/* GLOBAL */
/* ---------------------------------------------------------------- */

int telio_init(int telescope,int port)
{
  int    err=0;
  double a,d,e=0.0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d)\n",PREFUN,telescope,port);
#endif

  pthread_mutex_init(&telio_lock,NULL);

  telio_online = (port) ? 1 : 0;
  telio_port = (u_short)port;
  telio_id = telescope;

  switch(telescope) {
  case TELIO_NONE:
    strcpy(telio_host,"localhost"); 
    break;
  case TELIO_MAG1:
    strcpy(telio_host,"mag1tcs.lco.cl");
    break;
  case TELIO_MAG2:
    strcpy(telio_host,"mag2tcs.lco.cl");
    break;
  default:
    telio_online = 0; 
    strcpy(telio_host,"");
    return -1;
  }
#if (DEBUG > 0)
  fprintf(stderr,"%s(): _host=%s:%d\n",PREFUN,telio_host,telio_port);
#endif
  if (telio_online) {
    err = telio_pos(&a,&d,&e);         /* check position */
    if (err) {
#if (DEBUG > 0)
      fprintf(stderr,"%s(): cannot get telescope position\n",PREFUN);
#endif
    }
  }

  return err;
}

/* ---------------------------------------------------------------- */

int telio_open(int timeout)            /* seconds */
{
#if (DEBUG > 2)
  fprintf(stderr,"%s(%d): count=%d\n",PREFUN,timeout,telio_count);
#endif

  int err = tcs_lock(timeout); if (err) return err;

  if (telio_count == 0) {
    assert(telio_sock < 0);
    if (telio_online) { 
      telio_sock = TCPIP_CreateClientSocket(telio_host,telio_port,&err);
      if (telio_sock < 0) err = E_telio_open;
    } else msleep(50);
  }
  if (!err) telio_count++;

  tcs_unlock();

  return err;
}

/* ---------------------------------------------------------------- */

void telio_close(void)
{
  int  err;

  err = tcs_lock(2*TELIO_TIMEOUT);

  if (telio_count > 0) telio_count--;
  if (telio_count == 0) {
    if (telio_online) {
      if (telio_sock > 0) { (void)close(telio_sock); telio_sock = -1; }
    }
  }
#if (DEBUG > 2)
  fprintf(stderr,"%s(): count=%d\n",PREFUN,telio_count);
#endif
  if (!err) tcs_unlock();
}

/* ---------------------------------------------------------------- */

int telio_command(const char* cmd)
{
  int err = telio_open(TELIO_TIMEOUT); if (err) return err;

  err = tcs_command(cmd);
  telio_close();

  return err;
}

/* ---------------------------------------------------------------- */

int telio_pos(double* alpha,double* delta,double* equinox)
{
  int    err;
  double h,m,s,al=*alpha,de=*delta,eq=*equinox;
  char   buf[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p,%p)\n",PREFUN,alpha,delta,equinox);
#endif

  err = telio_open(TELIO_TIMEOUT); if (err) return err;
 
  if (!err) err = tcs_request("ra",buf);    /* get R.A. */
  if (!err && telio_online) {
    sscanf(buf,"%lf:%lf:%lf",&h,&m,&s);
    al = h + m/60.0 + s/3600.0;
  }
  if (!err) err = tcs_request("dec",buf);   /* get DEC */
  if (!err && telio_online) {
    sscanf(buf,"%lf:%lf:%lf",&h,&m,&s);
    de = fabs(h) + fabs(m)/60.0 + fabs(s)/3600.0;
    if (strchr(buf,'-')) de = -de;
  }
  if (!err) err = tcs_request("epoch",buf); /* get equinox */
  if (!err && telio_online) {
    eq = atof(buf);
  }

  telio_close(); 

  if (!err && telio_online) {
    if (*equinox > 0.0) {              /* use supplied equinox */
      precess(al,de,eq,alpha,delta,*equinox);
    } else {                           /* use TCS equinox */
      *alpha = al; *delta = de; *equinox = eq;
    }
  }
 
  return err;
}

/* ---------------------------------------------------------------- */

int telio_slew(double alpha,double delta,double equinox)
{
  int  err;
  char cmd[128],buf[128],*c;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%f,%f,%f)\n",PREFUN,alpha,delta,equinox);
#endif
 
  err = telio_open(TELIO_TIMEOUT);  if (err) return err;

  if (!err) {                          /* preset R.A. */
    sprintf(cmd,"ra %s",alpha_str(buf,alpha));
    c = cmd; while (*c) { if (*c == ':') *c = ' '; c++; }
    err = tcs_command(cmd);
  }
  if (!err) {                          /* preset DEC */
    sprintf(cmd,"dc %s",delta_str(buf,delta));
    c = cmd; while (*c) { if (*c == ':') *c = ' '; c++; }
    err = tcs_command(cmd);
  }
  if (!err) {                          /* preset equinox */
    sprintf(cmd,"mp %.3f",equinox);
    err = tcs_command(cmd);
  }
#if 0 // don't slew
  if (telio_id == TELIO_NONE) {
    if (!err) err = tcs_command("slew");
  }
#endif
  telio_close();
 
  return err;
}

/* ---------------------------------------------------------------- */

int telio_aeg(float az,float el)
{
  char cmd[128];
#if (DEBUG > 0) && !defined(ENG_MODE)
  fprintf(stderr,"%s(%.2f,%.2f)\n",PREFUN,az,el);
#endif
 
  sprintf(cmd,"aeg %.2f %.2f",az,el);
  int err = tcs_command(cmd);

  return err;
}

/* ---------------------------------------------------------------- */

int telio_gpaer(int mode,float az,float el)
{
  char cmd[128];
#if (DEBUG > 0) && !defined(ENG_MODE) 
  fprintf(stderr,"%s(%d,%.2f,%.2f)\n",PREFUN,mode,az,el);
#endif
 
  sprintf(cmd,"gpaer%d %.2f %.2f",mode,az,el);
  int err = tcs_command(cmd);

  return err;
}

/* ---------------------------------------------------------------- */

int telio_move(float ofra, float ofdc)
{
  int  err;
  char cmd[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%.1f,%.1f)\n",PREFUN,ofra,ofdc);
#endif
 
  err = telio_open(TELIO_TIMEOUT); if (err) return err;
 
  if (!err) {                          /* offset R.A. */
    sprintf(cmd,"ofra %.2f",ofra);
    err = tcs_command(cmd);
  }
  if (!err) {                          /* offset DEC. */
    sprintf(cmd,"ofdc %.2f",ofdc);
    err = tcs_command(cmd);
  }
#if 0 /* default anyway */
  if (!err) {                          /* '-1' := apparent */
    sprintf(cmd,"ofep %.3f",get_epoch(0));
    err = tcs_command(cmd);
  }
#endif
 
  if (!err) err = tcs_command("offp"); /* initiate offset move */
  telio_close();
 
  return err;
}

/* ---------------------------------------------------------------- */

int telio_mstat(int timeout,int coord)  /* 2014-07-16 */
{
  int  err=0;
  char cmd[32],buf[128]="0";
 
  err = telio_open(TELIO_TIMEOUT); if (err) return err;

  if (coord) sprintf(cmd,"gdrmountw 3");
  else       sprintf(cmd,"mountw 3");

  sleep(1);
  while (timeout >= 0) {                /* wait for telescope/guider */
    sleep(1);
    err = tcs_request(cmd,buf); if (err) break;
    if (atoi(buf) == 0) break;
    timeout -= 3; err = E_telio_timeout;
  }
  telio_close();
 
  return err;
}

/* ---------------------------------------------------------------- */

int telio_setfocus(float focus)
{
  int  err;
  char cmd[128];

  sprintf(cmd,"zset %d",(int)focus);
  err = tcs_command(cmd);

  return(err);
}

/* ---------------------------------------------------------------- */

int telio_getfocus(float* focus)
{
  int  err;
  char buf[128];

  err = tcs_request("telfocus",buf);
  if (!err && telio_online) *focus = (float)atof(buf);

  return err;
}

/* ---------------------------------------------------------------- */
 
int telio_vstat(int timeout)
{
  int  err=0;
 
  while (timeout >= 0) {               /* wait for focus vane */
    sleep(1);
    err = tcs_command("vanew 3");
#if (DEBUG > 1)
    fprintf(stderr,"%s(): err=%d\n",PREFUN,err);
#endif
    if (err != E_telio_tcsbusy) break;
    timeout -= 3;
  }
  if (err == E_telio_tcsbusy) err = E_telio_timeout;
 
  return err;
}

/* ---------------------------------------------------------------- */

int telio_geo(float* am,float* ra,float* re,float* az,float* el,float* pa)
{
  int  err;
  char buf[128];

  err = telio_open(TELIO_TIMEOUT); if (err) return err;

  if (!err && am) {                    /* request airmass */
    err = tcs_request("airmass",buf);
    if (!err && telio_online) *am = (float)atof(buf);
  }
  if (!err && ra) {                    /* north vs. instrument */
    err = tcs_request("rotangle",buf);
    if (!err && telio_online) *ra = (float)atof(buf);
  }
  if (!err && re) {                    /* rotator vs. elev. disc */
    err = tcs_request("rotatore",buf);
    if (!err && telio_online) *re = (float)atof(buf);
  }
  if (!err && az) {                    /* telescope azimuth */
    err = tcs_request("telaz",buf);
    if (!err && telio_online) *az = (float)atof(buf);
  }
  if (!err && el) {                    /* telescope elevation */
    err = tcs_request("telel",buf);
    if (!err && telio_online) *el = (float)atof(buf);
  }
  if (!err && pa) {                    /* paralactic angle */
    err = tcs_request("telpa",buf);
    if (!err && telio_online) *pa = (float)atof(buf);
  }
  telio_close();

  return err;
}

/* --- */

int telio_char(const char* cmd,char* res)
{
  int err = telio_open(TELIO_TIMEOUT);
  if (!err) {
    err = tcs_request(cmd,res);
    telio_close();
  }
  return err;
}

/* ---------------------------------------------------------------- */

int telio_temps(float* td,float* tc,float* to)
{
  int  err;
  char buf[128];
 
  err = telio_open(TELIO_TIMEOUT); if (err) return err;
 
  if (!err && td) {                    /* dome */
    err = tcs_request("tambient",buf);
    if (!err && telio_online) *td = (float)atof(buf);
  }
  if (!err && tc) {                    /* primary cell */
    err = tcs_request("tcell",buf);
    if (!err && telio_online) *tc = (float)atof(buf);
  }
  /* IDEA: "ttruss", "tseccel" */
  if (!err && to) {                    /* outside */
    err = tcs_request("wxtemp",buf);
    if (!err && telio_online) *to = (float)atof(buf);
  }
  telio_close();

  return err;
}

/* ---------------------------------------------------------------- */

int telio_time(int *ut)
{
  int  err;
  char buf[128];

  err = tcs_request("ut",buf);
  if (!err && telio_online) { int hr,mi,sc;
    if (sscanf(buf,"%d:%d:%d",&hr,&mi,&sc) < 3) err = E_telio_time;
    else *ut = 3600*hr + 60*mi + sc;
  }

  return err;
}

/* ---------------------------------------------------------------- */

int telio_guider_offset(int probe,double offset)
{
  int  err;
  char cmd[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d,%.3f)\n",PREFUN,probe,offset);
#endif

  sprintf(cmd,"cnca %d %8.3f",probe,offset);
  err = tcs_command(cmd);

  return err;
}

/* ---------------------------------------------------------------- */

int telio_guider_guiding(int probe1,int probe2)
{
  int  err;
  char cmd[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d,%d)\n",PREFUN,probe1,probe2);
#endif

  err = telio_open(TELIO_TIMEOUT); if (err) return err;

  if (!err) {
    if (probe1) sprintf(cmd,"gafthr");   /* probe-1 (PR) */
    else        sprintf(cmd,"gafone");
    err = tcs_command(cmd);
  }
  if (!err) { 
    if (probe2) sprintf(cmd,"gbfthr");   /* probe-2 (SH) */
    else        sprintf(cmd,"gbfone");
    err = tcs_command(cmd);
  }
  telio_close();

  return err;
}

/* ---------------------------------------------------------------- */

#if 0 /* 2015-04-27 */
int telio_guider_box(float dx,float dy)    /* PR guider */
{
  int  err;
  char cmd[128];

  sprintf(cmd,"gprdr1 %.2f %.2f",dx,dy);   /* WARNING: moves probe */
  err = tcs_command(cmd);

  return err;
}
#endif

/* ---------------------------------------------------------------- */

int telio_guider_probe(float dx,float dy,int p1,int p2) /* 2015-04-27 */
{
  char cmd[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%.1f,%.1f,%d,%d)\n",PREFUN,dx,dy,p1,p2);
#endif

  int err = telio_open(TELIO_TIMEOUT); if (err) return err;

  if (!err && p1) {
    sprintf(cmd,"gprdr1 %.2f %.2f",dx,dy);
    err = tcs_command(cmd);
  }
  if (!err && p2) {
    sprintf(cmd,"gprdr2 %.2f %.2f",dx,dy);
    err = tcs_command(cmd);
  }
  telio_close();

  return err;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

