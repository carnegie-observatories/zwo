/* ----------------------------------------------------------------
 *
 * edstail.h -- a tcsif client for the Magellan EDS stream
 *
 * Shared by zwo (gcamzwo: src/gcam/edstail/) and IMACS (mechgui:
 * src/edstail/, a submodule on zwo's 'edstail' branch).  Record
 * layout, behaviour and the split branch: README.md.
 *
 * 2026-10-31  first version: the core of IMACS v3.23's gpc.c
 * 2026-11-02  edstail_active() fails closed, edstail_start() checks the regexp
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_EDSTAIL_H
#define INCLUDE_EDSTAIL_H

/* DEFINEs -------------------------------------------------------- */

#define EDSTAIL_PORT     5601          /* tcsif fan-out (TCP) */
#define EDSTAIL_LINE_MAX 256           /* one EDS record */

/* TYPEDEFs ------------------------------------------------------- */

typedef struct {
  const char* host;                    /* tcsif host; NULL or "" = tail off */
  int         port;                    /* 0 = EDSTAIL_PORT */
  const char* regexp;                  /* subscription, sent with a NUL */
  int         rotator;                 /* own rotator of interest: the gate */
  /* hooks, called in the reader thread (or in edstail_line's caller);
     each may be NULL; the texts are only valid during the call */
  void (*on_roi) (int old,int now);    /* latch changed; now=-1: stream lost */
  void (*on_cmd) (int sender,const char* text);  /* Z81x inside the gate */
  void (*on_drop)(int sender,const char* text);  /* Z81x outside the gate */
  void (*on_log) (int level,const char* text);   /* 0 news, 1 trouble */
} EdsTail;

/* function prototype(s) ------------------------------------------ */

int  edstail_start (const EdsTail*);   /* 0 = running or tail off, else errno */
void edstail_stop  (void);             /* end the reader, wake it up */
int  edstail_telroi(void);             /* latched rotator, -1 = unknown */
int  edstail_active(void);             /* no host, or telroi == rotator */
void edstail_line  (const char*);      /* one record; the benches' entry */

#endif /* INCLUDE_EDSTAIL_H */

/* ---------------------------------------------------------------- */
