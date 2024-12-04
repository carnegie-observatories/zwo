/* ---------------------------------------------------------------- */
//
// FITS.m
//
// Created by Christoph Birk on 2015-08-21
// Copyright (c) 2015 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#define DEBUG           0

/* ---------------------------------------------------------------- */

#import  "FITS.h"

/* ---------------------------------------------------------------- */

#define FITSRECORD      80               // FITS record length
#define FITSLINES       36               // #lines per block
#define FITSBLOCK       (FITSLINES*FITSRECORD)  // FITS block length
#define KEY_LEN         8                // length of keyword
#define RIGHT_MAR       30               // right margin of value
#define SLASH_POS       39               // comment field in FITS record

#define MIN_SHORT       (-32768)
#define MAX_SHORT       (+32767)

#define KEY_BITPIX      "BITPIX"
#define KEY_NAXIS1      "NAXIS1"
#define KEY_NAXIS2      "NAXIS2"
#define KEY_BSCALE      "BSCALE"
#define KEY_BZERO       "BZERO"

#define PREFUN          __func__

/* ---------------------------------------------------------------- */

static int my_bigendian(void)
{
  union { int l; char c[sizeof(int)]; } u;
  
  u.l = 1;
  return((u.c[sizeof(int)-1] == 1));
}

/* ---------------------------------------------------------------- */

static char* my_extend(char *s, char c, int l)
{
  size_t i = strlen(s);
  while (i < l) s[i++] = c;

  s[i] = '\0';

  return s;
}

/* ---------------------------------------------------------------- */

static double my_round(double x,int d)
{
  int i;
  
  for (i=0; i<d; i++) x *= 10.0;
  x = floor(x+0.5);
  for (i=0; i<d; i++) x /= 10.0;

  return(x);
}

/* ---------------------------------------------------------------- */

@interface FITS ()
@property (readwrite,nonatomic,copy) NSString *failedFile;
@end

/* ---------------------------------------------------------------- */

@implementation FITS

@synthesize linktarg;

/* ---------------------------------------------------------------- */
//34567890123456789012345678901234567890

- (instancetype)initWithBase:(const char*)preStr width:(u_int)w height:(u_int)h
{
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

  self = [super init]; if (!self) return nil;
  
  nbytes = nlines = 0;
  _bzero = -MIN_SHORT;

  data_width = w;
  data_height = h;
  big = my_bigendian();
  if (!big) swabuf = (short*)malloc(data_width*sizeof(short));
  shobuf = (short*)malloc(data_width*sizeof(short));
  
  base = [[NSString alloc] initWithFormat:@"%s",preStr];
  
  return self;
}

/* ---------------------------------------------------------------- */

- (void)dealloc
{
#if (DEBUG > 1)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif

  [base release];
  [linkname release];
  [linktarg release];
  [_failedFile release];
  
  if (fp) free((void*)fp);
  if (swabuf) free((void*)swabuf);
  if (shobuf) free((void*)shobuf);

  [super dealloc];
}

/* ---------------------------------------------------------------- */

- (BOOL)open:(const char*)file paths:(NSArray*)datapaths
{
  BOOL r=FALSE;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s,%d)\n",PREFUN,file,(int)datapaths.count);
#endif

  n_folders = (int)datapaths.count; if (n_folders == 0) return FALSE;
  fp = (FILE**)malloc(n_folders*sizeof(FILE*));

  for (int i=0; i<n_folders; i++) {
    NSString *path = [datapaths objectAtIndex:i];
    if ([path compare:@""] == NSOrderedSame) {
      fp[i] = NULL;
    } else {
      NSString *filepath = [NSString stringWithFormat:@"%@/%s",path,file];
      fp[i] = fopen([filepath UTF8String],"wb");
      if (fp[i]) r = TRUE;                // at least one file open
      else       self.failedFile = filepath;
      if ((i==0) && (r)) {
        linkname = [[NSString alloc] initWithFormat:@"%@/%@.fits",path,base];
        // NSLog(@"linkname=%@",linkname);
        linktarg = [filepath retain];
        // NSLog(@"linktarg=%@",linktarg);
      }
    }
  } // endfor(n_folders)
  
  if (r) {
    [self number:"T"  forKey:"SIMPLE" comment:NULL];
    [self intValue:16 forKey:KEY_BITPIX comment:NULL];
    [self intValue:2  forKey:"NAXIS" comment:NULL];
    [self intValue:data_width forKey:KEY_NAXIS1 comment:NULL];
    [self intValue:data_height forKey:KEY_NAXIS2 comment:NULL];
    [self floatValue:1.0 forKey:KEY_BSCALE prec:8 comment:"real=bzero+bscale*value"];
    [self floatValue:_bzero forKey:KEY_BZERO prec:8 comment:NULL];
    [self charValue:"DU/PIXEL" forKey:"BUNIT" comment:NULL];
  }

  return r;
}

/* ---------------------------------------------------------------- */

- (void)writeHeader:(const char*)line
{
  for (int f=0; f<n_folders; f++) {
    if (fp[f]) {
      fwrite(line,sizeof(char),FITSRECORD,fp[f]);
    }
  }
  nlines++;
}

/* ---------------------------------------------------------------- */

- (void)number:(const char*)num forKey:(const char*)key comment:(const char*)com
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
  [self writeHeader:line];
}

/* --- */

- (void)intValue:(int)value forKey:(const char*)key comment:(const char*)com
{
  char buf[32];
  
  sprintf(buf,"%d",value);
  [self number:buf forKey:key comment:com];
}

/* --- */

- (void)floatValue:(double)value forKey:(const char*)key prec:(int)dp comment:(const char*)com
{
  char buf[32];
  
  assert(fabs(value) < 1.0e19);
  sprintf(buf,"%.*lf",dp,my_round(value,dp));
  [self number:buf forKey:key comment:com];
}

/* --- */

- (void)expValue:(double)value forKey:(const char*)key prec:(int)dp comment:(const char*)com
{
  char buf[32];
  
  sprintf(buf,"%.*E",dp,value);
  [self number:buf forKey:key comment:com];
}

/* ---------------------------------------------------------------- */

- (void)charValue:(const char*)value forKey:(const char*)key comment:(const char*)com
{
  char line[128],buf[128];
  
  strcpy(line,key);
  my_extend(line,' ',KEY_LEN);
  strcat(line,"= ");
  if (value) {
    if (*value) {
      sprintf(buf,"'%s'",value);
      strcat(line,buf);
    }
  }
  if (com) {
    my_extend(line,' ',SLASH_POS);
    strcat(line,"/ ");
    strcat(line,com);
  }
  assert(sizeof(line) > FITSRECORD);
  my_extend(line,' ',FITSRECORD);
  [self writeHeader:line];
}

/* --- */

- (void)stringValue:(NSString*)value forKey:(const char*)key comment:(const char*)com
{
  if (!value) value = @"";
  [self charValue:[value UTF8String] forKey:key comment:com];
}

/* ---------------------------------------------------------------- */

- (void)endHeader
{
  char buf[128];

#if 0 // FITS standard (PFS_STA only)
  strcpy(buf,"END"); my_extend(buf,' ',FITSRECORD);
  [self writeHeader:buf];
  memset((void*)buf,' ',FITSRECORD);
  while (nlines % FITSLINES) [self writeHeader:buf];
#else // OCIW standard
  strcpy(buf,"COMMENT"); my_extend(buf,' ',FITSRECORD);
  while ((nlines % FITSLINES) != FITSLINES-1) [self writeHeader:buf];
  strcpy(buf,"END"); my_extend(buf,' ',FITSRECORD);
  [self writeHeader:buf];
  assert((nlines % FITSLINES) == 0);
#endif

  assert(((nlines * FITSRECORD) % FITSBLOCK) == 0);
}

/* ---------------------------------------------------------------- */

- (void)writeLine:(u_short*)buf
{
  if (_bzero == 0) [self writeShort:(short*)buf];
  else             [self writeLine:buf bzero:(int)_bzero];
}

/* --- */

- (void)writeLine:(u_short*)buf bzero:(int)bz
{
  assert(shobuf);

  short *sb = shobuf;
  for (int i=0; i<data_width; i++,sb++,buf++) *sb = (short)((int)*buf - bz);

  [self writeShort:shobuf];
}

/* --- */

- (void)writeShort:(short*)buf
{
  if (big) {                          // big endian
    for (int f=0; f<n_folders; f++) {
      if (fp[f]) fwrite((void*)buf,sizeof(short),data_width,fp[f]);
    }
  } else {                            // little endian
    assert(swabuf);
    swab((char*)buf,(char*)swabuf,data_width*sizeof(short));
    for (int f=0; f<n_folders; f++) {
      if (fp[f]) fwrite((void*)swabuf,sizeof(short),data_width,fp[f]);
    }
  }
  nbytes += data_width*sizeof(short);
}

/* --- */

- (void)writeData:(u_short*)data
{
  for (int i=0; i<data_height; i++) {
    [self writeLine:(data + i*data_width)];
  }
}

/* ---------------------------------------------------------------- */

- (void)close:(int)clink
{
  char buf[FITSBLOCK];
#if (DEBUG > 0)
  fprintf(stderr,"%s:%p\n",PREFUN,self);
#endif
  
  if (nbytes != data_width*data_height*sizeof(short)) {
    NSLog(@"%s: inconsistent data size ",PREFUN);
  }

  u_int tail = (FITSBLOCK - (nbytes % FITSBLOCK)) % FITSBLOCK;
  if (tail) {
    memset((void*)buf,0,tail);
    for (int f=0; f<n_folders; f++) {
      if (fp[f]) fwrite((void*)buf,sizeof(char),tail,fp[f]);
    }
  }
  for (int f=0; f<n_folders; f++) {
    if (fp[f]) {
      fclose(fp[f]); fp[f] = NULL;
      if ((f == 0) && clink) {
        unlink([linkname UTF8String]);
        symlink([linktarg UTF8String],[linkname UTF8String]);
      }
    }
  }
}

/* ---------------------------------------------------------------- */

static void swap2(void* ptr,int nitems)
{
#if 1 // paranoia about 'in-place' swap
  register int  i;
  register char c,*p=(char*)ptr;

  assert(sizeof(short) == 2);

  for (i=0; i<nitems; i++,p+=2) {
    c = p[0]; p[0] = p[1]; p[1] = c;
  }
#else
  swab((void*)ptr,(void*)ptr,nitems*sizeof(short));  // BSD: undefined behavior
#endif
}

/* --- */

static void swap4(void* ptr,int nitems)
{
  register int  i;
  register char c,*p=(char*)ptr;

  assert(sizeof(int) == 4);

  for (i=0; i<nitems; i++,p+=4) {
    c = p[0]; p[0] = p[3]; p[3] = c;
    c = p[1]; p[1] = p[2]; p[2] = c;
  }
}

/* --- */

static int isKey(const char* buf,const char* key)
{
  return !strncmp(buf,key,strlen(key));
}

/* --- */

static int read_header(FILE* fp,int* bp,int* n1,int* n2,double* bz,double* bs,int *x1,int *x2,int* y1,int* y2)
{
  int    i,endflag=0;
  size_t r=0;
  char   buf[FITSRECORD+2];

  *bp = *n1 = *n2 = *x1 = *x2 = *y1 = *y2 = 0;
  *bz = 0.0;
  *bs = 1.0;

  do {                                 /* read header */
    for (i=0; i<FITSLINES; i++) {
      r = fread((void*)buf,1,FITSRECORD,fp); if (r < FITSRECORD) break;
      buf[FITSRECORD] = '\0';          /* terminate */
      if      (isKey(buf,KEY_BITPIX))  *bp = atoi(buf+10);
      else if (isKey(buf,KEY_NAXIS1))  *n1 = atoi(buf+10);
      else if (isKey(buf,KEY_NAXIS2))  *n2 = atoi(buf+10);
      else if (isKey(buf,KEY_BSCALE))  *bs = atof(buf+10);
      else if (isKey(buf,KEY_BZERO))   *bz = atof(buf+10);
      else if (isKey(buf,KEY_TRIMSEC)) sscanf(buf+10,"'[%d:%d,%d:%d]'",x1,x2,y1,y2);
      else if (isKey(buf,KEY_DATASEC)) sscanf(buf+10,"'[%d:%d,%d:%d]'",x1,x2,y1,y2);
      else if (!strncmp(buf,"END ",4)) endflag = 1; /* end of header */
    }
    if (r < FITSRECORD) break;
  } while (!endflag);

  if ((*bp == 0) || (*n1 < 1) || (*n2 < 1)) return 0;

  return endflag;
}

/* --- */

u_short* fits_load16(const char* file,int* w,int* h,int trim)
{
  int     x,y,do_swap,bp,n1,n2,x1,x2,y1,y2;
  size_t  r=0;
  short   *sbuf0;
  int     *ibuf0;
  u_short *data0,*data;
  double  bscale,bzero;
  FILE    *fp;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,file);
#endif
  assert(sizeof(u_short) == 2);

  fp = fopen(file,"rb");  if (!fp) return NULL;

  int i = read_header(fp,&bp,&n1,&n2,&bzero,&bscale,&x1,&x2,&y1,&y2);
#if (DEBUG > 0)
  printf("bp=%d, n1=%d, n2=%d, bz=%f, bs=%f, x=%d:%d, y=%d:%d\n",
         bp,n1,n2,bzero,bscale,x1,x2,y1,y2);
#endif
  if (!i) { fclose(fp); return NULL; }

  *w = n1; *h = n2;
  if (trim) {
    if (x1 && x2 && y1 && y2) {
      *w = x2-x1+1; *h = y2-y1+1;
    } else {
      x1 = y1 = 1; x2 = n1; y2 = n2;
    }
  } else {
    x1 = y1 = 1; x2 = n1; y2 = n2;
  }
  assert((x1 > 0) && (x2 >= x1) && (x2 <= n1) && (*w == x2-x1+1));
  assert((y1 > 0) && (y2 >= y1) && (y2 <= n2) && (*h == y2-y1+1));
  
  do_swap = (my_bigendian()) ? 0 : 1;
  data = data0 = (u_short*)malloc((*w)*(*h)*sizeof(u_short));

  switch (bp) {
  case 16:
    assert(sizeof(short) == 2);
    sbuf0 = (short*)malloc(n1*sizeof(short));
    for (y=1; y<=y2; y++) {            /* loop over data rows */
      r = fread(sbuf0,sizeof(short),n1,fp);
      if (y < y1) continue;
      if (r != n1) break;
      if (do_swap) swap2((void*)sbuf0,n1);
      for (x=x1-1; x<x2; x++,data++) { /* loop over data columns */
        *data = (u_short)(bzero + bscale * (double)sbuf0[x]);
      }
    } /* endfor(y) */
    free((void*)sbuf0);
    break;
  case 32:
    assert(sizeof(int) == 4);
    ibuf0 = (int*)malloc(n1*sizeof(int));
    for (y=1; y<=y2; y++) {            /* loop over data rows */
      r = fread(ibuf0,sizeof(int),n1,fp);
      if (y < y1) continue;
      if (r != n1) break;
      if (do_swap) swap4((void*)ibuf0,n1);
      for (x=x1-1; x<x2; x++,data++) { /* loop over data columns */
        *data = (u_short)(bzero + bscale * (double)ibuf0[x]);
      }
    } /* endfor(y) */
    free((void*)ibuf0);
    break;
  }
  fclose(fp);

  if (r != n1) { free((void*)data0); data0 = NULL; }

  return data0;
}

/* --- */

int* fits_load32(const char* file,int* w,int* h,int trim)
{
  int     x,y,do_swap,bp,n1,n2,x1,x2,y1,y2;
  size_t  r=0;
  short   *sbuf0;
  int     *ibuf0;
  int     *data0,*data;
  double  bscale,bzero;
  FILE    *fp;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,file);
#endif
  assert(sizeof(int) == 4);

  fp = fopen(file,"rb");  if (!fp) return NULL;

  int i = read_header(fp,&bp,&n1,&n2,&bzero,&bscale,&x1,&x2,&y1,&y2);
#if (DEBUG > 0)
  printf("bp=%d, n1=%d, n2=%d, bz=%f, bs=%f, x=%d:%d, y=%d:%d\n",
         bp,n1,n2,bzero,bscale,x1,x2,y1,y2);
#endif
  if (!i) { fclose(fp); return NULL; }

  *w = n1; *h = n2;
  if (trim) {
    if (x1 && x2 && y1 && y2) {
      *w = x2-x1+1; *h = y2-y1+1;
    } else {
      x1 = y1 = 1; x2 = n1; y2 = n2;
    }
  } else {
    x1 = y1 = 1; x2 = n1; y2 = n2;
  }
  assert((x1 > 0) && (x2 >= x1) && (x2 <= n1) && (*w == x2-x1+1));
  assert((y1 > 0) && (y2 >= y1) && (y2 <= n2) && (*h == y2-y1+1));
  
  do_swap = (my_bigendian()) ? 0 : 1;
  data = data0 = (int*)malloc((*w)*(*h)*sizeof(int));

  switch (bp) {
  case 16:
    assert(sizeof(short) == 2);
    sbuf0 = (short*)malloc(n1*sizeof(short));
    for (y=1; y<=y2; y++) {            /* loop over data rows */
      r = fread(sbuf0,sizeof(short),n1,fp);
      if (y < y1) continue;
      if (r != n1) break;
      if (do_swap) swap2((void*)sbuf0,n1);
      for (x=x1-1; x<x2; x++,data++) { /* loop over data columns */
        *data = (int)(bzero + bscale * (double)sbuf0[x]);
      }
    } /* endfor(y) */
    free((void*)sbuf0);
    break;
  case 32:
    assert(sizeof(int) == 4);
    ibuf0 = (int*)malloc(n1*sizeof(int));
    for (y=1; y<=y2; y++) {            /* loop over data rows */
      r = fread(ibuf0,sizeof(int),n1,fp);
      if (y < y1) continue;
      if (r != n1) break;
      if (do_swap) swap4((void*)ibuf0,n1);
      for (x=x1-1; x<x2; x++,data++) { /* loop over data columns */
        *data = (int)(bzero + bscale * (double)ibuf0[x]);
      }
    } /* endfor(y) */
    free((void*)ibuf0);
    break;
  }
  fclose(fp);

  if (r != n1) { free((void*)data0); data0 = NULL; } 

  return data0;
}

/* ---------------------------------------------------------------- */

float* fits_load4(const char* file,int* w,int* h,int trim)
{
  int     x,y,do_swap,bp,n1,n2,x1,x2,y1,y2;
  size_t  r=0;
  short   *sbuf0;
  int     *ibuf0;
  float   *data0,*data;
  double  bscale,bzero;
  FILE    *fp;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,file);
#endif
  assert(sizeof(u_short) == 2);

  fp = fopen(file,"rb");  if (!fp) return NULL;

  int i = read_header(fp,&bp,&n1,&n2,&bzero,&bscale,&x1,&x2,&y1,&y2);
#if (DEBUG > 0)
  printf("bp=%d, n1=%d, n2=%d, bz=%f, bs=%f, x=%d:%d, y=%d:%d\n",
         bp,n1,n2,bzero,bscale,x1,x2,y1,y2);
#endif
  if (!i) { fclose(fp); return NULL; }

  *w = n1; *h = n2;
  if (trim) {
    if (x1 && x2 && y1 && y2) {
      *w = x2-x1+1; *h = y2-y1+1;
    } else {
      x1 = y1 = 1; x2 = n1; y2 = n2;
    }
  } else {
    x1 = y1 = 1; x2 = n1; y2 = n2;
  }
  assert((x1 > 0) && (x2 >= x1) && (x2 <= n1) && (*w == x2-x1+1));
  assert((y1 > 0) && (y2 >= y1) && (y2 <= n2) && (*h == y2-y1+1));
  
  do_swap = (my_bigendian()) ? 0 : 1;
  data = data0 = (float*)malloc((*w)*(*h)*sizeof(float));

  switch (bp) {
  case 16:
    assert(sizeof(short) == 2);
    sbuf0 = (short*)malloc(n1*sizeof(short));
    for (y=1; y<=y2; y++) {            /* loop over data rows */
      r = fread(sbuf0,sizeof(short),n1,fp);
      if (y < y1) continue;
      if (r != n1) break;
      if (do_swap) swap2((void*)sbuf0,n1);
      for (x=x1-1; x<x2; x++,data++) { /* loop over data columns */
        *data = (float)(bzero + bscale * (double)sbuf0[x]);
      }
    } /* endfor(y) */
    free((void*)sbuf0);
    break;
  case 32:
    assert(sizeof(int) == 4);
    ibuf0 = (int*)malloc(n1*sizeof(int));
    for (y=1; y<=y2; y++) {            /* loop over data rows */
      r = fread(ibuf0,sizeof(int),n1,fp);
      if (y < y1) continue;
      if (r != n1) break;
      if (do_swap) swap4((void*)ibuf0,n1);
      for (x=x1-1; x<x2; x++,data++) { /* loop over data columns */
        *data = (float)(bzero + bscale * (double)ibuf0[x]);
      }
    } /* endfor(y) */
    free((void*)ibuf0);
    break;
  }
  fclose(fp);

  if (r != n1) { free((void*)data0); data0 = NULL; }

  return data0;
}

/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
