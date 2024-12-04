/* ---------------------------------------------------------------- */
// 
// guider.h
//
/* ---------------------------------------------------------------- */

#ifndef INCLUDE_GUIDER_H
#define INCLUDE_GUIDER_H

/* ---------------------------------------------------------------- */

#include "qltool.h"
#include "graph.h"
#include "fits.h"

/* ---------------------------------------------------------------- */

void* run_guider(void* param);

enum guider_modes_enum { 
  GMODE_NONE,GMODE_PR,GMODE_SH,GMODE_SV,GMODE_SV4 // todo replace GM_
};

/* ---------------------------------------------------------------- */

typedef struct guider_tag {
  int           gnum,gmode;            /* v0313 */
  char          gmpar;                 /* v0354 */
  int           fmode;                 /* v0329 */
  char          name[32];
  FITSpars      status;
  volatile Bool loop_running,house_running;
  volatile Bool stop_flag;
  volatile int  write_flag,send_flag,init_flag;
  pthread_t     gid;
  int           pamode,esmode,msmode;
  float         stored_tf1,stored_tf3; /* v0329 */
  int           stored_send,stored_av,stored_mode;
  char          lastCommand[256];
  char          command_msg[128];      /* v0333 */
  int           slitW;                 /* NEW v0408 */
  /* GUI */
  Window      win;
  QlTool      *qltool;
  GraphWindow *g_az,*g_el,*g_tc,*g_fw;
  EditWindow  tfbox;                   /* exposure time */
  EditWindow  dtbox;                   /* exposure->send countdown v0343 */
  EditWindow  fpbox,fdbox,fgbox;       /* frames per second */
  EditWindow  gdbox,tcbox,mxbox,bkbox,fwbox,dxbox,dybox;
  EditWindow  snbox,pxbox,bxbox,pabox,avbox;
  EditWindow  gmbox,gabox,fmbox,mmbox;
  EditWindow  csbox;                   /* cursor step */
  EditWindow  sqbox;                   /* sequence -> sent frame v0343 */
  EditWindow  tmbox,cpbox;             /* temperature,cooler */
  EditWindow  cmbox;                   /* command line */
  EditWindow  *msbox;                  /* message window(s) */
  Led         *led;
  EditWindow  smbox,gnbox;             /* v0322 */
  /* ZWO */
  ZwoStruct   *server;
  int         offx,offy;
  /* Guiding */
  int         shmode,q_flag;
  u_int       sendNumber;
  float       px;                      /* eff. pixscale (incl. binning) */
  double      angle,elsign,rosign,parity;
  double      pa,azerp,elerp,sens,azg,elg;
  pthread_mutex_t mutex;
  volatile int update_flag;
  volatile double fps,flux,ppix,back,fwhm,dx,dy;
  char        send_host[128];
  int         send_port;
} Guider;

/* ---------------------------------------------------------------- */

#endif /* INCLUDE_GUIDER_H */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

