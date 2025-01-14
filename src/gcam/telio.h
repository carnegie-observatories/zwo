/* ----------------------------------------------------------------
 *
 * telio.h
 *
 * Project: Magellan I/O library (OCIW, Pasadena, CA)
 *
 * Christoph C. Birk, birk@obs.carnegiescience.edu
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_TELIO_H
#define INCLUDE_TELIO_H 

/* ---------------------------------------------------------------- */ 

#ifdef IS_TELIO_C
#define EXT_TELIO 
#else
#define EXT_TELIO extern
#endif

/* DEFINEs ------------------------------------------------------- */ 


/* TYPEDEFs ------------------------------------------------------ */


/* GLOBALs ------------------------------------------------------- */ 

#if defined(IS_TELIO_C)
char *telio_name[] = {            /* names of available telescopes */
            "Simulator",
            "Baade_Mag_1",
            "Clay_Mag_2",
            ""
};
#else
extern char* telio_name[];
#endif

enum telio_telescopes {
  TELIO_NONE,
  TELIO_MAG1,
  TELIO_MAG2,
  TELIO_LAST
};

#define TELIO_BAADE TELIO_MAG1
#define TELIO_CLAY  TELIO_MAG2

#ifdef IS_TELIO_C
const double telio_lat[] = {  +34.1,        /* SBS */
                              -29.01423,    /* Magellan1/Baade */
                              -29.01423,    /* Magellan2/Clay */
                              0.0 
};
const double telio_lon[] = { -118.1,        /* SBS */
                              -70.69242,    /* Magellan */
                              -70.69242,
                              0.0
};
const double telio_alt[] = {  300.0,        /* SBS */
                             2400.0,        /* Magellan */
                             2400.0,
                             0.0
};
const double telio_flen[] = {   1.0,        /* none */
                               71.5,        /* Magellan f/11 */
                               71.5,
                                1.0
};
const char *telio_site[] = { "SBS",
                             "LCO",
                             "LCO",
                             "None"
};
const int telio_zone[] = { 8,4,4,0 };
#else
extern const double telio_lat[],telio_lon[],telio_alt[],telio_flen[];
extern const char*  telio_site[];
extern const int    telio_zone[];
#endif

EXT_TELIO  int  telio_id;
EXT_TELIO  int  telio_online;
EXT_TELIO  char telio_host[128];

enum telio_error_codes {
  E_telio=2000,
  E_telio_lock,
  E_telio_open,
  E_telio_tcsbusy,
  E_telio_tcserror,
  E_telio_timeout,
  E_telio_time,
  E_telio_last
};

#ifdef IS_TELIO_C
const char* telio_errstr[] = {
  "telio: generic error",
  "telio: locking TCS connection timed out",
  "telio: cannot open socket to TCSIS",
  "telio: TCS busy",
  "telio: TCS error",
  "telio: command timed out",
  "telio: cannot get TCS time",
  "telio: last errror"
};
#else
extern const char* telio_errstr[];
#endif

/* function prototype(s) ----------------------------------------- */

int   telio_init     (int,int);
int   telio_open     (int);
void  telio_close    (void);
int   telio_command  (const char*);

int   telio_pos      (double*,double*,double*);
int   telio_slew     (double,double,double);
int   telio_aeg      (float,float);
int   telio_gpaer    (int,float,float);
int   telio_move     (float,float);
int   telio_mstat    (int,int);
int   telio_setfocus (float);
int   telio_getfocus (float*);
int   telio_vstat    (int);
int   telio_geo      (float*,float*,float*,float*,float*,float*);
int   telio_char     (const char*,char*);
int   telio_temps    (float*,float*,float*);
int   telio_time     (int*);

int   telio_guider_offset  (int guider,double offset);
int   telio_guider_guiding (int,int);
int   telio_guider_box     (float,float);
int   telio_guider_probe   (float,float,int,int);


/* --------------------------------------------------------------- */

#endif  /* INCLUDE_TELIO_H */

/* --------------------------------------------------------------- */
/* --------------------------------------------------------------- */
/* --------------------------------------------------------------- */

