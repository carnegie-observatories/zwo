/* -----------------------------------------------------------------
 *
 * fits.c
 * 
 * Project: IMACS (OCIW, Pasadena, CA) 
 *
 * FITS functions ...  
 *
 * 1999-07-01  Christoph C. Birk, birk@obs.carnegiescience.edu (CCB) 
 * 2001-05-22  MT-safe
 * 2002-03-19  subraster
 *     -07-25  temp_ccd[]
 * 2003-05-21  temp_str
 * 2006-03-21  etalon
 *     -05-09  data section
 * 2007-05-03  GISMO
 * 2008-03-31  v2.00
 * 2009-06-25  add hour angle
 * 2010-05-17  scan mode
 * 2011-08-12  Mosaic/3
 * 2019-07-23  CASCA -- rewrite OO style
 *
 * ---------------------------------------------------------------- */

/* DEFINEs -------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           1
#endif

#define FITSRECORD      80
#define FITSBLOCK       (36*FITSRECORD)
#define FITSLINES       36

#define KEY_LEN         8              /* length if keyword */
#define RIGHT_MAR       30             /* right margin of value */
#define SLASH_POS       39             /* start of comment field */

#define MIN_SHORT       (-32768)

#define PREFUN          __func__

/* INCLUDES ------------------------------------------------------- */

#define _REENTRANT                     /* gmtime_r() */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>

#include "fits.h"

/* GLOBALs -------------------------------------------------------- */

/* none */

/* EXTERNs -------------------------------------------------------- */

#ifdef LINUX
extern void  swab(const void*,void*,size_t);
#endif

static char* my_extend(char *s, char c, int l)
{
  int i;

  i = strlen(s);
  while (i < l) {
    s[i++] = c;
  }
  s[i] = '\0';                         /* s[l] = '\0';   */

  return(s);
}

/* --- */

static int my_bigendian(void)
{
  union {
    long l;
    char c[sizeof(long)];
  } u;
  u.l = 1;
  return(u.c[sizeof(long)-1] == 1);
}

/* --- */

static double my_round(double x,int d)
{
  int i;

  for (i=0; i<d; i++) x *= 10.0;
  x = floor(x+0.5);
  for (i=0; i<d; i++) x /= 10.0;

  return x;
}

/* ---------------------------------------------------------------- */

static void fits_writeHeader(FITS* self,const char* line)
{
  assert(self->fp);
  fwrite(line,sizeof(char),FITSRECORD,self->fp);
  self->nlines += 1;
}

/* ---------------------------------------------------------------- */

void fits_number(FITS* self,const char* key,const char* num,const char* com)
{
  char line[128];

  strcpy(line,key);
  my_extend(line,' ',KEY_LEN);
  strcat(line,"= ");                   // numbers are right justified
  my_extend(line,' ',RIGHT_MAR-(int)strlen(num));
  strcat(line,num);
  if (com) {
    my_extend(line,' ',SLASH_POS);
    strcat(line,"/ ");
    strcat(line,com);
  }
  assert(sizeof(line) > FITSRECORD);
  my_extend(line,' ',FITSRECORD);
  fits_writeHeader(self,line);
}

/* --- */

void fits_int(FITS* self,const char* key,int value,const char* com)
{
  char buf[32];

  sprintf(buf,"%d",value);
  fits_number(self,key,buf,com);
}

/* --- */

void fits_float(FITS* self,const char* key,double val,int dp,const char* com)
{
  char buf[32];

  assert(fabs(val) < 1.0e19);
  sprintf(buf,"%.*lf",dp,my_round(val,dp));
  fits_number(self,key,buf,com);
}

/* --- */

void fits_exp(FITS *self,const char* key,double val,int dp,const char* com)
{
  char buf[32];

  sprintf(buf,"%.*E",dp,val);
  fits_number(self,key,buf,com);
}

/* ---------------------------------------------------------------- */

void fits_char(FITS* self,const char* key,const char* val,const char* com)
{
  char line[128],buf[128];

  strcpy(line,key);
  my_extend(line,' ',KEY_LEN);
  strcat(line,"= ");
  sprintf(buf,"'%s'",val);
  strcat(line,buf);
  if (com) {
    my_extend(line,' ',SLASH_POS);
    strcat(line,"/ ");
    strcat(line,com);
  }
  assert(sizeof(line) > FITSRECORD);
  my_extend(line,' ',FITSRECORD);
  fits_writeHeader(self,line);
}

/* ---------------------------------------------------------------- */

FITS* fits_create(int dimx,int dimy,int bitpix)
{
  FITS* self = calloc(1,sizeof(FITS));
  self->data_w = dimx;
  self->data_h = dimy;
  self->data_z = 1;
  self->bitpix = bitpix;
  self->naxis = 2;
  self->bzero = (bitpix==16) ? -MIN_SHORT : 0;
  self->little = (my_bigendian()) ? 0 : 1;
  if (self->little) self->swabuf = (short*)malloc(dimx*dimy*sizeof(short));

  return self;
}

/* ---------------------------------------------------------------- */ 

void fits_free(FITS* self)
{
  if (self->swabuf) free((void*)self->swabuf);
  free((void*)self);
}

/* ---------------------------------------------------------------- */ 

int fits_open(FITS* self,const char* file)
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%s)\n",PREFUN,self,file);
#endif

  self->fp = fopen(file,"w"); 
  if (!self->fp) return -1;

  self->nbytes = self->nlines = 0;
  fits_number(self,"SIMPLE","T",NULL);
  fits_int   (self,"BITPIX",self->bitpix,NULL);
  fits_int   (self,"NAXIS",self->naxis,NULL);
  fits_int   (self,"NAXIS1",self->data_w,NULL);
  fits_int   (self,"NAXIS2",self->data_h,NULL);
  if (self->naxis == 3) fits_int(self,"NAXIS3",self->data_z,NULL);
  fits_float (self,"BSCALE",1.0,6,NULL);
  fits_float (self,"BZERO",self->bzero,6,NULL);
  fits_char  (self,"BUNIT","DU/PIXEL",NULL);

  return 0;
}

/* ---------------------------------------------------------------- */ 

void fits_endHeader(FITS* self)
{
  char buf[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,self);
#endif

#if 1 // work with IRAF
  strcpy(buf,"END"); my_extend(buf,' ',FITSRECORD);
  fits_writeHeader(self,buf);
  memset((void*)buf,' ',FITSRECORD);
  while (self->nlines % FITSLINES) fits_writeHeader(self,buf);
#else // standard ?
  strcpy(buf,"COMMENT"); my_extend(buf,' ',FITSRECORD);
  while ((self->nlines % FITSLINES) != FITSLINES-1) fits_writeHeader(self,buf);
  strcpy(buf,"END"); my_extend(buf,' ',FITSRECORD);
  fits_writeHeader(self,buf);
  assert((self->nlines % FITSLINES) == 0);
#endif

  assert(((self->nlines * FITSRECORD) % FITSBLOCK) == 0);
}

/* ---------------------------------------------------------------- */ 

void fits_close(FITS *self)
{
  char buf[FITSBLOCK];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",__func__,self);
#endif

  size_t size = self->data_w*self->data_h*self->data_z*abs(self->bitpix)/8;
  if (self->nbytes != size) {
    fprintf(stderr,"%s: inconsistent data size ",__func__);
  }

  u_int tail = (FITSBLOCK - (self->nbytes % FITSBLOCK)) % FITSBLOCK;
  if (tail) {
    memset((void*)buf,0,tail);
    fwrite((void*)buf,sizeof(char),tail,self->fp);
  }
  fclose(self->fp); self->fp = NULL;
}

/* ---------------------------------------------------------------- */ 

void fits_writeLine(FITS* self,void* buf)
{
#if (DEBUG > 2)
  fprintf(stderr,"%s(%p,%p)\n",PREFUN,self,buf);
#endif
  assert(self->fp);

  switch (self->bitpix) {
  case 16:
    if (self->little) {                 // little endian
      assert(self->swabuf);
      swab((char*)buf,(char*)self->swabuf,self->data_w*sizeof(short));
      fwrite((void*)self->swabuf,sizeof(short),self->data_w,self->fp);
    } else {                            // big endian
      fwrite((void*)buf,sizeof(short),self->data_w,self->fp);
    }
    self->nbytes += self->data_w*sizeof(short);
    break;
  case 8:
    fwrite((void*)buf,sizeof(char),self->data_w,self->fp);
    self->nbytes += self->data_w*sizeof(char);
    break;
  default:
    assert(0);
    break;
  }
}

/* --- */

void fits_writeData16(FITS* self,u_short* data)
{
  int     i,j;
  u_short *udata;
  short   *sdata,*shortbuf;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d,%d)\n",__func__,self->data_w,self->data_h);
#endif
#if (DEBUG > 2)
  u_short min=0xffff,max=0;
#endif

  int bzero = (int)self->bzero;
  int data_width = self->data_w;
  int data_height = self->data_h;

  shortbuf = (short*)malloc(data_width*sizeof(short));

  for (i=0; i<data_height; i++) {
    udata = data + i*data_width;
    sdata = shortbuf;
    for (j=0; j<data_width; j++) {
     *sdata = (short)((int)*udata - bzero);
#if (DEBUG > 1)
     if (*udata < min) min = *udata;
     if (*udata > max) max = *udata;
#endif
     udata++; sdata++;
    }
    fits_writeLine(self,shortbuf);
  }
#if (DEBUG > 1)
  printf("min=%u, max=%u\n",min,max);
#endif
  free((void*)shortbuf);
}

/* ---------------------------------------------------------------- */ 

void fits_writeData(FITS* self,void* data)
{
  switch (self->bitpix) {
  case 8:
    fwrite(data,sizeof(char)*self->data_w,self->data_h,self->fp);
    self->nbytes += self->data_w*self->data_h*sizeof(char);
    break;
  case 16:
    fits_writeData16(self,(u_short*)data);
    break;
  default:
    assert(0);
    break;
  }
}

/* ---------------------------------------------------------------- */ 
/* ---------------------------------------------------------------- */ 
/* ---------------------------------------------------------------- */ 

