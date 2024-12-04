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

#define SQRLN22         2.35482

/* ---------------------------------------------------------------- */

static void run_guider1(void*);
static void run_guider3(void*);
static void run_guider4(void*);

/* ---------------------------------------------------------------- */

void* run_guider(void* param)
{
  int    err,edsOpen=0,tcsOpen=0;
  char   buf[128];
  Guider *g = (Guider*)param;

  g->fps = 1.0/g->status.exptime;
  g->fwhm = 1;                         /* just a flag here */
  g->q_flag = 0;
  strcpy(g->tcbox.text,"tc"); CBX_UpdateEditWindow(&g->tcbox); 
  strcpy(g->mxbox.text,"mx"); CBX_UpdateEditWindow(&g->mxbox);
  strcpy(g->bkbox.text,"bk"); CBX_UpdateEditWindow(&g->bkbox);
  strcpy(g->fwbox.text,"fw"); CBX_UpdateEditWindow(&g->fwbox);

  switch (g->gmode) {                  /* plot title/scaling NEW v0404 */
  case GMODE_PR: default:
    g->g_tc->eighth = g->g_fw->eighth = 0;
    strcpy(g->g_tc->name,"tc"); graph_scale(g->g_tc,0,10000,0); 
    strcpy(g->g_fw->name,"fw"); graph_scale(g->g_fw,0.0,2.0,0); 
    break;
  case GMODE_SV:    // todo temporary ? remove ?
    g->g_tc->eighth = g->g_fw->eighth = 1;
    strcpy(g->g_tc->name,"dx"); graph_scale(g->g_tc,-0.4,0.4,0); 
    strcpy(g->g_fw->name,"dy"); graph_scale(g->g_fw,-0.2,0.2,0); 
    break;
  case GMODE_SV4:   // todo temporary ? remove ?
    g->g_tc->eighth = g->g_fw->eighth = 1;
    strcpy(g->g_tc->name,"dx"); graph_scale(g->g_tc,-0.4,0.4,0); 
    strcpy(g->g_fw->name,"dy"); graph_scale(g->g_fw,-4.0,4.0,0); 
    break;
  }

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
  case GMODE_PR:
    run_guider1(g);
    break;
  case GMODE_SV:
    run_guider3(g);
    break;
  case GMODE_SV4:
    run_guider4(g);
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
  sprintf(g->gdbox.text,"gd  off"); CBX_UpdateEditWindow(&g->gdbox);
  printf("%s(%s) -- stopped)\n",PREFUN,g->name);

  return (void*)0;
}

/* ---------------------------------------------------------------- */

static void run_guider1(void* param)
{
  double t1,t2;
  double cx,cy,gx,gy,fwhm=0,back,flux,peak;
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
#if (DEBUG > 2)
  int debug_cnt=0;
#endif
  while (g->loop_running && qltool->guiding) {
    msleep(20);
    ZwoFrame *frame = zwo_frame4reading(server,seqNumber);
    if (frame) {
      seqNumber = frame->seqNumber;
      if (fwhm == 0 || (g->q_flag==2)) {  /* first (or bad) fit */
        fwhm = g->px * get_fwhm(frame->data,frame->w,frame->h,ix,iy,
                         qltool->vrad,1.0,1.0,&back,&cx,&cy,&peak,&flux);
        if (!(fwhm > 0)) fwhm = 0.8;   /* default [arcsec] */
      }
#if (DEBUG > 1)
      printf("\nback=%.1f, cx=%.1f, cy=%.1f, peak=%.1f, fwhm=%.3f\n",
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
        for (xx=-vrad,ppix=0; xx<=vrad; xx++) {  /* loop over box */
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
        fit[3] = peak;                 /* peak of gauss -- not peak pixel */
        fit[4] = fwhm/(SQRLN22*g->px); /* sigma [pixels] */
        g->q_flag = fit_star(pbuf,i,fit,400);
        back = fit[0];
        cx   = fit[1];
        cy   = fit[2];
        flux = 2.0*M_PI*fit[3]*fit[4]*fit[4];
        peak = fit[3];
        fwhm = SQRLN22*fit[4]*g->px;   /* FWHM [arcsec] */
#if (DEBUG > 1)
        printf("back=%.1f, cx=%.1f, cy=%.1f, peak=%.1f, fwhm=%.3f, flux=%.0f\n",
               back,cx,cy,peak,fwhm,flux);
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
          gx = cx; ix = qltool->curx[QLT_BOX] = (int)my_round(gx,0);
          gy = cy; iy = qltool->cury[QLT_BOX] = (int)my_round(gy,0);
        }
        g->flux = flux;
        g->ppix = (double)ppix;
        g->back = back;
        g->fwhm = fwhm;
        g->dx = (cx-gx); 
        g->dy = (cy-gy);
        if (qltool->guiding < 0) graph_scale(g->g_tc,0,1.333*flux,0);
        graph_add1(g->g_tc,flux,0);    // todo how often ?Shec ?Povilas
        graph_add1(g->g_fw,fwhm,0);
        rotate(g->px*g->dx,g->px*g->dy,g->pa,g->parity,&azerr,&elerr);
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
#if (DEBUG > 2)
      debug_cnt++; printf("_cnt=%d\n",debug_cnt);
#endif
    } // endif(frame)
  } // endwhile(loop-doing && guiding)

  free((void*)pbuf);
}

/* ---------------------------------------------------------------- */

static void run_guider3(void* param)          /* v0350 */
{
  double t1,t2,last;
  double dx=0,dy=0,azerr,elerr;
  int    ix,iy;
  u_int  seqNumber=0,counter=0;
  Guider *g = (Guider*)param;
  QlTool *qltool = g->qltool;
  ZwoStruct *server = g->server;

  last = t1 = walltime(0);
  while (g->loop_running && qltool->guiding) {
    msleep(20);
    ZwoFrame *frame = zwo_frame4reading(server,seqNumber);
    if (frame) {
      seqNumber = frame->seqNumber;
      ix = (int)my_round(qltool->curx[QLT_BOX],0);
      iy = (int)my_round(qltool->cury[QLT_BOX],0);
      get_quads(frame->data,frame->w,frame->h,ix,iy,qltool->vrad,&dx,&dy);
      zwo_frame_release(server,frame);
      t2 = walltime(0);
      pthread_mutex_lock(&g->mutex);
      g->fps = 0.8*g->fps + 0.2/(t2-t1);
      t1 = t2;
      g->dx = dx;                      /* use as criterion if we */
      g->dy = dy;                      /* should send a correction */
#if 1 // xxx temporary plot todo ? remove ?
      graph_add1(g->g_tc,dx,1); 
      graph_add1(g->g_fw,dy,1);
#endif
      dx = ((dx > 0) ? 0.1 : -0.1) / (g->px); /* [arcsec] */
      dy = ((dy > 0) ? 0.1 : -0.1) / (g->px); /* [arcsec] */
      counter++;                       /* only every 'av' frames or 5 seconds */
      qltool->guiding = abs(qltool->guiding);  
      if ((counter > server->rolling) && ((walltime(0)-last) >= 5.0)) { 
        rotate(g->px*dx,g->px*dy,g->pa,g->parity,&azerr,&elerr);
        graph_add1(g->g_az,azerr,0);
        graph_add1(g->g_el,elerr,0);
        counter = 0; last = walltime(0);
        if ((fabs(g->dx) > 0.1) || (fabs(g->dy) > 0.05)) { /* v0355 */
          g->azg = g->sens * azerr;
          g->elg = g->sens * elerr;
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

static void run_guider4(void* param)          /* NEW v0404 */
{
  double t1,t2,last=0;
  double fit[5];
  double dx=0,dy=0,ody=0,ddy,gx,gy,azerr,elerr;
  double back,fwhm=0,flux,cy=0;
  int    ix,iy,vrad=0,ppix,v;
  u_int  seqNumber=0;
  Guider *g = (Guider*)param;
  QlTool *qltool = g->qltool;
  ZwoStruct *server = g->server;
  Pixel *pbuf=NULL;

  t1 = walltime(0);
  while (g->loop_running && qltool->guiding) {
    msleep(20);
    ZwoFrame *frame = zwo_frame4reading(server,seqNumber);
    if (frame) { int i=0,x,y,xx,yy,npix; double s,pk1=0,pk2=0,pk3=0;
      seqNumber = frame->seqNumber;
      gx = my_round(qltool->curx[QLT_BOX],1); /* 0.1 pixel resolution */
      gy = my_round(qltool->cury[QLT_BOX],1);
      ix = (int)my_round(gx,0);
      iy = (int)my_round(gy,0);
      if (vrad != qltool->vrad) {      /* aperture/cross size */
        vrad = qltool->vrad;
        npix = 1+2*vrad;
        pbuf = (Pixel*)realloc(pbuf,npix*sizeof(Pixel));
      }
      for (yy=-vrad,ppix=0; yy<=vrad; yy++) { /* get profile along y-axis */
        y = iy + yy;
        if (y < 0) continue;
        if (y >= frame->h) break;
        for (xx=-vrad,s=0; xx<=vrad; xx++) {   /* add pixels along x-axis */
          x = ix+xx;
          if (x < 0) continue;
          if (x >= frame->w) break;
          v = frame->data[x+y*frame->w]; if (v > ppix) ppix = v;
          s += (double)v; 
        }
        pbuf[i].y = y;
        pbuf[i].z = s;
        if      (s > pk1) { pk3 = pk2; pk2 = pk1; pk1 = s; } 
        else if (s > pk2) { pk3 = pk2; pk2 = s; }
        else if (s > pk3) { pk3 = s; }
        i++;
      } 
      assert(i <= npix);
      back = get_quads(frame->data,frame->w,frame->h,ix,iy,vrad,&dx,&dy); 
#if (DEBUG > 1)
      printf("\nback=%.0f, peak=%.0f,%.0f,%.0f\n",back,pk1,pk2,pk3);
#endif
      if (fwhm <= 0) fwhm  = 0.7;      /* default [arcsec] */
      if (cy <= 0) cy = iy;
      fit[0] = back*(2*vrad+1);
      fit[1] = cy;
      fit[2] = pk2 - fit[0];
      fit[3] = fwhm/(SQRLN22*g->px); /* sigma [pixels] */
      g->q_flag = fit_profile(pbuf,i,fit,3000);
      back = fit[0]/(2*vrad+1);
      cy   = fit[1];
      flux = sqrt(2.0*M_PI)*fit[2]*fit[3];
      fwhm = SQRLN22*fit[3]*g->px;   /* FWHM [arcsec] */
#if (DEBUG > 1)
      double peak = fit[2];
      printf("back=%.0f, dy=%.1f, peak=%.1f, fwhm=%.3f, flux=%.0f\n",
             back,cy,peak,fwhm,flux);
#endif
      zwo_frame_release(server,frame);
      t2 = walltime(0);
      pthread_mutex_lock(&g->mutex);
      g->fps = 0.8*g->fps + 0.2/(t2-t1);
      t1 = t2;
      g->dx = dx;                      /* dimensionless ratio */
      g->dy = cy-gy;                   /* [pixels] from fit */
      g->flux = flux;
      g->ppix = ppix;
      g->back = back;
      g->fwhm = fwhm;
#if 1 // xxx temporary plot todo ? show flux & fwhm
      graph_add1(g->g_tc,g->dx,1); 
      graph_add1(g->g_fw,g->dy,1);
#endif
      dx = ((dx > 0) ? 0.1 : -0.1);      /* [arcsec] */
      if (walltime(0)-last < 5.0) dx = 0; 
      else                        last = walltime(0);
      ddy = (qltool->guiding < 0) ? 0.0 : g->dy-ody; /* derivative */
      ody = g->dy;                     /* old 'dy' [pixels] */
      dy = g->dy + (server->rolling+1.0)*ddy; /* [pixels] */
      //printf("dy=%+.1f, ddy=%+.1f, dx=%+.2f, dy=%+.2f\n",g->dy,ddy,dx,dy); 
      qltool->guiding = abs(qltool->guiding);  
      if (dx || fabs(dy*g->px) > 0.05) { 
        double fdge = (g->q_flag == 0) ? 0.35 : 0.2;
        dy *= g->px * fdge;            /* [arcsec] */
        rotate(dx,dy,g->pa,g->parity,&azerr,&elerr);
        //printf("dx=%.3f dy=%.3f, azerr=%.3f elerr=%.3f\n",dx,dy,azerr,elerr);
        graph_add1(g->g_az,azerr,0);
        graph_add1(g->g_el,elerr,0);
        g->azg = g->sens * azerr;
        g->elg = g->sens * elerr;
        if ((qltool->guiding == 3) || (qltool->guiding == 5)) {
          if (g->gmpar == 'p') telio_gpaer(3,-g->azg,-g->elg); 
          telio_aeg(g->azg,g->elg);
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

