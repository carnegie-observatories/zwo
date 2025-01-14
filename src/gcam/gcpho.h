/*
 * gcpho.h 
 *
 */

typedef struct {
  short  x,y;
  double z;
} Pixel;

int  ccbphot(Pixel* x,int n,double* fit,int itmax);
int  ccbprofile(Pixel* x,int n,double* fit,int itmax);

int fit_star    (Pixel* x,int n,double* a,int itmax);
int fit_profile4(Pixel* x,int n,double* a,int itmax);
int fit_profile3(Pixel* x,int n,double* a,int itmax);

