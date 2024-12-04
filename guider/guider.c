/* ----------------------------------------------------------------
 *  
 * guider.c
 *
 * Project: ZWO Guider Camera software (OCIW, Pasadena, CA)
 *
 * ---------------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           1          /* debug level */
#endif

/* ---------------------------------------------------------------- */

#define _REENTRANT

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "zwogcam.h"
#include "zwotcp.h"
#include "guider.h"
#include "gcpho.h"
#include "telio.h"
#include "eds.h"
#include "utils.h"

/* ---------------------------------------------------------------- */

#define PREFUN          __func__

/* ---------------------------------------------------------------- */

static void run_guider1(void*);
static void run_guider3(void*);

/* ---------------------------------------------------------------- */

void* run_guider(void* param)
{
  int    err,edsOpen=0,tcsOpen=0;
  char   buf[128];
  Guider *g = (Guider*)param;

  g->fps = 1.0/g->status.exptime;
  g->fwhm = 1;
  g->q_flag = 0;
  strcpy(g->tcbox.text,"tc"); CBX_UpdateEditWindow(&g->tcbox); 
  strcpy(g->mxbox.text,"mx"); CBX_UpdateEditWindow(&g->mxbox);
  strcpy(g->bkbox.text,"bk"); CBX_UpdateEditWindow(&g->bkbox);
  strcpy(g->fwbox.text,"fw"); CBX_UpdateEditWindow(&g->fwbox);

  err = telio_open(2);                 /* v0310 */
  if (err) {
    sprintf(buf,"connection to TCSIS failed (err=%d)",err);
    message(g,buf,MSS_ERROR);
  } else tcsOpen = 1;

  if (g->gmode == GMODE_PR) {
    err = eds_open(2);                 /* v0336 */
    if (err) {
      sprintf(buf,"connection to EDS failed (err=%d)",err);
      message(g,buf,MSS_ERROR);
    } else edsOpen = 1;
  }

  switch (g->gmode) {
  case 1:
    run_guider1(g);
    break;
  case 3:
    run_guider3(g);
    break;
  default:   // todo ?Povilas
    message(g,"invalid guider mode",MSS_WARN);
    g->gid = 0;
    break;
  }
  g->qltool->guiding = g->update_flag = 0;

  if (edsOpen) eds_close();
  if (tcsOpen) telio_close();

  sprintf(g->fgbox.text,"%d",0); CBX_UpdateEditWindow(&g->fgbox);
  printf("%s(%s) -- stopped)\n",PREFUN,g->name);

  return (void*)0;
}

/* ---------------------------------------------------------------- */

void run_guider1(void* param)
{
  double t1,t2;
  double cx,cy,gx,gy,dx=0,dy=0,fwhm=0,back,flux,peak;
  double fit[6];
  double next_eds=0;
  int    ix,iy,ppix=0,vrad=0,npix=0,doit=1;
  u_int  seqNumber=0,counter=0;
  Pixel  *pbuf=NULL;
  Guider *g = (Guider*)param;
  QlTool *qltool = g->qltool;
  ZwoStruct *server = g->server;

  gx = my_round(qltool->curx[QLT_BOX],1);  /* 0.1 pixel resolution */
  gy = my_round(qltool->cury[QLT_BOX],1);
  ix = (int)my_round(gx,0);
  iy = (int)my_round(gy,0);

  t1 = walltime(0);
#if (DEBUG > 1)
  int debug_cnt=0;
#endif
  while (g->loop_running && qltool->guiding) {
    msleep(20);
    ZwoFrame *frame = zwo_frame4reading(server,seqNumber);
    if (frame) {
      seqNumber = frame->seqNumber;
      if (fwhm == 0 || (g->q_flag==2)) {  /* first (or bad) frame */
        fwhm = get_fwhm(frame->data,frame->w,frame->h,ix,iy,qltool->vrad,1.0,1.0,
                   &back,&cx,&cy,&peak,&flux);
        if (!(fwhm > 0)) fwhm = 0.9/g->px;  /* default */
      } else {                         /* last measurement as starting point */
        fwhm /= g->px;
        cx = dx;
        cy = dy;
      }
#if (DEBUG > 1)
      printf("back=%.1f, cx=%.1f, cy=%.1f, peak=%.1f, fwhm=%.3f\n",
             back,cx,cy,peak,fwhm);
#endif
      assert(fwhm > 0);
      if (vrad != qltool->vrad) {      /* box size changed */
        vrad = qltool->vrad;
        npix =(1+2*vrad) * (1+2*vrad);
        pbuf = (Pixel*)realloc(pbuf,npix*sizeof(Pixel));
      }
      double ggx = my_round(qltool->curx[QLT_BOX],1);
      double ggy = my_round(qltool->cury[QLT_BOX],1);
      if ((gx != ggx) || (gy != ggy)) {    /* used to drag in F5 mode */
        gx = ggx; gy = ggy;
        printf("telescope dragging (%f,%f) !!!!\n",gx,gy);
        ix = (int)my_round(gx,0); iy = (int)my_round(gy,0);
      }
      assert(npix); assert(pbuf);
      if (fwhm > 0) { int x,y,v,xx,yy,i=0; /* we have a valid estimate */
        ppix=0;                            /* peak pixel */
        for (xx=-vrad; xx<=vrad; xx++) {   /* loop over box */
          x = ix + xx;
          if (x < 0) continue;             /* outside frame */
          if (x >= frame->w) break;
          for (yy=-vrad; yy<=vrad; yy++) {
            y = iy + yy;
            if (y < 0) continue;
            if (y >= frame->h) break;
            pbuf[i].x = x;
            pbuf[i].y = y;
            v = frame->data[x+y*frame->w]; if (v > ppix) ppix = v;
            pbuf[i].z = (double)v;
            i++;
          }
        }
        assert(i <= npix);
        fit[0] = back;
        fit[1] = cx;
        fit[2] = cy;
       // not used fit[3] = flux;
        fit[4] = fwhm/2.35482;         /* sigma [pixels] */
        fit[5] = peak;
        int itmax = 50+(int)(400.0*g->status.exptime);
        int it = ccbphot(pbuf,i,fit,itmax);
        // printf("it=%d (%d)\n",it,itmax); 
        g->q_flag = (it > itmax) ? 1 : 0;
        back = fit[0];
        dx   = fit[1];
        dy   = fit[2];
        flux = fit[3];
        fwhm = fit[4] * g->px;         /* FWHM [arcsec] */
        peak = fit[5];
#if (DEBUG > 1)
        printf("back=%.1f, dx=%.1f, dy=%.1f, peak=%.1f, fwhm=%.3f, flux=%.0f\n",
               back,dx,dy,peak,fwhm/g->px,flux);
#endif
#ifdef ENG_MODE
        if ((fwhm > 5.0) || (fwhm < 0.1)) g->q_flag = 2;  /* sanity check v0337 */
#else
        if ((fwhm > 5.0) || (fwhm < 0.2)) g->q_flag = 2;  /* sanity check */
#endif
      } else {
        fprintf(stderr,"no star !!!!!!!!!!!!!!!!!!!!\n");
        g->q_flag = 2;
      } /* endif(fwhm) */
      zwo_frame_release(server,frame);
      t2 = walltime(0);
      pthread_mutex_lock(&g->mutex);
      g->fps = 0.8*g->fps + 0.2/(t2-t1);
      t1 = t2;
      if (g->q_flag < 2) { double azerr,elerr; /* we have valid measurement */
        if ((qltool->guiding == -4) || (qltool->guiding == -5)) {  /* adjust g.box */
          gx = dx; ix = qltool->curx[QLT_BOX] = (int)my_round(gx,0);
          gy = dy; iy = qltool->cury[QLT_BOX] = (int)my_round(gy,0);
        }
        g->flux = flux;
        g->ppix = (double)ppix;
        g->back = back;
        g->fwhm = fwhm;
        g->dx = (dx-gx);
        g->dy = (dy-gy);
        if (qltool->guiding < 0) graph_scale(g->g_tc,0,1.333*flux,0);
        graph_add1(g->g_tc,flux,0);    // todo how often ?Shec ?Povilas
        graph_add1(g->g_fw,fwhm,0);
        rotate(g->px*(dx-gx),g->px*(dy-gy),g->pa,g->parity,&azerr,&elerr);
        graph_add1(g->g_az,azerr,0);
        graph_add1(g->g_el,elerr,0);
        double fdge = (g->q_flag == 0) ? 0.35 : 0.2;
        if (qltool->guiding < 0) {     /* first pass -- no derivative */
          g->azg = fdge * g->sens * azerr;
          g->elg = fdge * g->sens * elerr;
        } else {                       /* use derivative */
          g->azg  = azerr + ((server->rolling + 1.0) * (azerr - g->azerp));
          g->azg *= fdge * g->sens;
          g->elg  = elerr + ((server->rolling + 1.0) * (elerr - g->elerp));
          g->elg *= fdge * g->sens;
        }
        g->azerp = azerr;              /* store last error */
        g->elerp = elerr;
        qltool->guiding = abs(qltool->guiding);
        counter++; doit=1;
        if (server->rolling && (walltime(0) < next_eds)) {
          if (counter % server->rolling) doit = 0;
        }
        if (doit) {                    /* send EDS-801 v0336 */
          next_eds = walltime(0) + 1.0; /* v0347 */
          eds_send801(g->gnum,fwhm,qltool->guiding,g->dx,g->dy,flux);
        }
        if ((qltool->guiding == 3) || (qltool->guiding == 5)) {
          telio_aeg(g->azg,g->elg);  // todo everytime ?Povilas
        } 
      } else {
        g->azg = g->elg = 0;
      } // endif(q_flag)
      g->update_flag = True;           /* update GUI */
      pthread_mutex_unlock(&g->mutex);
#if (DEBUG > 1)
      debug_cnt++; printf("_cnt=%d\n",debug_cnt);
#endif
    } // endif(frame)
  } // endwhile(loop-doing && guiding)

  free((void*)pbuf);

}

/* ---------------------------------------------------------------- */

void run_guider3(void* param)          /* v0350 */
{
  double t1,t2,last;
  double gx,gy,dx=0,dy=0,azerr,elerr;
  int    ix,iy;
  u_int  seqNumber=0,counter=0;
  Guider *g = (Guider*)param;
  QlTool *qltool = g->qltool;
  ZwoStruct *server = g->server;

#if 1 //xxx temporary plot
  g->g_tc->eighth = g->g_fw->eighth = 1;
  graph_scale(g->g_tc,-0.4,0.4,0);  // dx 
  graph_scale(g->g_fw,-0.2,0.2,0);  // dy
#endif

  gx = my_round(qltool->curx[QLT_BOX],1);  /* 0.1 pixel resolution */
  gy = my_round(qltool->cury[QLT_BOX],1);
  ix = (int)my_round(gx,0);
  iy = (int)my_round(gy,0);

  last = t1 = walltime(0);
  while (g->loop_running && qltool->guiding) {
    msleep(20);
    ZwoFrame *frame = zwo_frame4reading(server,seqNumber);
    if (frame) {
      seqNumber = frame->seqNumber;
      assert(g->gmode == GMODE_SV);
      get_quads(frame->data,frame->w,frame->h,ix,iy,qltool->vrad,&dx,&dy);
#if 1 // todo move up ?
      double ggx = my_round(qltool->curx[QLT_BOX],1);
      double ggy = my_round(qltool->cury[QLT_BOX],1);
      if ((gx != ggx) || (gy != ggy)) {
        gx = ggx; gy = ggy;
        printf("telescope dragging (%f,%f) !!!!\n",gx,gy);
        ix = (int)my_round(gx,0); iy = (int)my_round(gy,0);
      }
#endif
      zwo_frame_release(server,frame);
      t2 = walltime(0);
      pthread_mutex_lock(&g->mutex);
      g->fps = 0.8*g->fps + 0.2/(t2-t1);
      t1 = t2;
      g->dx = dx;                      /* use as criterion if we */
      g->dy = dy;                      /* should send a correction */
#if 1 // xxx temporary plot NEW 
      graph_add1(g->g_tc,dx,1); 
      graph_add1(g->g_fw,dy,1);
#endif
      dx = ((dx > 0) ? 0.1 : -0.1) / (g->px); // TODO remove 'px' here and 
      dy = ((dy > 0) ? 0.1 : -0.1) / (g->px); // below at 'rotate'
      counter++;                       /* only every 'av' frames or 5 seconds */
      if ((counter > server->rolling) && ((walltime(0)-last) >= 5.0)) { 
        rotate(g->px*dx,g->px*dy,g->pa,g->parity,&azerr,&elerr);
        graph_add1(g->g_az,azerr,0);
        graph_add1(g->g_el,elerr,0);
        counter = 0; last = walltime(0);
        if ((fabs(g->dx) > 0.1) || (fabs(g->dy) > 0.05)) { /* NEW v0355 */
          g->azg = g->sens * azerr;
          g->elg = g->sens * elerr;
          qltool->guiding = abs(qltool->guiding);  
          if ((qltool->guiding == 3) || (qltool->guiding == 5)) {
            assert(g->gmode == 3);
            if (g->gmpar == 'p') telio_gpaer(3,-g->azg,-g->elg);  /* v0354 */
            telio_aeg(g->azg,g->elg);
          }
        }
      }
      g->update_flag = True;           /* update GUI */
      pthread_mutex_unlock(&g->mutex);
    } // endif(frame)
  } // endwhile(loop-doing && guiding)
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

