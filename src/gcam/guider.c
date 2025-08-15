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

static void tcs_error(Guider *g,int err);
static int  tcs_recon(Guider *g);

static int  tcsOpen=0;

/* ---------------------------------------------------------------- */

void* run_guider(void* param)
{
  int    err,edsOpen=0;
  char   buf[128];
  Guider *g = (Guider*)param;

  g->fps = 1.0/g->status.exptime;
  g->fwhm = 1;                         /* just a flag here */
  g->q_flag = 0;
  strcpy(g->tcbox.text,"tc"); CBX_UpdateEditWindow(&g->tcbox); 
  strcpy(g->mxbox.text,"mx"); CBX_UpdateEditWindow(&g->mxbox);
  strcpy(g->bkbox.text,"bk"); CBX_UpdateEditWindow(&g->bkbox);
  strcpy(g->fwbox.text,"fw"); CBX_UpdateEditWindow(&g->fwbox);

  err = telio_open(2);                 /* v0310 */
  if (err) {
    sprintf(buf,"connection to TCSIS failed (err=%d)",err);
    message(g,buf,MSS_ERROR);
  } 
  tcsOpen = (err) ? 0 : 1;

  if (!err) {                          /* TCSIS connection ok v0417 */
    if (g->gmode == GM_PR) {           /* gm1 */
      if (err) {
        sprintf(buf,"connection to EDS failed (err=%d)",err);
        message(g,buf,MSS_ERROR);
      } else edsOpen = 1;
    }
    switch (g->gmode) {
    case GM_PR:
    case GM_SV5:                       /* v0416 */
      run_guider1(g);
      break;
    case GM_SV3:
      run_guider3(g);
      break;
    case GM_SV4:
      run_guider4(g);
      break;
    default: 
      message(g,"invalid guider mode",MSS_WARN);
      g->gid = 0;
      break;
    }
  } /* endif(err) */
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
  double r0=1,a0=0,n0=0;               /* gm5 stuff v0416 */
  int    gm5_locked=0;
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
      assert(npix); assert(pbuf);
      if (g->gmode == GM_SV5) {        /* gm5 mode v0416 */
        if (qltool->guiding < 0) gm5_locked = 0;
        if (!gm5_locked) {             /* store distance and angle */
          msleep(500);                 /* delay for telescope update v0420 */
          if (qltool->guiding > 0) {   /* 2nd iteration (stable) */
            double x = qltool->curx[QLT_BOX] - qltool->curx[QLT_BOX-1];
            double y = qltool->cury[QLT_BOX] - qltool->cury[QLT_BOX-1];
            n0 = g->north;             /* v0419 */
            r0 = qltool->arc_radius = sqrt(x*x+y*y);   /* [pixel] */
            a0 = atan2(y,x); while (a0 < 0) { a0 += 2.0*M_PI; } /* [radians] */
            qltool->arc_angle = a0 * RADS;   /* [degrees] */
            // printf("r=%f, a=%f, p=%f\n",r0,qltool->arc_angle,n0); 
            a0 = (qltool->arc_angle + g->parit2*g->parity*n0); // todo sign=parit2
            gm5_locked = 1;
          }
        } else {                       /* we have a lock in gm5 */
          if (g->north != n0) {        /* position angle changed */
            n0 = g->north;
            // printf("r=%f, a=%f, p=%f\n",r0,a0,n0);
            double x = qltool->curx[QLT_BOX-1];  /* cursor4: center */
            double y = qltool->cury[QLT_BOX-1];
            double a = (a0-(g->parit2*g->parity*n0))/RADS; // todo sign=parit2
            // todo? qltool->arc_angle = a*RADS; printf("a=%f\n",qltool->arc_angle);
            qltool->curx[QLT_BOX] = gx = x + r0*cos(a);
            qltool->cury[QLT_BOX] = gy = y + r0*sin(a);
            ix = my_round(gx,0);       /* v0421 */
            iy = my_round(gy,0);
            qltool_redraw(g->qltool,False);
            redraw_gwin(g);
          }
        }
      } else {                         /* has box been moved ? */
        double ggx = my_round(qltool->curx[QLT_BOX],1);
        double ggy = my_round(qltool->cury[QLT_BOX],1);
        if ((gx != ggx) || (gy != ggy)) {
          gx = ggx; gy = ggy;
          if (g->gmode != GM_SV5) printf("telescope dragging (%f,%f) !!!\n",gx,gy);
          ix = (int)my_round(gx,0); iy = (int)my_round(gy,0);
        }
      } /* endif(SV5) */
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
        if ((fwhm > 5.0) || (fwhm < 0.1)) g->q_flag = 2;  /* v0337 */
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
        if ((qltool->guiding == -4) || (qltool->guiding == -5)) {  /* adjust */
          gx = cx; ix = qltool->curx[QLT_BOX] = (int)my_round(gx,0); /* guide */
          gy = cy; iy = qltool->cury[QLT_BOX] = (int)my_round(gy,0); /* box */
        }
        g->flux = flux;
        g->ppix = (double)ppix;
        g->back = back;
        g->fwhm = fwhm;
        g->dx = (cx-gx); 
        g->dy = (cy-gy);
        if (qltool->guiding < 0) graph_scale(g->g_tc,0,1.333*flux,0);
        graph_add1(g->g_tc,flux,0);
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
	//	if (server->rolling && (walltime(0) < next_eds)) {
	//          if (counter % server->rolling) doit = 0;
	//}
	//	if (doit) {                    /* send EDS-801 v0336 */
	if (walltime(0) >= next_eds) {
          next_eds = walltime(0) + 1.0; /* v0347 */
          eds_send801(g->gnum,fwhm,qltool->guiding,g->dx,g->dy,flux);
	  // This is a hack to get cursor 1 & 5 out for  coordinated offsets
	  eds_send82i(g->gnum,1,g->qltool->curx[0],g->qltool->cury[0]);
	  eds_send82i(g->gnum,5,g->qltool->curx[4],g->qltool->cury[4]);

        }
        if ((qltool->guiding == 3) || (qltool->guiding == 5)) { int err=0;
          if (!tcsOpen) err = tcs_recon(g);
          if (!err) {
            err = telio_aeg(g->azg,g->elg); /* v0417 */
            if (err) tcs_error(g,err);
          }
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

  qltool->arc_radius = 0;

  free((void*)pbuf);
}

/* ---------------------------------------------------------------- */

static void run_guider3(void* param)          /* v0350 */
{
  double t1,t2,last;
  double rx=0,ry=0,dx,dy,azerr,elerr,flux;
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
      get_quads(frame->data,frame->w,frame->h,ix,iy,qltool->vrad,&rx,&ry,&flux);
      zwo_frame_release(server,frame);
      t2 = walltime(0);
      pthread_mutex_lock(&g->mutex);
      g->fps = 0.8*g->fps + 0.2/(t2-t1);
      t1 = t2;
      g->dx = rx;                      /* use as criterion if we */
      g->dy = ry;                      /* should send a correction */
      graph_add1(g->g_tc,rx,0); 
      graph_add1(g->g_fw,ry,0);
      dx = (rx > 0) ? 0.1 : -0.1;      /* [arcsec] */
      dy = (ry > 0) ? 0.1 : -0.1;      /* [arcsec] */
      counter++;                       /* only every 'av' frames or 5 seconds */
      qltool->guiding = abs(qltool->guiding);  
      if ((counter > server->rolling) && ((walltime(0)-last) >= 5.0)) { 
        rotate(dx,dy,g->pa,g->parity,&azerr,&elerr);
        graph_add1(g->g_az,azerr,0);
        graph_add1(g->g_el,elerr,0);
        counter = 0; last = walltime(0);
        if ((fabs(rx) > 0.1) || (fabs(ry) > 0.05)) { /* v0355 */
          g->azg = g->sens * azerr;
          g->elg = g->sens * elerr;
          if ((qltool->guiding == 3) || (qltool->guiding == 5)) { int err=0;
            if (!tcsOpen) err = tcs_recon(g);
            if (!err) { 
              if (g->gmpar == 'p') telio_gpaer(3,-g->azg,-g->elg);  /* v0354 */
              err = telio_aeg(g->azg,g->elg);
              if (err) tcs_error(g,err);
            }
          } /* endif(guiding) */
        } /* endif(fabs(rx,ry) */
      } /* endif(counter) */
      g->update_flag = True;           /* update GUI */
      pthread_mutex_unlock(&g->mutex);
    } // endif(frame)
  } // endwhile(loop-doing && guiding)
}

/* ---------------------------------------------------------------- */

static void run_guider4(void* param)          /* v0404 */
{
  double t1,t2;
  double fit[4];
  double dx=0,dy=0,rx,ry,ody=0,ddy,odx=0,ddx,drx,gx,gy,azerr,elerr;
  double back,fwhm=0,flux,cy=0,scale;
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
    if (frame) { int x,y,xx,yy,npix,n; double s,pk1=0,pk2=0,pk3=0;
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
      for (n=0,yy=-vrad,ppix=0; yy<=vrad; yy++) { /* get profile along y-axis */
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
        pbuf[n].y = y;
        pbuf[n].z = s;
        if      (s > pk1) { pk3 = pk2; pk2 = pk1; pk1 = s; } 
        else if (s > pk2) { pk3 = pk2; pk2 = s; }
        else if (s > pk3) { pk3 = s; }
        n++;
      } 
      assert(n <= npix);
      back = get_quads(frame->data,frame->w,frame->h,ix,iy,vrad,&rx,&ry,&flux);
#if (DEBUG > 1)
      printf("\nb=%.0f rx=%.3f flux=%.0f p=%.0f,%.0f,%.0f\n",back,rx,flux,pk1,pk2,pk3);
#endif
      if ((fwhm <= 0) || (g->q_flag > 1)) fwhm = 0.7;  /* default [arcsec] */
      if ((cy <= 0) || (g->q_flag > 1)) cy = iy;
      fit[0] = back*(2*vrad+1);        /* y-fit */
      fit[1] = cy;
      fit[2] = pk2 - fit[0];
      fit[3] = fwhm/(SQRLN22*g->px); /* sigma [pixels] */
      g->q_flag = fit_profile4(pbuf,n,fit,3000);
      back = fit[0]/(2*vrad+1);
      cy   = fit[1];
      fwhm = SQRLN22*fit[3]*g->px;   /* FWHM [arcsec] */
      if ((fwhm > 5.0) || (fwhm < 0.1)) g->q_flag = 2; /* sanity check */
      if ((fabs(cy-gy) > vrad) || (flux < 1*n*n))  g->q_flag = 2;
#if (DEBUG > 1)
      printf("back=%.0f, dy=%.1f, peak=%.1f, fwhm=%.3f, flx=%.0f\n",
             back,cy,fit[2],fwhm,flx);
#endif
      drx = calc_quad(vrad,g->slitW,fit[3],1.0,NULL); /* quadrant ratio */
      dx = rx/drx;
      (void)calc_quad(vrad,g->slitW,fit[3],dx,&scale); /* scale factor */
      flux *= scale;                   /* v0413 */
#if (DEBUG > 1)
      printf("rx=%f, drx=%f, dx=%f, sf=%f\n",rx,drx,dx,scale);
#endif
      zwo_frame_release(server,frame);
      pthread_mutex_lock(&g->mutex);
      t2 = walltime(0);
      g->fps = 0.8*g->fps + 0.2/(t2-t1);
      t1 = t2;
      if (g->q_flag < 2) {             /* ok fit */
        g->dx = dx;                    /* [pixels] from ratio v0408 */
        g->dy = cy-gy;                 /* [pixels] from fit */
        g->flux = flux;
        g->ppix = ppix;
        g->back = back;
        g->fwhm = fwhm;
        if (qltool->guiding < 0) graph_scale(g->g_tc,0,1.333*flux,0x00);
        graph_add1(g->g_tc,flux,0); 
        graph_add1(g->g_fw,fwhm,0);
        graph_add1(g->g_az,g->dx*g->px,0); /* graph shows [arcsec] */
        graph_add1(g->g_el,g->dy*g->px,0);
        ddy = (qltool->guiding < 0) ? 0.0 : g->dy-ody; /* derivative */
        ody = g->dy;                     /* old 'dy' [pixels] */
        dy = g->dy + (server->rolling+1.0)*ddy; /* [pixels] */
        ddx = (qltool->guiding < 0) ? 0.0 : g->dx-odx; /* derivative */
        odx = g->dx;                     /* old 'dx' [pixels] */
        dx = g->dx + (server->rolling+1.0)*ddx; /* [pixels] */
        qltool->guiding = abs(qltool->guiding);  
        if ((fabs(dx*g->px) > 0.05) || (fabs(dy*g->px) > 0.05)) { /* [arcsec] */
          // printf("dx=%.2f dy=%.2f\n",dx,dy); 
          rotate(dx*g->px,dy*g->px,g->pa,g->parity,&azerr,&elerr);
          // printf("az=%.2f el=%.2f\n",azerr,elerr); 
          // ?Povilas eds_send801(g->gnum,fwhm,qltool->guiding,g->dx,g->dy,flux);
          double fdge = (g->q_flag == 0) ? 0.35 : 0.2;
          g->azg = fdge * g->sens * azerr;
          g->elg = fdge * g->sens * elerr;
          if ((qltool->guiding == 3) || (qltool->guiding == 5)) { int err=0;
            if (!tcsOpen) err = tcs_recon(g);
            if (!err) {  
              if (g->gmpar == 'p') telio_gpaer(3,-g->azg,-g->elg); 
              err = telio_aeg(g->azg,g->elg);
              if (err) tcs_error(g,err);
            }
          } /* endif(guiding) */
        } /* > 0.05 */
      } /* q_flag */
      g->update_flag = True;           /* update GUI */
      pthread_mutex_unlock(&g->mutex);
    } // endif(frame)
  } // endwhile(loop-doing && guiding)
}

/* ---------------------------------------------------------------- */

static void tcs_error(Guider *g,int err)  /* v0417 */
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%d)\n",__func__,g,err);
#endif

  if (err) { char buf[128]; 
    sprintf(buf,"sending correction to TCSIS failed, err=%d",err);
    fprintf(stderr,"%s\n",buf);
    tdebug(__func__,buf);
    message(g,buf,MSS_ERROR);
    telio_close(); tcsOpen=0;
    msleep(350); 
  }
}

/* --- */

static int tcs_recon(Guider *g)        /* v0417 */
{
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",__func__,g);
  tdebug(__func__,g);
#endif

  int err = telio_open(2);
  if (err) {
    sprintf(buf,"re-connecting failed, err=%d",err);
  } else {
    sprintf(buf,"re-connecting successful");
    tcsOpen = 1;
  }
  fprintf(stderr,"%s\n",buf);
  message(g,buf,(err) ? MSS_ERROR : MSS_INFO);
  if (err) msleep(3000);
  return err;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

