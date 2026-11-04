/* ----------------------------------------------------------------
 *
 * gcamtail.h -- gcamzwo's reactions to the TCS's command echoes, the
 *               camera half of magsoft's gcam2zwo.py, on tcsis's gcmd
 *
 * docs/plans/gcam-gcmd-poll.md
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_GCAMTAIL_H
#define INCLUDE_GCAMTAIL_H

#include <stddef.h>                    /* size_t */

/* TYPEDEFs ------------------------------------------------------- */

typedef struct {
  const char* host;                    /* tcsis host (telio's), "" = no poll */
  int         tport;                   /* tcsis port (telio's) */
  int         gnum;                    /* camera 1-3: probe 'a'+gnum-1 */
  int         port;                    /* ini 'port': own rotator, whose echoes */
  int         cnca;                    /* ini 'cnca': 'cnca <gnum> <deg>' -> 'ca <deg>' */
  int         portexit;                /* ini 'portexit': exit when the rotator leaves 'port' */
  const void* who;                     /* message() target for warnings, may be NULL */
} GcamTail;

/* function prototype(s) ------------------------------------------ */

int  gcamtail_start (const GcamTail*); /* 0 = polling, or no poll configured */
void gcamtail_stop  (void);
int  gcamtail_on    (void);            /* a poll is configured ('status' eds=) */
int  gcamtail_telroi(void);            /* answered rotator, -1 = unknown (roi=) */

/* provided by the program that links gcamtail.o: zwogcam.c in gcamzwo,
   recording stand-ins in gcamtail_test.c */

extern int  remote_command(const char* cmd,char* reply,size_t n);
extern void remote_exit   (void);      /* what the 'exit' command does */

#endif /* INCLUDE_GCAMTAIL_H */

/* ---------------------------------------------------------------- */
