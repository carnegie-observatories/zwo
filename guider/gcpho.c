/* ---------------------------------------------------------------- *
 * 
 * gcpho.c
 *
 * star position and FWHM fitting
 *
 * ---------------------------------------------------------------- */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "gcpho.h"
#include "utils.h"

/* ---------------------------------------------------------------- */

#define DEBUG      1
#define TIME_TEST  0

/* ---------------------------------------------------------------- */

/* Note: 'static' and 'inline' make code slower */

double gauss2d(Pixel p,double x0,double y0,double bias,
               double peak,double f2)
{
  double xx = ((double)p.x-x0);
  double yy = ((double)p.y-y0);
  double pexp =  bias + peak*exp(-(xx*xx+yy*yy)/f2);

  return pexp;
}

/* ---------------------------------------------------------------- */

static double get_chi(Pixel* p,int n,double *a)
{
  int    i;
  double f,chi=0.0;
  double bias = a[0];
  double x0 = a[1];
  double y0 = a[2];
  double peak = a[3];
  double f22 = 2.0*a[4]*a[4];

  //fprintf(stderr,"%s(%p,%d,%p)\n",__func__,p,n,a);

  for (i=0; i<n; i++) {
    f = gauss2d(p[i],x0,y0,bias,peak,f22) - p[i].z;
    chi += f*f;
  }
  return chi;
}

/* ---------------------------------------------------------------- */

#define NDEG  5

void show_a(double *a)
{
  int i;
  for (i=0; i<NDEG; i++) printf(" %7.3f",a[i]);
}

/* --- */

int fit_star(Pixel* x,int n,double* a,int itmax)
{
  int    it,i,conv=0;
  double da[NDEG]   = {1.0,0.5  ,0.5  ,2.0,0.05}; /* bias,x0,y0,peak,fwhm */
  double alim[NDEG] = {0.1,0.002,0.002,0.2,0.002};
  double dc[NDEG],dcold[NDEG],chi,chi1,chi2,chiold,sumdc;
#if (TIME_TEST > 0)
  double t1 = walltime(0);
  static double s1=0,s2=0,sn=0;
#endif

  for (i=0; i<NDEG; i++) dcold[i] = 0.0;
  chi = chiold = get_chi(x,n,a);

  for (it=1; it<=itmax; it++) {
    for (i=0; i<NDEG; i++) {
      assert(da[i] > 0);
      a[i] += da[i];                   /* test small +variation */
      chi1 = get_chi(x,n,a);
      a[i] -= 2*da[i];                 /* test small -variation */
      chi2 = get_chi(x,n,a);
      a[i] += da[i];                   /* restore */
      dc[i] = chi1-chi2;
      if ((chi1 > chiold) && (chi2 > chiold)) {  /* found minimum */
        dc[i] = 0.0;
        if (da[i] >= alim[i]) { 
          da[i] *= 0.5;  // printf("SHRINK(%d)\n",i); 
          if (da[i] < alim[i]) conv++;
        }
      } else 
      if (dc[i]*dcold[i] < 0) {                  /* change of sign */
        if (da[i] >= alim[i]) {
          da[i] *= 0.3;  // printf("TURN(%d)\n",i);
          if (da[i] < alim[i]) conv++;
        }
      }
      dcold[i] = dc[i];
      // show_a(da); printf(" dc=%.2f\n",dc[i]);
    } /* endfor(i<NDEG) */
    for (i=0,sumdc=0; i<NDEG; i++) sumdc += fabs(dc[i]);
    if (sumdc > 0) {
      for (i=0; i<NDEG; i++) {         /* apply weighted change */
        a[i] -= 2.0*da[i]*(dc[i]/sumdc);
      }
    }
    chi = get_chi(x,n,a);
    // show_a(a); printf(" it=%4d chi=%.1f, chiold=%.1f\n",it,chi,chiold);
    chiold = chi;
    if (conv == NDEG) break;
  }
#if (DEBUG > 1)
  show_a(a); printf(" it=%d chiold=%.1f conv=%d (%d)\n",it,chi,conv,itmax);
#endif

#if (TIME_TEST > 0)
  double t2 = walltime(0)-t1;
  s1 = 0.8*s1 + 0.2*t2;
  s2 = 0.8*s2 + 0.2*t2*t2;
  sn = 0.8*sn + 0.2;
  double ave = s1/sn;
  double sig = 1000.0*sqrt(s2/sn-ave*ave);
#if (TIME_TEST < 2)
  static int cnt=0; cnt++;
  if ((cnt % 10) == 0)
#endif
  printf("walltime=%.3f msec (%.1f,%.2f)\n",1000.0*t2,1000.0*ave,sig);
#endif

  return it;
}

/* ---------------------------------------------------------------- */

int ccbphot(Pixel* x,int n,double* fit,int itmax)
{
  double a[5];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d,%p,%d)\n",__func__,x,n,fit,itmax);
#endif

  a[0] = fit[0];                       /* bias/back */
  a[1] = fit[1];                       /* x0 */
  a[2] = fit[2];                       /* y0 */
  a[3] = fit[5];                       /* peak */
  a[4] = fit[4];                       /* sigma */

  // int i;
  // for (i=0; i<5; i++) printf("a[%d]=%f ",i,a[i]); printf("\n");
  int it = fit_star(x,n,a,itmax);
  // for (i=0; i<5; i++) printf("b[%d]=%f ",i,a[i]); printf("\n");

  fit[0] = a[0];                       // background
  fit[1] = a[1];                       // x-center
  fit[2] = a[2];                       // y-center
  fit[3] = 2.0*M_PI*a[4]*a[4]*a[3];    // total count
  fit[4] = 2.35482*a[4];               // fwhm
  fit[5] = a[3];                       // peak

  return it;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

