/* ----------------------------------------------------------------
 *
 * eds.h
 *
 * Project: Magellan EDS library (OCIW, Pasadena, CA)
 *
 * Christoph C. Birk, birk@obs.carnegiescience.edu
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_EDS_H
#define INCLUDE_EDS_H 

/* ---------------------------------------------------------------- */ 

enum eds_error_codes {
  E_eds=3000,
  E_eds_lock,
  E_eds_open,
  E_eds_last
};

#ifdef IS_EDS_C
const char* eds_errstr[] = {
  "EDS:: generic error",
  "EDS: locking EDS connection timed out",
  "EDS: cannot open socket to TCSIS",
  "EDS: last errror"
};
#else
extern const char* eds_errstr[];
#endif

/* function prototype(s) ----------------------------------------- */

int   eds_init     (const char*,int);
int   eds_open     (int);
void  eds_close    (void);

int   eds_send801  (int,float,int,float,float,float);
int   eds_send82i  (int,int,float,float);

/* --------------------------------------------------------------- */

#endif  /* INCLUDE_EDS_H */

/* --------------------------------------------------------------- */
/* --------------------------------------------------------------- */
/* --------------------------------------------------------------- */

