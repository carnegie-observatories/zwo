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
/* todo explain strategy */
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

/* --- */

double gauss1d(Pixel p,double y0,double bias,
               double peak,double f2)
{
  double yy = ((double)p.y-y0);
  double pexp =  bias + peak*exp(-(yy*yy)/f2);

  return pexp;
}

/* ---------------------------------------------------------------- */

static double get_chi2(Pixel* p,int n,double *a)
{
  int    i;
  double f,chi=0.0;
  double bias = a[0];
  double x0 = a[1];
  double y0 = a[2];
  double peak = a[3];
  double f22 = 2.0*a[4]*a[4];          /* 2*sigma^2 */
#if (DEBUG > 2)
  fprintf(stderr,"%s(%p,%d,%p)\n",__func__,p,n,a);
#endif

  for (i=0; i<n; i++) {
    f = gauss2d(p[i],x0,y0,bias,peak,f22) - p[i].z;
    chi += f*f;
  }
  return chi;
}

/* --- */

static double get_chi1(Pixel* p,int n,double *a)
{
  int    i;
  double f,chi=0.0;
  double bias = a[0];
  double y0 = a[1];
  double peak = a[2];
  double f22 = 2.0*a[3]*a[3];          /* 2*sigma^2 */
#if (DEBUG > 2)
  fprintf(stderr,"%s(%p,%d,%p)\n",__func__,p,n,a);
#endif

  for (i=0; i<n; i++) {
    f = gauss1d(p[i],y0,bias,peak,f22) - p[i].z;
    chi += f*f;
  }
  return chi;
}

/* ---------------------------------------------------------------- */

void show_a(double *a,int n)  /* 'static' complains about non-usage */
{
  int i;
  for (i=0; i<n; i++) printf(" %7.3f",a[i]);
}

/* --- */

int fit_star(Pixel* x,int n,double* a,int itmax)
{
  int    it,i,conv=0;
  const int ndeg=5;
  double da[5]   = {1.0,0.5  ,0.5  ,2.0,0.05}; /* bias,x0,y0,peak,fwhm */
  double alim[5] = {0.1,0.002,0.002,0.2,0.002};
  double dc[5],dcold[5],chi,chi1,chi2,chiold,sumdc;
#if (TIME_TEST > 0)
  double t1 = walltime(0);
  static double s1=0,s2=0,sn=0;
#endif

  for (i=0; i<ndeg; i++) dcold[i] = 0.0;
  chi = chiold = get_chi2(x,n,a);

  for (it=1; it<=itmax; it++) {
    for (i=0; i<ndeg; i++) {
      assert(da[i] > 0);
      a[i] += da[i];                   /* test small +variation */
      chi1 = get_chi2(x,n,a);
      a[i] -= 2*da[i];                 /* test small -variation */
      chi2 = get_chi2(x,n,a);
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
    } /* endfor(i<ndeg) */
    for (i=0,sumdc=0; i<ndeg; i++) sumdc += fabs(dc[i]);
    if (sumdc > 0) {
      for (i=0; i<ndeg; i++) {         /* apply weighted change */
        a[i] -= 2.0*da[i]*(dc[i]/sumdc);
      }
    }
    chi = get_chi2(x,n,a);
    // show_a(a); printf(" it=%4d chi=%.1f, chiold=%.1f\n",it,chi,chiold);
    chiold = chi;
    if (conv == ndeg) break;
  }
#if (DEBUG > 1)
  show_a(a,ndeg); printf(" it=%d chiold=%.1f conv=%d (%d)\n",it,chi,conv,itmax);
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

  return (it < itmax) ? 0 : 1;
}

/* --- */

int fit_profile(Pixel* x,int n,double* a,int itmax)
{
  int    it,i,conv=0;
  const int ndeg=4;
  double da[4]   = {10.0,0.5  , 5.0,0.05};  /* bias,y0,peak,fwhm */
  double alim[4] = { 1.0,0.002, 0.5,0.002};
  double dc[4],dcold[4],chi,chi1,chi2,chiold,sumdc;
#if (TIME_TEST > 0)
  double t1 = walltime(0);
  static double s1=0,s2=0,sn=0;
#endif

  for (i=0; i<ndeg; i++) dcold[i] = 0.0;
  chi = chiold = get_chi1(x,n,a);
  //show_a(a,ndeg); printf(" chi=%f\n",chi); 

  for (it=1; it<=itmax; it++) {
    for (i=0; i<ndeg; i++) {
      assert(da[i] > 0);
      a[i] += da[i];                   /* test small +variation */
      chi1 = get_chi1(x,n,a);
      a[i] -= 2*da[i];                 /* test small -variation */
      chi2 = get_chi1(x,n,a);
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
    } /* endfor(i<ndeg) */
    for (i=0,sumdc=0; i<ndeg; i++) sumdc += fabs(dc[i]);
    if (sumdc > 0) {
      for (i=0; i<ndeg; i++) {         /* apply weighted change */
        a[i] -= 2.0*da[i]*(dc[i]/sumdc);
      }
    }
    chi = get_chi1(x,n,a);
    // show_a(a); printf(" it=%4d chi=%.1f, chiold=%.1f\n",it,chi,chiold);
    chiold = chi;
    if (conv == ndeg) break;
  }
#if (DEBUG > 1)
  show_a(a,ndeg); printf(" it=%d chiold=%.1f conv=%d (%d)\n",it,chi,conv,itmax);
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
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

