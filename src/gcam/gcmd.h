/* ----------------------------------------------------------------
 *
 * gcmd.h -- poll tcsis for the TCS's command echoes
 *
 * One of two copies of the gcmd client: zwo src/gcam/gcmd.c and imacs
 * src/gcmd.c, the protocol in imacs docs/plans/guider-architecture.md,
 * Appendix A.  Keep them the same: a change to the loop, the answer's
 * parsing or the back-off in one is made in the other, in pull
 * requests of the two repositories opened together.
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_GCMD_H
#define INCLUDE_GCMD_H

/* DEFINEs -------------------------------------------------------- */

#define GCMD_TICK_MS    250            /* between requests while nothing is new */
#define GCMD_NORING_MS  5000           /* between requests while tcsis has no ring */
                                       /* or no gcmd (an older tcsis) */
#define GCMD_ANSWER_MS  3000           /* no answer by then: tcsis is lost */
#define GCMD_CONNECT_MS 2000           /* connect() timeout */
#define GCMD_BACKOFF_MIN 1             /* s: reconnect back-off, doubling, */
#define GCMD_BACKOFF_MAX 30            /* back to the minimum once tcsis answers */
#define GCMD_TEXT_MAX   64             /* on_cmd's text is shorter (tcsis: 22 at */
                                       /* most); a longer echo is passed over */

/* TYPEDEFs ------------------------------------------------------- */

typedef struct {
  const char* host;                    /* tcsis host; NULL or "" = no poll */
  int         port;                    /* tcsis port (5800, 5801, 5810-5815) */
  int         rotator;                 /* own rotator 0-5: whose echoes to ask for */
  void (*on_roi)(int old,int now);     /* rotator of interest changed, -1 = unknown */
  void (*on_cmd)(int sender,const char* text);  /* an echo for 'rotator' */
  void (*on_log)(int level,const char* text);   /* 0 news, 1 trouble */
} Gcmd;

/* function prototype(s) ------------------------------------------ */

int       gcmd_start (const Gcmd*);    /* 0 = polling, or no host; else an errno */
void      gcmd_stop  (void);           /* end the poll: after it the thread calls */
                                       /* no hook (one already running finishes) */
int       gcmd_telroi(void);           /* last answered rotator of interest, -1 = unknown */
int       gcmd_active(void);           /* no poll, or gcmd_telroi() == rotator */
int       gcmd_line  (const char* answer);  /* one answer, no socket (benches): */
                                       /* 1 an echo, 0 nothing newer, -1 none */
int       gcmd_hello (const char* answer);  /* the bare gcmd's answer (benches): */
                                       /* 0 it knows gcmd, -1 not */
long long gcmd_after (void);           /* the cursor: the next request's <after> */

#endif /* INCLUDE_GCMD_H */

/* ---------------------------------------------------------------- */
