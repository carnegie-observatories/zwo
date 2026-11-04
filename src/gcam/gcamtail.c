/* ----------------------------------------------------------------
 *
 * gcamtail.c -- gcamzwo's reactions to the TCS's command echoes, the
 *               camera half of magsoft's gcam2zwo.py, on tcsis's gcmd
 *
 * gcmd.c asks tcsis for the echoes the TCS took while this camera's
 * rotator (ini 'port') was the rotator of interest; this file acts on
 * them as gcam2zwo.py did:
 *
 *   g<p><cmd>          ->  <cmd>, as a command-port request
 *   slew...            ->  fone
 *   cnca <gnum> <deg>  ->  ca <deg>             (ini 'cnca 1')
 *
 * and ends the program when the rotator leaves 'port' (ini 'portexit').
 * Other echoes (the camera's own 'aeg', the other probes' commands) are
 * passed over without a word.  X-free, so that the bench
 * (gcamtail_test.c) can link it.  docs/plans/gcam-gcmd-poll.md
 *
 * ---------------------------------------------------------------- */

#include <stdio.h>                     /* snprintf() */
#include <stdlib.h>                    /* strtod() */
#include <stdarg.h>                    /* va_list */
#include <string.h>                    /* strncmp() */
#include <ctype.h>                     /* isspace() */

#include "zwogcam.h"                   /* message(),MSS_* */
#include "gcmd.h"
#include "gcamtail.h"

/* STATICs -------------------------------------------------------- */

static GcamTail  cfg;                  /* the ini settings, copied */
static char      tcsis_host[128];
static char      probe='a';            /* 'a' + gnum - 1 */
static int       configured=0;         /* a poll is configured */
static int       last_known=-1;        /* last answered rotator other than -1 */

/* ---------------------------------------------------------------- */
/* LOCAL */
/* ---------------------------------------------------------------- */

static void say(int warn,const char* fmt,...)  /* one 'eds:' log line */
{
  char    buf[512];
  va_list ap;

  strcpy(buf,"eds: ");
  va_start(ap,fmt);
  (void)vsnprintf(buf+5,sizeof(buf)-5,fmt,ap);
  va_end(ap);
  if (warn) message(cfg.who,buf,MSS_FLUSH | MSS_WARN);  /* also on screen */
  else      message(NULL,buf,MSS_FLUSH);               /* log file only */
}

/* ---------------------------------------------------------------- */

static void run(int sender,const char* cmd,const char* why)
{
  char reply[128]="";
  int  err;

  if (why) say(0,"'%s' (%s) from %d",cmd,why,sender);
  else     say(0,"'%s' from %d",cmd,sender);
  err = remote_command(cmd,reply,sizeof(reply));
  if (err) say(0,"'%s' failed: %s",cmd,(*reply) ? reply : "error");
}

/* ---------------------------------------------------------------- */

static void on_roi(int old,int now)    /* the port exit */
{
  say(0,"roi %d -> %d, gate %s",old,now,(now == cfg.port) ? "open" : "closed");
  if ((last_known == cfg.port) && (now != cfg.port) && (now != -1)) {
    if (cfg.portexit) {                /* free the ports for the */
      say(0,"port %d left for %d, exiting",cfg.port,now);  /* new rotator */
      last_known = now;
      remote_exit();
      return;
    }
    say(0,"port %d left for %d, staying (portexit 0)",cfg.port,now);
  }
  if (now != -1) last_known = now;     /* a gap keeps the last port */
}

/* ---------------------------------------------------------------- */

static void on_cmd(int sender,const char* text)  /* an echo for 'port' */
{
  const char* p;
  char        cmd[GCMD_TEXT_MAX+8],deg[64],*end;
  int         cam;

  if ((text[0] == 'g') && (text[1] == probe)) {  /* g<p><cmd> */
    for (p=text+2; isspace((unsigned char)*p); p++) ;
    if (*p) run(sender,p,NULL);
    return;
  }
  if (!strncmp(text,"slew",4)) {       /* any command starting 'slew' */
    run(sender,"fone","slew");
    return;
  }
  if (cfg.cnca && !strncmp(text,"cnca",4) && isspace((unsigned char)text[4])) {
    if ((sscanf(text+4,"%d %63s",&cam,deg) != 2) || (cam != cfg.gnum)) {
      say(0,"ignored '%s' from %d (not camera %d)",text,sender,cfg.gnum);
      return;
    }
    (void)strtod(deg,&end);
    if (*end != '\0') {
      say(0,"ignored '%s' from %d (angle)",text,sender);
      return;
    }
    snprintf(cmd,sizeof(cmd),"ca %s",deg);      /* the angle verbatim */
    run(sender,cmd,"cnca");
    return;
  }
}

/* ---------------------------------------------------------------- */

static void on_log(int level,const char* text)
{
  say(level > 0,"%s",text);
}

/* ---------------------------------------------------------------- */
/* GLOBAL */
/* ---------------------------------------------------------------- */

int gcamtail_start(const GcamTail* c)
{
  Gcmd g;
  int  err;

  cfg = *c;
  snprintf(tcsis_host,sizeof(tcsis_host),"%s",(c->host) ? c->host : "");
  cfg.host = tcsis_host;
  configured = (tcsis_host[0] != '\0');
  last_known = -1;
  if ((cfg.gnum < 1) || (cfg.gnum > 3)) {
    say(1,"camera number %d is not 1-3, no poll",cfg.gnum);
    configured = 0;
    return -1;
  }
  probe = (char)('a'+cfg.gnum-1);

  memset(&g,0,sizeof(g));
  g.host    = tcsis_host;
  g.port    = cfg.tport;
  g.rotator = cfg.port;
  g.on_roi  = on_roi;
  g.on_cmd  = on_cmd;
  g.on_log  = on_log;

  if (!configured) {                   /* hooks only: gcmd_line() */
    say(0,"no cmdpoll, no poll");
    return gcmd_start(&g);
  }
  say(0,"cmdpoll %s:%d, probe %c, port %d, cnca %d, portexit %d",
      tcsis_host,cfg.tport,probe,cfg.port,cfg.cnca,cfg.portexit);
  if ((err=gcmd_start(&g)) != 0) {
    say(1,"starting the poll failed (err=%d)",err);
    configured = 0;
  }
  return err;
}

/* ---------------------------------------------------------------- */

void gcamtail_stop(void)
{
  gcmd_stop();
}

/* ---------------------------------------------------------------- */

int gcamtail_on(void)
{
  return configured;
}

/* ---------------------------------------------------------------- */

int gcamtail_telroi(void)
{
  return gcmd_telroi();
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
