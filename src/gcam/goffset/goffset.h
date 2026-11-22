/* ----------------------------------------------------------------
 *
 * goffset.h -- mouse-mode offsets through the Swope app
 *
 * At Swope the guide probes and the telescope offsets belong to the
 * Swope instrument app.  goffset sends gcamzwo's mouse-mode 2 and 3
 * offsets to the app's TCP command port as one text line each,
 * "guider_offset <n> <dx> <dy>" and "tele_offset <dx> <dy>" (arcsec
 * on the sky, +east, +north), and waits for the app's answer, which
 * comes when the move has ended: "ok", or "-E" and the reason.  The
 * commands, the keys, the hooks in gcamzwo and how to remove this
 * directory: README.md.
 *
 * Standard headers only (POSIX sockets, poll, pthreads), nothing from
 * gcamzwo, so the bench builds it alone.
 *
 * 2026-11-22  first version
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_GOFFSET_H
#define INCLUDE_GOFFSET_H

/* DEFINEs -------------------------------------------------------- */

#define GOFFSET_MSG       256          /* room for the app's answer */

enum goffset_errors {                  /* what goffset_probe/_tele return */
  GOFFSET_OK = 0,
  GOFFSET_REFUSED,                     /* the app answered -E: msg says why */
  GOFFSET_NOLINK,                      /* no connection: nothing was sent */
  GOFFSET_TIMEOUT,                     /* sent, no answer in offset_wait s */
  GOFFSET_BADREPLY,                    /* sent, an answer that is neither */
  GOFFSET_OFF                          /* offset_port is 0 */
};

/* function prototype(s) ------------------------------------------ */

int   goffset_ini    (const char* key,const char* val); /* 1 = one of our keys, taken */
int   goffset_enabled(void);           /* offset_port is set */
int   goffset_probe  (int gnum,double x,double y,char* msg,int len);
int   goffset_tele   (double x,double y,char* msg,int len);
                                       /* arcsec, the axes rotate() gives;
                                          blocks until the app answers */

#endif /* INCLUDE_GOFFSET_H */

/* ---------------------------------------------------------------- */
