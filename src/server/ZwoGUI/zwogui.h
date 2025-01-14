/* ---------------------------------------------------------------- */
// 
// zwogui.h
//
/* ---------------------------------------------------------------- */

#ifndef INCLUDE_ZWOGUI_H
#define INCLUDE_ZWOGUI_H

/* ---------------------------------------------------------------- */

#include "qltool.h"
#include "fits.h"

/* ---------------------------------------------------------------- */

typedef struct zwogui_tag {
  char          name[32];
  FITSpars      status;
  volatile Bool loop_running;
  volatile Bool stop_flag;
  volatile int  write_flag,send_flag,init_flag;
  float         px;                    /* eff. pixscale (incl. binning) */
  char          send_host[128];
  int           send_port;
  pthread_mutex_t mutex;
  volatile double fps;
  /* GUI */
  Window      win;
  QlTool      *qltool;
  EditWindow  tfbox,dtbox;             /* exposure time */
  Menu        gdmenu;                  /* guider options menu */
  EditWindow  fpbox,fdbox;             /* frames per second */
  EditWindow  avbox;
  EditWindow  csbox;                   /* cursor step */
  EditWindow  sqbox;                   /* sequence number */
  EditWindow  tmbox,pcbox;             /* temperature */
  EditWindow  cmbox;                   /* command line */
  EditWindow  *msbox;                  /* message window(s) */
  Led         *led;
  ZwoStruct   *ccd;
} ZwoGUI;

/* ---------------------------------------------------------------- */

#endif /* INCLUDE_ZWOGUI_H */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

