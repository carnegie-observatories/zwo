/* ----------------------------------------------------------------
 *
 * utils.h
 *
 * 1999-05-06  Christoph C. Birk, birk@obs.carnegiescience.edu
 *
 * --------------------------------------------------------------- */

#ifndef INCLUDE_UTILS_H
#define INCLUDE_UTILS_H 

/* --------------------------------------------------------------- */ 

#include <math.h>
#include <sys/time.h>                  /* time_t */

#define RADS       (180.0/M_PI)        /* degrees per radian */

#ifndef __isleap
#define __isleap(y)  (((y) % 4 == 0) && (((y) % 100 != 0) || ((y) % 400 == 0)))
#endif

/* --------------------------------------------------------------- */ 

char*    extend            (char*,char,int);
char*    uppercase         (char*);
char*    lowercase         (char*);
char*    cut_spaces        (char*);
 
char*    alpha_str         (char*,double);
char*    alpha_str2        (char*,double,int);
char*    alpha_str3        (char*,double,int,char);
char*    delta_str         (char*,double);
char*    delta_str2        (char*,double,int);
char*    delta_str3        (char*,double,int,char);
char*    time_str          (char*,double);
 
double   d_alpha           (const char*);
double   d_delta           (const char*);

void     precess           (double,double,double,double*,double*,double);
double   get_sidereal      (time_t,double);
double   get_epoch         (time_t);
int      get_uts           (time_t);
char*    get_night         (char*,time_t);
double   get_airmass       (double,double,double);
double   get_ha            (time_t,double,double);
double   get_pa            (double,double,double);
double   get_zd            (double,double,double);
double   get_rt            (double,double,double);
double   get_rtrate        (double,double,double);
double   get_parate        (double,double,double);
double   get_telaz         (time_t,double,double);
double   get_telel         (time_t,double,double,double,double);
void     get_altaz (double,double,double,double,double*,double*);
void     get_radec (double,double,double,double,double*,double*);

void     adebug            (const char*,const char*);
void     tdebug            (const char*,const char*);
time_t   cor_time          (int);
double   walltime          (int);
 
char*    extract_filename  (char*,const char*);
char*    extract_rootname  (char*,const char*);
char*    extract_pathname  (char*,const char*);

void     play_sound        (const char*);

void     msleep            (int); 

void     q_sort            (int*,int,int);
void     d_sort            (double*,int,int);

char*    genv2             (const char*,char*);
char*    genv3             (const char*,const char*,char*);

int      imin              (int,int);
int      imax              (int,int);
 
char*    get_local_datestr (char*,time_t);
char*    get_ut_datestr    (char*,time_t);
char*    get_ut_timestr    (char*,time_t);
char*    get_local_timestr (char*,time_t);

double   mag2flux          (double,double);
double   flux2peak         (double,double,double);
double   peak2flux         (double,double);
double   flux2mag          (double,double);

float    checkfs           (const char*);

float    diff_tp           (struct timeval*,struct timeval*);

int      put_string        (const char*,const char*,char*);
char*    get_string        (const char*,const char*,char*,char*);
long     get_long          (const char*,const char*,long);
int      put_long          (const char*,const char*,long);
double   get_double        (const char*,const char*,double);
int      put_double        (const char*,const char*,double,int);

int      is_bigendian      (void);

char*    ini_path          (char*,const char*,const char*);
char*    set_path          (char*,const char*);
char*    log_path          (char*,const char*,const char*);
char*    bin_path          (char*,const char*,const char*);
void     lnk_logfile       (char*,const char*);
// void     mail_file         (const char*,const char*,const char*,int);

double   my_round          (double,int);
double   modulo            (double,double,double);
double   modneg            (double,double);
double   modtop            (double,double);

int      lockfile_open     (const char*);
void     lockfile_close    (int);

double   stringVal         (const char* buf,int i);

/* ---------------------------------------------------------------- */

#endif  /* INCLUDE_UTILS_H */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

