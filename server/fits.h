/* ----------------------------------------------------------------
 *
 * fits.h
 *
 * Project: IMACS  (OCIW, Pasadena, CA)
 *
 * Christoph C. Birk (birk@obs.carnegiescience.edu)
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_FITS_H
#define INCLUDE_FITS_H 

/* TYPEDEFs ------------------------------------------------------- */
 
typedef struct fits_tag {
  FILE *fp;
  int   naxis,bitpix,bzero,little;
  int   data_w,data_h,data_z;
  u_int nbytes,nlines;
  short *swabuf;
} FITS;

/* function prototype(s) ------------------------------------------ */ 

FITS* fits_create    (int,int,int);
void  fits_free      (FITS*);
int   fits_open      (FITS*,const char*);
void  fits_endHeader (FITS*);
void  fits_writeData (FITS*,void*);
void  fits_close     (FITS*);

void  fits_int       (FITS*,const char*,int,const char*);
void  fits_float     (FITS*,const char*,double,int,const char*);
void  fits_exp       (FITS*,const char*,double,int,const char*);
void  fits_char      (FITS*,const char*,const char*,const char*);

/* ---------------------------------------------------------------- */

typedef struct system_parameters_tag {
  /* camera */
  char   instrument[64];
  int    dimx,dimy,startx,starty;
  float  pixscale;
  /* observing */
  double start_int,stop_int; // xxx
  u_int  seqNumber; //xxx
  int    run;
  float  exptime;
  int    binning;
  char   prefix[16];
  float  temp_ccd,cool_percent;
  char   origin[64];
  /* FITS */
  int    bzero,bitpix;
  char   datapath[512];
  char   filename[64];
  char   version[64];
} FITSpars; // todo rename and move


/* ---------------------------------------------------------------- */

#endif  /* INCLUDE_FITS_H */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

