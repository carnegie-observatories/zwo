/* ----------------------------------------------------------------
 *  
 * graph.c 
 * 
 * Project: ZwoGuider/Andor guider camera
 *
 * Christoph C. Birk, birk@carnegiescience.edu
 *
 * ---------------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG          1               /* debug level */
#endif

#define PREFUN         __func__

/* X-DEFINEs ------------------------------------------------------ */

extern int PXh;

#define PXw         (10+(PXh-14)/2)
#define XXh         (PXh+4)            /* standard height of controls */

/* INCLUDE -------------------------------------------------------- */

#define _REENTRANT                     /* multi-threaded */

#include <stdlib.h>                    /* std-C stuff */
#include <stdio.h>
#include <string.h>                    /* strlen() */
#include <math.h>                      /* floor() */
#include <assert.h>

#include "graph.h"
#include "utils.h"

/* EXTERNs -------------------------------------------------------- */

extern Application  *app;

/* ---------------------------------------------------------------- */

GraphWindow* graph_create(MainWindow* mw,Window parent,const char* fn,
                          const char* nm,int x,int y,int w,int h,int nc,int nd)
{
  int  i;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%ld,%d,%d,%d,%d,%d)\n",PREFUN,mw,parent,x,y,w,h,nc,nd);
#endif

  GraphWindow *g = (GraphWindow*)malloc(sizeof(GraphWindow));
  
  pthread_mutex_init(&g->lock,NULL); 
  g->disp = mw->disp;
  strcpy(g->name,nm);
  g->nc = nc;
  g->m = (int*)malloc(nc*sizeof(int));
  g->cur = (int*)malloc(nc*sizeof(int));
  g->data = (float**)malloc(nc*sizeof(float*));
  for (i=0; i<nc; i++) {
    g->m[i] = nd;
    g->data[i] = (float*)calloc(g->m[i],sizeof(float));
    g->cur[i] = 0;
  }
  g->vmin = -1.0f;
  g->vmax = +1.0f;
  g->half = 1;
  g->quarter = g->eighth = 0;
  g->x = x;
  g->y = y;
  g->w = w;
  g->h = h;
  CBX_Lock(0);
  g->win = XCreateSimpleWindow(g->disp,parent,x,y,w,h,1,app->black,app->lgrey);
  XMapRaised(g->disp,g->win);
  CBX_Unlock();
  CBX_SelectInput_Ext(g->disp,g->win,ExposureMask);
  g->gfc = CBX_CreateGfC_Ext(g->disp,fn,"bold",PXh);

  return g;
}

/* ---------------------------------------------------------------- */

void graph_scale(GraphWindow* g,float vmin,float vmax,int half)
{
  pthread_mutex_lock(&g->lock);
  g->vmin = vmin;
  g->vmax = vmax;
  g->half = half;                      /* show dashed line at 0.5 */
  pthread_mutex_unlock(&g->lock);
  graph_redraw(g);
}

/* ---------------------------------------------------------------- */

int graph_event(GraphWindow* g,XEvent* event) 
{
  int r=0;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s)\n",PREFUN,g->name);
#endif
  assert(g);

  switch (event->type) {
  case Expose:                         /* exposure event */
    if (event->xexpose.window == g->win) {
      if (event->xexpose.count == 0) graph_redraw(g);
      r = 1;
    }
    break;
  }
  // if (r) fprintf(stderr,"%s(%s)\n",PREFUN,g->name);

  return r; 
} 

/* ---------------------------------------------------------------- */

void graph_redraw(GraphWindow* g)
{
  int         x0,y0,x1,y1,i,j,r;
  double      w,h,y,x;
  float       *d;
  Display     *disp=g->disp;
  Window      win=g->win;
  GC          gc=g->gfc.gc;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s)\n",PREFUN,g->name);
#endif

  pthread_mutex_lock(&g->lock);

  y0 = 2+PXh/2;               y1 = g->h-PXh/2;  h = (double)(y1-y0);
  x0 = 3+PXw*strlen(g->name); x1 = g->w-PXw/2;  w = (double)(x1-x0);

  (void)CBX_Lock(0);

  XClearWindow(disp,win);              /* clear window */
  XSetForeground(disp,gc,app->black);
  XSetLineAttributes(disp,gc,2,LineSolid,CapRound,JoinRound);
  XDrawLine(disp,win,gc,x0,y0,x1,y0);  /* draw frame */
  XDrawLine(disp,win,gc,x1,y0,x1,y1);
  XDrawLine(disp,win,gc,x1,y1,x0,y1);
  XDrawLine(disp,win,gc,x0,y1,x0,y0);

  XDrawString(disp,win,gc,1,(y0+y1)/2+PXh/3,g->name,strlen(g->name));

  if (g->half) {
    XSetLineAttributes(disp,gc,1,LineOnOffDash,CapRound,JoinRound);
    y = (y0+y1)/2;
    XDrawLine(disp,win,gc,x0,y,x1,y);
  }
  if (g->quarter) {                    /* v0355 */
    XSetLineAttributes(disp,gc,1,LineOnOffDash,CapRound,JoinRound);
    y = 1*(y0+y1)/4;
    XDrawLine(disp,win,gc,x0,y,x1,y);
    y = 3*(y0+y1)/4;
    XDrawLine(disp,win,gc,x0,y,x1,y);
  }
  if (g->eighth) {                     /* NEW v0355 */
    XSetLineAttributes(disp,gc,1,LineOnOffDash,CapRound,JoinRound);
    y = 3*(y0+y1)/8;
    XDrawLine(disp,win,gc,x0,y,x1,y);
    y = 5*(y0+y1)/8;
    XDrawLine(disp,win,gc,x0,y,x1,y);
  }

  double vdyn = (double)(g->vmax - g->vmin);
  for (i=0; i<g->nc; i++) {
    double tdyn = (double)g->m[i];
    if (i==0) {                        /* channel-1 */
      XSetForeground(disp,gc,app->blue);
    } else {                           /* channel-2 */
      XSetForeground(disp,gc,app->red); 
    }
    d = g->data[i];
    for (j=0; j<g->m[i]; j++) {
      r = (j == g->cur[i]) ? 3 : 1;
      x = x0 + (w*j)/tdyn;
      y = y1 - h*(d[j]-g->vmin)/vdyn;
      y = imin(y1+2,imax(y0-2,y));
      XFillArc(disp,win,gc,x-r-1,y-r-1,2*r+1,2*r+1,0,23040);
    }
  }
  CBX_Unlock();

  pthread_mutex_unlock(&g->lock);
}

/* ---------------------------------------------------------------- */

void graph_add1(GraphWindow *g,double value,int update)
{
  graph_add(g,0,value,update);
}

/* --- */

void graph_add(GraphWindow *g,int ch,double value,int update)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%d,%.4f)\n",PREFUN,g->name,ch,value);
#endif
  assert(ch < g->nc);

  pthread_mutex_lock(&g->lock);

  int n = (g->cur[ch]+1) % g->m[ch];
  float *d = g->data[ch];
  d[n] = (float)value;
  g->cur[ch] = n;

  pthread_mutex_unlock(&g->lock);

  if (update) graph_redraw(g);
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

