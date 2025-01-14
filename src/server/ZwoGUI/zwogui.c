/* ----------------------------------------------------------------
 *
 * zwogui.c  
 * 
 * Project: ZWO/ASI camera software (OCIW, Pasadena, CA)
 *
 * v0.001  2019-01-07  Christoph C. Birk, birk@carnegiescience.edu
 *
 * todo loop time (independent of exptime) -- take exposures 
 *
 * ---------------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           1              /* debug level */
#endif

#define TIME_TEST       1

/* ------------------------------------------------------- */

#define _REENTRANT

#include <stdlib.h>                    /* atoi(),exit() */
#include <stdio.h>                     /* sprintf() */
#include <string.h>                    /* strcpy(),memset(),memcpy() */
#include <math.h>                      /* sqrt() */
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#if (TIME_TEST > 0)
#include <limits.h> 
#include <sys/times.h>
#include <unistd.h>
#endif

#include <cxt.h>                       /* Xlib stuff */

#include "zwo.h"
#include "zwotcp.h"
#include "zwogui.h"                    /* qltool.h,fits.h */

#include "tcpip.h"                     /* TCPIP stuff */
#include "utils.h"                     /* generic utilities */
#include "ptlib.h"                     /* POSIX threads lib */
#include "random.h"

/* DEFINEs -------------------------------------------------------- */

#define TEST_GUIDER 

#define P_TITLE         "ZWO/ASI"

#define MY_HELP         "zwo.html" // TODO 

#define DBE_DATAPATH    "datapath"
#define FMT_RUN         "run%d"
#define DBE_PREFIX      "prefix"    
#define DEF_PREFIX      "zwo"

#define DEF_HELP "http://instrumentation.obs.carnegiescience.edu/Software/ZWO"
#if defined(MACOSX)
#define DEF_BROWSER     "open"         /* Safari */
#else
#define DEF_BROWSER     "firefox"
#endif

#define DEF_FONT        "lucidatypewriter"

#define TCPIP_PORT      (50001+(100*PROJECT_ID))

#define PREFUN          __func__

#define MAX_FILENUM   9999


/* EXTERNs -------------------------------------------------------- */

#ifdef LINUX
extern void swab(const void *from, void *to, ssize_t n);
#endif

/* X-DEFINEs ------------------------------------------------------ */

#ifdef FONTSIZE
int PXh=FONTSIZE;
#else
#ifdef MACOSX
int PXh=12;
#else
int PXh=14;
#endif
#endif
#define PXw         (10+(PXh-14)/2)
#define XXh         (PXh+4)
 
enum file_enum {
            FILE_HELP,
            FILE_D1,
            FILE_EXIT,
            FILE_N,
};
 
enum options_enum {
            OPT_DATAPATH,
            OPT_LOGFILE,
            OPT_N
};

enum guider_enum {
            GDR_FLIP_X,
            GDR_FLIP_Y,
            GDR_D1,
            GDR_HOST,
            GDR_D2,
            GDR_RESET,
            GDR_N
};
 
/* --- */

int   gWIDE=588;
#define gHIGH       (eHIGH-XXh-9)
#define eWIDE       (6+(n_guis*(2+gWIDE)))
#ifdef MACOSX
#define eHIGH       810
#else
#define eHIGH       800               /* todo */
#endif

#define MAX_NGUIS   3

/* TYPEDEFs ------------------------------------------------------- */

typedef struct gcam_header_tag {
  int  dimx,dimy,seqn,flag;
  char reserved[24];
} Header;

/* GLOBALs -------------------------------------------------------- */

Application *app;
char        logfile[512];

/* function prototype(s) */


/* STATICs -------------------------------------------------------- */

volatile Bool     done=True;

static MainWindow mwin;                /* main window */
static char       fontname[128];
static int        n_msg=0;
static float      throttle=5.0f;

static Menu       fimenu;              /* file dropdown menu */ 
static Menu       opmenu;              /* options dropdown menu */
static EditWindow utbox,rtbox;

static ZwoGUI *guis[MAX_NGUIS];
static const int n_guis=1;

static time_t next_disk=0;
static time_t startup_time;

static int   baseW=4656,baseH=3520,baseB=1,baseI=4656/8;
static float pscale=0.023f;

#ifdef SIM_ONLY
static const Bool camstat=False;
#else
static const Bool camstat=True;
#endif

static char    setup_rc[512];          /* dot-file in $HOME */

static ZwoGUI  *focusGui=NULL;

static pthread_mutex_t mesgMutex;

static char    zwohost[128]="localhost";
static int     zwoport=SERVER_PORT,zwo_id=0;

/* function prototype(s) ------------------------------------------ */

static void    redraw_text       (void);
static void    redraw_gwin       (ZwoGUI*);

static void    update_ut         (void);

static void    cb_file           (void*);
static void    cb_options        (void*);

static int     set_exptime       (ZwoGUI*,float);

static int     do_start          (ZwoGUI*,int);
static int     do_stop           (ZwoGUI*,int);

static void    cb_guider         (void*);

static void    handle_command    (ZwoGUI*,const char*);
static int     handle_key        (void*,XEvent*); 

static void*   run_housekeeping  (void*);
static void*   run_wait          (void*);
static void*   run_init          (void*);
static void*   run_setup         (void*);
static void*   run_cycle         (void*);
static void*   run_display       (void*);
static void*   run_write         (void*);
static void*   run_tcpip         (void*);

static int     check_datapath    (char*,int);
static void    my_shutdown       (Bool);

/* ---------------------------------------------------------------- */
 
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

/* ---------------------------------------------------------------- */

#if 0 // unused
static void rotate(double xi,double yi,double pa,double* az,double* el)
{
  double radpa = fabs(pa) / RADS;
  double sinpa = sin(radpa);
  double cospa = cos(radpa);
  assert(radpa != 0);
  if (pa < 0) {                        /* rotate into AZ,EL coodinates */
    *az = +(xi * cospa) -(yi * sinpa);
    *el = -(xi * sinpa) -(yi * cospa);
  } else {
    *az = -(xi * cospa) -(yi * sinpa);
    *el = +(xi * sinpa) -(yi * cospa);
  }
}
#endif

/* ---------------------------------------------------------------- */

/* --- M A I N ---------------------------------------------------- */

int main(int argc,char **argv)
{
  int        i,j;
  int        sleeptime=0,winpos=CBX_TOPLEFT;
  int        x,y,w,d;
  char       buffer[1024],buf[128];
  char       xserver[256];
  XEvent     event;
  XSizeHints hint;

  /* here comes the code ------------------------------------------ */ 

  startup_time = cor_time(0);

  strcpy(xserver,genv2("DISPLAY",""));
  strcpy(fontname,genv2("ZWOFONT",DEF_FONT)); 
  mwin.disp = NULL;
#ifdef MACOSX
  winpos = CBX_CalcPosition(45,0);
  baseI = 256;
#endif

  pthread_mutex_init(&mesgMutex,NULL);

  { extern char *optarg; char *p;      /* parse command line */  
    extern int opterr,optopt; opterr=0;
    while ((i=getopt(argc,argv,"f:h:i:g:")) != EOF) {
      switch (i) {
      case 'f':                        /* fontname */
        strcpy(fontname,optarg);
        break;
      case 'h':                        /* explicit host */
        strcpy(buffer,optarg);
        p = strchr(buffer,':'); if (p) *p = '\0';
        strcpy(zwohost,buffer);
        zwoport = (p) ? atoi(p+1) : SERVER_PORT;
        break; 
      case 'i':                        /* identity = serial */
        zwo_id = atoi(optarg);
        break;
      case 'g':                        /* geometry */
        { int a=0,b=0,c=0;
          if (sscanf(optarg,"%d %d %d",&a,&b,&c) == 3) {
            if (a > 0) baseW = a;
            if (b > 0) baseH = b;
            if (c > 0) baseB = c;
          }
        }
        break;
      case '?':
        fprintf(stderr,"%s: option '-%c' unknown or parameter missing\n",
                P_TITLE,(char)optopt);
        break;
      }
    } /* endwhile(getopt) */
  }

  InitRandom(0,0,0);                   /* QlTool uses Random */

  /* -------------------------------------------------------------- */

  /* initialize X-stuff ------------------------------------------- */

  Status s = XInitThreads();
  assert(s);

  if ((app=CBX_InitXlib(xserver)) == NULL) {  /* connect to 'xserver' */
    fprintf(stderr,"%s: CBX_InitXlib(%s) failed\n",P_TITLE,xserver);
    exit(4);
  }
 
  /* get setup from files */

  log_path(logfile,"ZWOLOG","zwogui"); /* create logfile name */
  lnk_logfile(logfile,"zwogui.log");     /* link to $HOME */
  sprintf(buf,"ZwoGUI-v%s",P_VERSION);
  message(NULL,buf,MSG_FILE);

  (void)set_path(setup_rc,"zwoguirc");   /* $HOME */

  get_string(setup_rc,DBE_DATAPATH,buffer,genv2("HOME","/tmp"));
  i = check_datapath(buffer,0);
  if (!i) strcpy(buffer,genv2("HOME","/tmp"));
  // printf("path= %s\n",buffer);

  for (i=0; i<n_guis; i++) {
    guis[i] = (ZwoGUI*)malloc(sizeof(ZwoGUI)); 
    ZwoGUI *g = guis[i];
    sprintf(g->name,"Zwo"); // TODO add name from server
    if (zwoport) {               /* explicit host */
      g->ccd = zwo_create(zwohost,zwoport);
    } else {
      g->ccd = NULL;
    }
    g->loop_running = False; 
    g->stop_flag = False;
    g->init_flag = g->send_flag = g->write_flag = 0;
    pthread_mutex_init(&g->mutex,NULL);
    g->fps = 0;
    g->px = pscale*baseB;
    strcpy(g->send_host,""); // TODO 
    g->send_port = 0;
    FITSpars *st = &g->status;
    sprintf(buf,FMT_RUN,0);
    st->run = (int)get_long(setup_rc,buf,1);
    st->seqNumber = 0;
    get_string(setup_rc,DBE_PREFIX,buf,DEF_PREFIX);
    sprintf(st->prefix,"%s",buf);
    strcpy(st->datapath,buffer); 
    strcpy(st->origin,"LCO/OCIW");
    strcpy(st->version,P_VERSION);
    strcpy(st->instrument,"ZWO ASI1600MM Pro"); // todo from server
    st->dimx = baseW;
    st->dimy = baseH;
    st->binning = baseB;
    st->pixscale = pscale;
    st->exptime = 0.5f;
  }

  /* create main-window ------------------------------------------- */
 
  sprintf(buf,"%s (v%s)",guis[0]->status.instrument,P_VERSION);
  CXT_OpenMainWindow(&mwin,winpos,eWIDE,eHIGH,&hint,buf,P_TITLE,True);
  // XSynchronize(mwin.disp,True);
  CBX_SelectInput(&mwin,ExposureMask | KeyPressMask | EnterWindowMask);
  (void)CBX_CreateGfC(&mwin,fontname,"bold",PXh);

  (void)CBX_CreateAutoDrop(&mwin,&fimenu,2,2,5*PXw,XXh,"File",cb_file);
    (void)CBX_AddMenuEntry(&fimenu,"Help",    0);
    (void)CBX_AddMenuEntry(&fimenu,NULL,      CBX_SEPARATOR);
    (void)CBX_AddMenuEntry(&fimenu,"Exit",    0);
 
  (void)CBX_CreateAutoDrop(&mwin,&opmenu,fimenu.x+fimenu.w+PXw/2,fimenu.y,
                           8*PXw,XXh,"Options",cb_options);
    (void)CBX_AddMenuEntry(&opmenu,"DataPath",0); // OPT_DATAPATH
    (void)CBX_AddMenuEntry(&opmenu,"Logfile",0);  // OPT_LOGFILE

  w = 11*PXw;
  x = eWIDE - w - PXw/2;
  y = fimenu.y;
  CBX_CreateAutoOutput(&mwin,&utbox,x,y,w,XXh,"UT");
  x -= w + PXw;
  CBX_CreateAutoOutput(&mwin,&rtbox,x,y,w,XXh,"et");

  for (i=0; i<n_guis; i++) {           /* guider controls */
    ZwoGUI *g = guis[i];
    g->win = CBX_CreateSimpleWindow(&mwin,2+i*(2+gWIDE),2+XXh+3,
                                    gWIDE,gHIGH,app->grey);
    CBX_SelectInput_Ext(mwin.disp,g->win,ExposureMask | KeyPressMask | 
                        EnterWindowMask );

    /* dropdown menu ---------------------------------------------- */
    CBX_CreateAutoDrop(&mwin,&g->gdmenu,opmenu.x+(1+i)*(opmenu.w+PXw/2),
                       fimenu.y,8*PXw,XXh,g->name,cb_guider);
      CBX_AddMenuEntry(&g->gdmenu,"Flip-X",0);   // GDR_FLIP_X
      CBX_AddMenuEntry(&g->gdmenu,"Flip-Y",0);   // GDR_FLIP_Y
      CBX_AddMenuEntry(&g->gdmenu,NULL,CBX_SEPARATOR);
      CBX_AddMenuEntry(&g->gdmenu,"SendHost",0); // GDR_HOST
      CBX_AddMenuEntry(&g->gdmenu,NULL,CBX_SEPARATOR);
      CBX_AddMenuEntry(&g->gdmenu,"Reset",0);    // GDR_RESET
    /* quicklook tool --------------------------------------------- */
    x = 1;
    y = 1;
    g->qltool = qltool_create(&mwin,g->win,fontname,x,y,
                              g->status.dimx,g->status.dimy,baseI);
    sprintf(g->qltool->name,"QlTool%d",1+i);
    /* 1st column ------------------------------------------------- */
    Window p = g->win;
    x += g->qltool->lWIDE + PXw/2;     
    y  = 2;
    w  = 8*PXw;
    y += 3+XXh+PXh/3;                  /* Note: 3+XXh in qltool.c */
    CBX_CreateAutoOutput_Ext(&mwin,&g->avbox,p,x,y,w,XXh,"av    0");
    y += 3+XXh+PXh/3;
    y += 3+XXh+PXh/3;
    y += 3+XXh+PXh/3;
    y += 3+XXh+PXh/3;
    /* 2nd column ------------------------------------------------- */
    x += 10*PXw + PXw/2;           
    y  = 2;
    w  = 8*PXw;
    y += 3+XXh+PXh/3;                  /* Note: 3+XXh in qltool.c */
    y += 3+XXh+PXh/3; 
    y += 3+XXh+PXh/3;
    y += 3+XXh+PXh/3;
    //xxx d  = 3*PXw;
    y += 3+XXh+PXh/3;
    /* 3rd column ------------------------------------------------- */
    x += 8*PXw + 3*PXw;
    y  = 2;
    w  = 8*PXw;
    y += 3+XXh+PXh/3;                  /* Note: 3+XXh in qltool.c */
    y += 3+XXh+PXh/3;
    y += 3+XXh+PXh/3;
    /* 4th column ------------------------------------------------- */
    w  = 15*PXw;
    x = gWIDE - w - PXw/2;             
    y = 2;
    CBX_CreateAutoOutput_Ext(&mwin,&g->csbox,p,x,y,w,XXh,"cursorStep=1.0");
    /* exposure controls ------------------------------------------ */
    y = g->qltool->iy + g->qltool->iHIGH + 4;
    sprintf(buf,"%.2f",g->status.exptime);
    CBX_CreateAutoOutput_Ext(&mwin,&g->tfbox,g->win,1+2*PXw,y,5*PXw,XXh,buf);
    CBX_CreateAutoOutput_Ext(&mwin,&g->dtbox,g->win,
                             g->tfbox.x+g->tfbox.w+3*PXw/2, y,3*PXw,XXh,"0");
    CBX_CreateAutoOutput_Ext(&mwin,&g->sqbox,g->win,
                             g->dtbox.x+g->dtbox.w+3*PXw/2, y,7*PXw+2,XXh,"0");
    CBX_CreateAutoOutput_Ext(&mwin,&g->fpbox,g->win,
                             g->sqbox.x+g->sqbox.w+4*PXw,   y,5*PXw,XXh,"0");
    CBX_CreateAutoOutput_Ext(&mwin,&g->fdbox,g->win,
                             g->fpbox.x+g->fpbox.w+3*PXw/2, y,5*PXw,XXh,"0");
    w = 4*PXw;
    x = gWIDE - w - PXw/2;
    CBX_CreateAutoOutput_Ext(&mwin,&g->pcbox,g->win,x,y,w,XXh,"");
    w = 5*PXw;
    x -= w + PXw;
    CBX_CreateAutoOutput_Ext(&mwin,&g->tmbox,g->win,x,y,w,XXh,"");
    y += XXh+PXh/3;
    w  = g->fpbox.x+g->fpbox.w-1;
    CBX_CreateAutoOutput_Ext(&mwin,&g->cmbox,g->win,1,y,w,XXh,"_");
    x  = 1;
    d  = PXw+6;
    w  = gWIDE -x -5;
    y += XXh+PXh/3;
    n_msg = (gHIGH-y-3)/XXh;
    g->msbox = (EditWindow*)malloc(n_msg*sizeof(EditWindow));
    g->led   = (Led*)malloc(n_msg*sizeof(Led));
    for (j=0; j<n_msg; j++) {
      CBX_CreateAutoOutput_Ext(&mwin,&g->msbox[j],g->win,x+d,y,w-d,XXh,NULL);
      CBX_CreateLed_Ext(&mwin,&g->led[j],g->win,x+2,y+4,PXw,PXw,app->lgrey); 
      y += XXh;
    }
  } /* endfor(n_guis) */
  done = False;

  message(NULL,guis[0]->status.instrument,MSG_FILE);

  /* -------------------------------------------------------------- */

  for (i=0; i<n_guis; i++) {        /* setup GUI */
    redraw_gwin(guis[i]);
    // CBX_SendExpose_Ext(mwin.disp,guis[i]->win);
    if (guis[i]->ccd) thread_detach(run_init,guis[i]);
  }

  /* this loop drives the program --------------------------------- */

  while (!done) {                      /* main loop */
    while (!CBX_AutoEvent(&mwin,&event,&sleeptime)) { /* no event */
      CBX_Sleep(&sleeptime,5,350);
      update_ut();
      if (done) break;                 /* break main loop */
    }
    if (done) break;                   /* break main loop */
#if (DEBUG > 3)
    fprintf(stderr,"%s: event.type=%d\n",__FILE__,event.type);
#endif
    switch(event.type) {
    case Expose:                       /* main window exposed  */
      if (event.xexpose.count == 0) {  /* if last event in queue */
        if (event.xexpose.window == mwin.win) {
          redraw_text();               /* redraw everything */
          break;
        }
        for (i=0; i<n_guis; i++) {
          ZwoGUI *g = guis[i];
          if (event.xexpose.window == g->win) { 
            redraw_gwin(g);
            break; 
          }
          if (qltool_event(g->qltool,&event)) break;
        }
      }
      break;
    case MapNotify:                    /* window mapped */
    case UnmapNotify:                  /* window unmapped */
#if (DEBUG > 0)
      printf("%s: Un/Map Notify\n",__FILE__);
#endif
      break;
    case EnterNotify:                  /* enter window */
      if (event.xany.window == mwin.win) {
        if (focusGui) {
          //CBX_WindowBorder_Ext(mwin.disp,focusGui->win,app->black);
          CBX_WindowBorder_Ext(mwin.disp,focusGui->cmbox.win,app->black);
          focusGui = NULL; 
        }
      } else {
        for (i=0; i<n_guis; i++) {
          ZwoGUI *g = guis[i];
          if (event.xany.window == g->win) {
            if (g != focusGui) {
             if (focusGui) {        /* green border ?Shec */
              // CBX_WindowBorder_Ext(mwin.disp,focusGui->win,app->black);
              CBX_WindowBorder_Ext(mwin.disp,focusGui->cmbox.win,app->black);
             }
             focusGui = g;
             // CBX_WindowBorder_Ext(mwin.disp,g->win,app->green);
             CBX_WindowBorder_Ext(mwin.disp,g->cmbox.win,app->green);
            }
            break;
          }
        }
      }
      break;
    case DestroyNotify:                /* WM closed window */
#if (DEBUG > 0)
      printf("%s: DestroyNotify on window=%d\n",__FILE__,
             (int)event.xdestroywindow.window);
#endif
      break;
    case MotionNotify:                 /* Mouse */
#if (DEBUG > 0)
      printf("%s: Motion Notify\n",__FILE__);
#endif
      /* ignore */
      break;
    case ReparentNotify:               /* MacOS */
#if (DEBUG > 0)
      printf("%s: Reparent Notify\n",__FILE__);
#endif
      /* ignore */
      break;
    case ConfigureNotify:              /* window moved */
#if (DEBUG > 0)
      printf("%s: Configure Notify\n",__FILE__);
#endif
      /* ignore */
      break;
    case KeyPress:                     /* keyboard events */
      if (event.xkey.window == mwin.win) {
#ifdef SIM_ONLY                        /*  */
        if (CBX_GetKey(&event) == 'Q') {
          my_shutdown(True);
        }
#endif
      } else {
        for (i=0; i<n_guis; i++) {
          ZwoGUI *g = guis[i];
          if (event.xkey.window == g->win) {
            if (qltool_handle_key(g->qltool,(XKeyEvent*)&event)) break;
            if (handle_key(g,&event)) break;
          } 
        }
      }
      break;
    case ButtonPress:                  /* mouse button pressed */
      if (event.xbutton.button == 1) { /* left button */
        for (i=0; i<n_guis; i++) {
          ZwoGUI *g = guis[i];
          if (g->init_flag != 1) continue;
          if (event.xbutton.window == g->qltool->iwin) {
            // TODO
            break;
          }
        } /* endfor(guis) */
      } /* endif(left button) */
      break;
#if (DEBUG > 0)
    default:
      fprintf(stderr,"unknown event '%d'\n",event.type);
      break;
#endif
    } /* endswitch(event.type) */
  } /* while(!done) */                 /* end of main-loop */

  /* free up all the resources and exit --------------------------- */

#if 0 // TODO
  if (info.port != 0) {                /* TCP/IP enabled */
    (void)TCPIP_TerminateServerThread(&info,1000);
  }
#endif

  CBX_CloseMainWindow(&mwin);

  for (i=0; i<n_guis; i++) {
    ZwoGUI *g = guis[i];
    if (g->ccd) zwo_free(g->ccd);
  }

#if (TIME_TEST > 0)                    /* print cpu-time */
  { struct tms tmsbuf;
    (void)times(&tmsbuf);
    printf("%s(%d): total-cpu: %5.2f seconds\n",P_TITLE,(int)getpid(),
      (float)(tmsbuf.tms_stime)/(float)sysconf(_SC_CLK_TCK) +
      (float)(tmsbuf.tms_utime)/(float)sysconf(_SC_CLK_TCK));
  }
#endif
 
  CXT_CloseLib(True);
 
  return 0;
}

/* ---------------------------------------------------------------- */

static void redraw_gwin(ZwoGUI *g)
{
  int     j,x,y;
  char    buf[128];
  Display *disp=mwin.disp;
  Window  win=g->win;
  GC      gc=mwin.gfs.gc;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s)\n",PREFUN,g->name);
#endif

  // CBX_Lock(0);
  // todo ? XClearWindow(mwin.disp,g->win);
  // CBX_Unlock();

  (void)CBX_Lock(0);
  XDrawString(disp,win,gc,g->tfbox.x-2*PXw,g->tfbox.y+PXh,"tf",2);
  XDrawString(disp,win,gc,g->fpbox.x-3*PXw,g->fpbox.y+PXh,"FpS",3);
  XDrawString(disp,win,gc,g->tmbox.x-4*PXw,g->tmbox.y+PXh,"Temp",4);
  for (j=0; j<QLT_NCURSORS; j++) {
    if (g->qltool->cursor_mode == j) XSetForeground(disp,gc,app->green);
    x = g->qltool->cubox[j].x-PXw;
    y = g->qltool->cubox[j].y+PXh;
    sprintf(buf,"%d",1+j);
    XDrawString(disp,win,gc,x,y,buf,strlen(buf));
    if (g->qltool->cursor_mode == j) XSetForeground(disp,gc,app->black);
  }

  CBX_Unlock();
  sprintf(g->csbox.text,"cursorStep=%.1f",g->qltool->cursor_step);
  CBX_UpdateEditWindow(&g->csbox);
}

/* --- */

static void redraw_text(void)
{
#if 0 // not used
  Display *disp=mwin.disp;
  Window  win=mwin.win;
  GC      gc=mwin.gfs.gc;
#endif
#if (DEBUG > 2)
  fprintf(stderr,"%s: %s()\n",__FILE__,PREFUN);
#endif

  (void)CBX_Lock(0);

  CBX_Unlock();
}

/* ---------------------------------------------------------------- */

static void update_ut(void)
{
  char buf[128];
  static time_t ut_now=0,et_next=0;
#if (DEBUG > 2)
  fprintf(stderr,"%s()\n",PREFUN);
#endif

  if (cor_time(0) == ut_now) return;   /* current time IDEA TCS offtime */
  ut_now = cor_time(0);                /* IDEA depends on time server */

  sprintf(utbox.text,"UT %s",get_ut_timestr(buf,ut_now));
  CBX_UpdateEditWindow(&utbox); 

  if (!camstat) { ZwoGUI *g=guis[0]; /* Simulator */
    if ((ut_now % 2) == 0) { char title[128];
      if ((ut_now % 4) == 0) {         /* blink title */
        sprintf(title,"%s (v%s)",g->status.instrument,P_VERSION);
      } else {
        sprintf(title,"%s - Simulator",g->status.instrument);
#if (__SIZEOF_POINTER__ == 8)          /* __x86_64__ , __LP64__ */
        strcat(title,"/64");
#endif
      }
      CBX_SetMainWindowName(&mwin,title);
    }
  }

  if (ut_now >= et_next) {             /* update runtime */
    double runtime = (double)(ut_now - startup_time);
    strcpy(buf,"s"); et_next = ut_now+1;
    if (runtime > 120) {               /* 120 seconds */
      runtime /= 60.0; strcpy(buf,"m"); et_next += 5; /* 0.1 mins */
      if (runtime > 100)  {            /* 100 minutes */
        runtime /= 60.0; strcpy(buf,"h"); et_next += 354; /* 0.1 hrs */
        if (runtime > 96)  { runtime /= 24.0; strcpy(buf,"d"); }
      }
    }
    sprintf(rtbox.text,"et %.1f%s",runtime,buf);
    CBX_UpdateEditWindow(&rtbox); 
    // IDEA return to execute only one thing each time
  }

  if (ut_now >= next_disk) { // IDEA thread_detach
    ZwoGUI *g = guis[0];
    float file_system = checkfs(g->status.datapath); 
    if (file_system < 0.05) { char buf[128]; 
      sprintf(buf,"WARNING: disk %.1f%% full",100.0*(1.0-file_system));
      message(g,buf,MSG_WARN);
    }
    next_disk = ut_now+600;
  }
}

/* ---------------------------------------------------------------- */
 
static void cb_file(void* param)
{
  int i;
 
  switch (fimenu.sel) {  
  case FILE_HELP:
    { char cmd[512]; AMB *box;
      box = CBX_AsyncMessageOpen(P_TITLE ": startup browser",DEF_HELP);
      sprintf(cmd,"%s %s/%s &",genv2("BROWSER",DEF_BROWSER),DEF_HELP,MY_HELP);
      system(cmd);
      sleep(3);
      CBX_AsyncMessageClose(box);
    }
    break;
  case FILE_EXIT:                      /* exit GUI */
    done = True;                       /* kill housekeeping loop */
    for (i=0; i<n_guis; i++) {
      if (guis[i]->loop_running) {
        if (!CBX_AlertBox("Exposure","Really exit?")) done = False;
        break;
      }
    }
    if (done) my_shutdown(True);
    break;
  }
  CBX_ClearAutoQueue(&mwin);
}

/* ---------------------------------------------------------------- */

static void cb_options(void* param)
{
  int  i;
  char buffer[1024];

  switch (opmenu.sel) {
  case OPT_LOGFILE:                    /* set logfile name */
    CBX_AsyncMessageBox(P_TITLE,logfile);
    break;
  case OPT_DATAPATH:                   /* set save-path (data) */
    sprintf(buffer,"%s",guis[0]->status.datapath);
    if (CBX_ParameterBox("Set Datapath",buffer)) {
      check_datapath(buffer,1);
      put_string(setup_rc,DBE_DATAPATH,buffer);
      for (i=0; i<n_guis; i++) {
        strcpy(guis[i]->status.datapath,buffer);
      }
    }
    break;
  }
  CBX_ClearAutoQueue(&mwin);
}

/* ---------------------------------------------------------------- */

static ZwoGUI* get_guider(void* param)
{
  int i;
  ZwoGUI *g=NULL;

  for (i=0; i<n_guis; i++) {
    g = guis[i];
    if (param == &g->gdmenu) break;
    g = NULL;
  }
  return g;
}

/* --- */

static void cb_guider(void* param)
{
  Menu *m=(Menu*)param;
  ZwoGUI *g = get_guider(param);
  char buffer[1024],buf[256];
  assert(g);

  switch (m->sel) {
  case GDR_FLIP_X:
    g->qltool->flip_x = 1-g->qltool->flip_x;
    g->gdmenu.entry[GDR_FLIP_X].flag = (g->qltool->flip_x) ? CBX_CHECKED : 0;
    qltool_redraw(g->qltool,False);
    break;
  case GDR_FLIP_Y:
    g->qltool->flip_y = 1-g->qltool->flip_y;
    g->gdmenu.entry[GDR_FLIP_Y].flag = (g->qltool->flip_y) ? CBX_CHECKED : 0;
    qltool_redraw(g->qltool,False);
    break;
  case GDR_HOST:                       /* v0066 */
    sprintf(buffer,"%s %d",g->send_host,g->send_port);
    if (CBX_ParameterBox("Send Host & Port",buffer)) { int port;
      if (sscanf(buffer,"%s %d",buf,&port) == 2) {
        strcpy(g->send_host,buf);
        g->send_port = port;
      }
    }
    break;
  case GDR_RESET:
    message(g,"not implemented",MSG_WARN);
    break;
  }
}

/* ---------------------------------------------------------------- */

static int do_start(ZwoGUI* g,int wait) 
{
  int weDidIt=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,g->name);
#endif

  if (g->init_flag != 1) return -E_NOTINIT;

  if (!g->loop_running) { 
    message(g,"starting acquisition",MSG_INFO);
    thread_detach(run_cycle,g);
    while (wait > 0) {
      if (!g->loop_running) msleep(350);
      wait -= 350;
    }
    weDidIt = 1;
  }

  return weDidIt;
}

/* ---------------------------------------------------------------------- */

static int do_stop(ZwoGUI* g,int wait) 
{
  int weDidIt=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,g->name);
#endif

  if (g->loop_running) {
    g->stop_flag = True;
    message(g,"stopping acquisition",MSG_INFO);
    while (wait > 0) {
      if (g->loop_running) msleep(350);
      wait -= 350;
    }
    weDidIt = 1;
  }
  return weDidIt;
}

/* ---------------------------------------------------------------------- */

static int handle_key(void* param,XEvent* event) 
{
  ZwoGUI *g=(ZwoGUI*)param;
  int r=1,i;
  static const float steps[]={0,0.1f,1.0f,4.0f,40.0f,0};
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p)\n",PREFUN,param,event);
#endif

  CBX_Lock(0);
  char *keys = XKeysymToString(XLookupKeysym((XKeyEvent*)event,0));
  //printf("%s: keys=%s\n",PREFUN,keys);
  CBX_Unlock();

  if (!strcmp(keys,"F1")) {            /* stop guiding "F1" */
    handle_command(g,"fone");
  } else
  if (!strcmp(keys,"F2")) {            /* calculate here */
    handle_command(g,"ftwo");
  } else
  if (!strcmp(keys,"F3")) {            /* guide here */
    handle_command(g,"fthr");
  } else 
  if (!strcmp(keys,"F4")) {            /* center -- calculate  */
    handle_command(g,"ffou");
  } else
  if (!strcmp(keys,"F5")) {            /* center -- guide */
    handle_command(g,"ffiv");
  } else 
  if (!strcmp(keys,"F9")) {
    handle_command(g,"fnin");
  } else 
  if (!strcmp(keys,"F10")) {
    handle_command(g,"ften");
  } else 
  if (!strcmp(keys,"Insert") || !strcmp(keys,"Delete") ||
      !strcmp(keys,"Prior") || !strcmp(keys,"Next")) { /* cursor step */
    for (i=0; i<(sizeof(steps)/sizeof(float)); i++) {
      if (g->qltool->cursor_step == steps[i]) break;
    }
    if (!strcmp(keys,"Insert") || !strcmp(keys,"Prior")) {
      if (steps[i+1] != 0) g->qltool->cursor_step = steps[i+1];
    } else {
      if (steps[i-1] != 0) g->qltool->cursor_step = steps[i-1];
    }
    redraw_gwin(g);
  } else {
    // printf("%s(): keys=%s\n",PREFUN,keys);
    r = 0;
  }
  if (r == 0) {                        /* not a special key */
    if (event->xkey.state & ControlMask) { /* Control key is pressed */
      if (!strcmp(keys,"1") || !strcmp(keys,"2") || !strcmp(keys,"3") ||
          !strcmp(keys,"4") || !strcmp(keys,"5")) {
        g->qltool->cursor_mode = atoi(keys)-1;
        redraw_gwin(g);
        qltool_redraw(g->qltool,False);
      }
      r = 1;
    }
  }
  if (r == 0) {                        /* redirect to command edit */
    if (g->cmbox.cpos > 0) {           /* remove '_' */
      g->cmbox.cpos--; g->cmbox.text[g->cmbox.cpos] = '\0'; 
    }
    int flag = CBX_HandleEditWindow(&g->cmbox,(XKeyEvent*)event);
    if (flag) {                        /* <Enter> or <Return> */
      if (strlen(g->cmbox.text) > 0) {
        handle_command(g,g->cmbox.text);
        strcpy(g->cmbox.text,"");
        CBX_ResetEditWindow(&g->cmbox);
        // IDEA g->cmbox.cpos = 1; strcpy(g->cmbox.text,"_");
      }
    } else {
      g->cmbox.cpos += 1; strcat(g->cmbox.text,"_");
      CBX_UpdateEditWindow(&g->cmbox);
    }
  }

  return r;
}

/* ---------------------------------------------------------------- */

static void handle_command(ZwoGUI* g,const char* command)
{
  int  err=0,n=0;
  char cmd[128]="",par1[128]="",par2[128]="",par3[128]="",msg[256]="";
  char buf[256];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%s)\n",PREFUN,g->name,command);
#endif

  //sprintf(buf,"%s: %s",PREFUN,command); message(g,buf,MSG_FLUSH);

  if (strlen(command) < 32) {          /* auto-add missing space-char */
    char *p1=(char*)command,*p2=buf;
    do {
      if (isupper(*p1) != islower(*p1)) {
        *p2 = *p1; p1++; p2++;
      } else {
        if (*p1 != ' ') { *p2 = ' '; p2++; }
        strcpy(p2,p1); 
        break;
      }
    } while (*p1);
    *p2 = *p1;
    message(g,buf,MSG_WINDOW);
    n = sscanf(buf,"%s %s %s %s",cmd,par1,par2,par3);
  }

  if (!strncasecmp(cmd,"av",2)) {      /* AVF,AVG */
    if (n >= 2) {
#if 1 // rolling at GUI
      g->ccd->rolling = imax(0,imin(99,atoi(par1)));
#else // rolling at server TODO ?
      err = zwo_rolling(g->ccd,imax(0,imin(99,atoi(par1))));
#endif
    }
    sprintf(g->avbox.text,"av %4d",g->ccd->rolling);
    CBX_UpdateEditWindow(&g->avbox);
  } else 
  if (!strcasecmp(cmd,"close")) {      /* TESTING */
    if (g->loop_running) do_stop(g,5000);
    zwo_close(g->ccd);
    g->init_flag = 0;
  } else
  if (!strcasecmp(cmd,"exit")) {       /* exit program */
    my_shutdown(True);
  } else
  if (!strcasecmp(cmd,"fone")) {       /* F1 */
  } else
  if (!strcasecmp(cmd,"ftwo")) {       /* F2 */
  } else 
  if (!strcasecmp(cmd,"fthr")) {       /* F3 */
  } else
  if (!strcasecmp(cmd,"ffou")) {       /* F4 */
  } else
  if (!strcasecmp(cmd,"ffiv")) {       /* F5 */
  } else
  if (!strcasecmp(cmd,"gm") || !strcasecmp(cmd,"fm")) { 
    err = E_NOTIMP;
  } else
  if (!strcasecmp(cmd,"mc")) {         /* simulate mouse click ?Shec */
    err = E_NOTIMP;
  } else
  if (!strcasecmp(cmd,"send")) {       /* send frame via TCP/IP */
    if (g->init_flag == 1) {
      g->send_flag = (n > 1) ? atoi(par1) : -1;
    } else { err = E_ERROR; strcpy(msg,"ZWO/ASI not initialized"); }
  } else
  if (!strcasecmp(cmd,"sky") || !strcasecmp(cmd,"sub")) {
    err = E_NOTIMP;
  } else
  if (!strcasecmp(cmd,"span") || !strcasecmp(cmd,"zero") ||
      !strcasecmp(cmd,"pct")  || !strcasecmp(cmd,"bkg")) {
    qltool_scale(g->qltool,cmd,par1,par2);
  } else 
  if (!strcasecmp(cmd,"tc")) {         /* send a TCS command */
    err = E_NOTIMP;
    printf("send a TCS command %s %s %s\n",par1,par2,par3); // TODO 
  } else
  if (!strcasecmp(cmd,"tec")) {        /* cooler on/off todo add setpoint */
    int weStoppedTheLoop = do_stop(g,5000);
    err = zwo_tempcon(g->ccd,atoi(par1)); 
    if (weStoppedTheLoop) do_start(g,0);
  } else
  if (!strcasecmp(cmd,"tf") || !strcasecmp(cmd,"tg")) {
    if (n >= 2) set_exptime(g,atof(par1));
  } else
  if (!strcasecmp(cmd,"xy")) {         /* activate cursor 1-5 */
    int j = atoi(par1)-1;
    if ((j >= 0) && (j < QLT_NCURSORS)) { 
      g->qltool->cursor_mode = j;      /* make active */
    }
    redraw_gwin(g);
  } else 
  if (!strcasecmp(cmd,"xys")) {        /* set cursor todo simplify */
    if (n > 3) {
      int j = imax(0,imin(QLT_NCURSORS-1,atoi(par1)-1));
      g->qltool->curx[j] = (float)atof(par2);
      g->qltool->cury[j] = (float)atof(par3);
      g->qltool->cursor_mode = j;      /* make active */
    } else 
    if (n > 2) { 
      g->qltool->curx[g->qltool->cursor_mode] = (float)atof(par1);
      g->qltool->cury[g->qltool->cursor_mode] = (float)atof(par2);
    } else err = E_MISSPAR;
    if (!err) {
      redraw_gwin(g);
      qltool_redraw(g->qltool,False);
    }
  } else
  if (!strcasecmp(cmd,"xyr")) {        /* move cursor 1-5 */
    if (n > 3) {
      int j = imax(0,imin(QLT_NCURSORS-1,atoi(par1)-1));
      g->qltool->curx[j] += (float)atof(par2);
      g->qltool->cury[j] += (float)atof(par3);
      g->qltool->cursor_mode = j;      /* make active */
    } else 
    if (n > 2) { 
      g->qltool->curx[g->qltool->cursor_mode] += (float)atof(par1);
      g->qltool->cury[g->qltool->cursor_mode] += (float)atof(par2);
    } else err = E_MISSPAR;
    if (!err) {
      redraw_gwin(g);
      qltool_redraw(g->qltool,False);
    }
  } else
  /* --- extended commmand set ------------------------------------ */
  if (!strncasecmp(cmd,"flip",3)) {
    if (n > 2) err = zwo_flip(g->ccd,atoi(par1),atoi(par2));
    else       err = E_MISSPAR;
  } else
  if (!strncasecmp(cmd,"offset",3)) {
    if (n > 1) err = zwo_offset(g->ccd,atoi(par1));
  } else
  if (!strncasecmp(cmd,"gain",3)) {
    if (n > 1) err = zwo_gain(g->ccd,atoi(par1));
  } else
  if (!strncasecmp(cmd,"filter",3)) { 
    if (n > 1) err = zwo_filter(g->ccd,atoi(par1)); // todo wait ?
    else       err = E_MISSPAR;
  } else
  if (!strncasecmp(cmd,"lmag",3)) {    /* lupe magnification */
    qltool_lmag(g->qltool,atoi(par1));
    sprintf(msg,"lmag=%d",g->qltool->lmag);
  } else
  if (!strncasecmp(cmd,"lut",3)) {     /* color lookup table */
    if (n >= 2) qltool_lut(g->qltool,par1);
    else        err = E_MISSPAR;
  } else
  if (!strncasecmp(cmd,"scale",3)) {   /* scaling mode */
    qltool_scale(g->qltool,par1,par2,par3);
  } else
  if (!strncasecmp(cmd,"dt",2)) {      /* display throttle */
    if (*par1) throttle = fmax(0,atof(par1));
    else               sprintf(msg,"dt= %.0f",throttle);
  } else
  if (!strncasecmp(cmd,"smooth",2)) {  /* IDEA display on GUI */
    g->qltool->smoothing = abs(atoi(par1));
  } else 
  if (!strncasecmp(cmd,"start",4)) {   /* start exposure loop */
    int r = do_start(g,0);
    if (r < 0) err = -r;
  } else
  if (!strncasecmp(cmd,"stop",3)) {    /* stop exposure loop */
    do_stop(g,0);
  } else
  if (!strncasecmp(cmd,"write",3)) {   /* write files */
    g->write_flag = (n > 1) ? atoi(par1) : 1;
    thread_detach(run_write,g);
  } else                               /* reset server */
  if (!strcasecmp(cmd,"dspi") || !strcasecmp(cmd,"reset")) {
    if (g->init_flag >= 0) thread_detach(run_init,g);
#ifdef SIM_ONLY
  } else                               /* restart server */
  if (!strncasecmp(cmd,"restart",4)) {
    if (g->loop_running) do_stop(g,3000);
    err = zwo_server(g->ccd,cmd,buf);
    if (!err) thread_detach(run_wait,g);
#endif
  } else                               /* reboot Minnow */
  if (!strncasecmp(cmd,"reboot",4)) {
    if (g->loop_running) do_stop(g,3000);
    err = zwo_server(g->ccd,cmd,buf);
    if (!err) thread_detach(run_wait,g);
  } else                               /* shutdown Minnow */
  if (!strncasecmp(cmd,"shutdown",4) || !strncasecmp(cmd,"poweroff",5)) {
    if (g->loop_running) do_stop(g,3000);
    err = zwo_server(g->ccd,cmd,buf);
  } else {
    err = E_ERROR; sprintf(msg,"unknown command: %s",cmd);
  }
  switch (err) {
  case 0:
    if (*msg) message(g,msg,MSG_WINDOW);
    break;
  case E_ERROR:   
    message(g,msg,MSG_WARN);
    break;
  case E_NOTINIT:
    message(g,"ZWO/ASI not initialized",MSG_WARN);
    break; 
  case E_RUNNING:
    message(g,"data acquisition running",MSG_WARN);
    break; 
  case E_MISSPAR: 
    message(g,"missing parameter",MSG_WARN);
    break;
  case E_NOTACQ:  
    message(g,"ZWO/ASI not acquiring",MSG_WARN);
    break;
  case E_FILTER:  
    message(g,"filter wheel failed",MSG_WARN);
    break;
  case E_NOTIMP:  
    message(g,"command not implemented",MSG_WARN);
    break;
  default: 
    sprintf(buf,"unknown error code %d",err);
    message(g,buf,MSG_WARN);
    fprintf(stderr,"%s\n",buf);
    break;
  } 
}

/* ---------------------------------------------------------------- */

static void* run_setup(void* param)
{
  ZwoGUI *g = (ZwoGUI*)param;
  char   buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p): %s\n",PREFUN,param,g->name);
#endif
  assert(!done);                       /* GUI up and running */
  assert(g->ccd->handle >= 0);       /* connected */
  assert(g->ccd->tid == 0);

  g->init_flag = -1;                   /* init running */
  message(g,PREFUN,MSG_INFO);

  // strcpy(g->status.serial,g->ccd->serialNumber);
  // IDEA if (!err) CBX_SetMainWindowName(&mwin,g->status.serial);

  long err = zwo_setup(g->ccd,baseW,baseH,baseB);
  if (!err) {
    printf("%d %d %d\n",g->ccd->aoiW,baseW,baseH);
    assert(g->ccd->aoiW == baseW/baseB); // todo fails with mod8 requirement
    assert(g->ccd->aoiH == baseH/baseB);
    g->status.binning = baseB;
    g->status.exptime = g->ccd->expTime;
  }
  if (err) {                           /* IDEA just close GUI */
    g->init_flag = 0;                  /* there's no redemption :-( */
    sprintf(buf,"err= %ld",err);
    message(g,buf,MSG_ERROR);
    assert(g->ccd->err == err);
  } else {
    g->init_flag = 1;
    set_exptime(g,0);                  /* update GUI */
    do_start(g,0);
    thread_detach(run_housekeeping,(void*)g);
  }

  return (void*)err;
}

/* ---------------------------------------------------------------- */

// TODO simplify run_init/wait

static void* run_wait(void* param)
{
  int  i;
  long err=0;
  char buf[128],dots[32]=".";
  ZwoGUI *g = (ZwoGUI*)param;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p): %s\n",PREFUN,param,g->name);
#endif
  assert(!done);                       /* GUI up and running */
  assert(g->ccd->tid == 0);

  g->init_flag = 0;
  message(g,PREFUN,MSG_INFO);
  sleep(1);
  zwo_disconnect(g->ccd);
  message(g,dots,MSG_WINDOW);

  for (i=0; i<60; i++) {               /* minnow reboot 60-100 seconds */
    sleep(1); if ((i % 6) == 0) strcat(dots,"."); 
    message(g,dots,MSG_OVER);
  }
  for (i=0; i<30; i++) {               /* 60 seconds */
    sleep(2);
    err = zwo_connect(g->ccd);     /* just TCP/IP connection */
    if (!err) break;
    strcat(dots,"."); message(g,dots,MSG_OVER);
  }

  if (!err) {
    message(g,"connecting to ZWO/ASI ...",MSG_OVER);
    err = zwo_open(g->ccd);        /* actual USB connection */
  } else {
    message(g,"reboot timeout",MSG_ERROR);
  }
#if 0 // todo how to tell connection status ?
  if (!err) { 
    if (!strncmp(g->ccd->serialNumber,"-E",2)) {
      sprintf(buf,"'%s' not connected to ZWO/ASI",g->ccd->host);
      message(g,buf,MSG_ERROR);
      err = E_zwo_not_connected;
    }
  }
#endif
  if (!err) {
    err = (long)run_setup(g);
    if (err) {
      sprintf(buf,"err=%ld",err); message(g,buf,MSG_ERROR);
    }
  }
  g->init_flag = (err) ? 0 : 1;

  return (void*)err;
}

/* ---------------------------------------------------------------- */

static void* run_init(void* param)
{
  long err=0;
  char buf[512];
  ZwoGUI *g = (ZwoGUI*)param;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p): %s\n",PREFUN,param,g->name);
#endif
  assert(!done);                       /* GUI up and running */
 
  g->init_flag = -1;                   /* init running */
  message(g,PREFUN,MSG_INFO);

  assert(g->ccd);

  if (g->loop_running) {               /* wait for loop to finish */
    do_stop(g,5000); msleep(1000);
  }
  if (g->ccd->handle > 0) zwo_close(g->ccd);

  if (!err) {
    err = zwo_open(g->ccd);
    if (err && !strcmp(g->ccd->host,"localhost")) {
      sprintf(buf,"cannot connect to %s:%d",g->ccd->host,g->ccd->port);
      message(g,buf,MSG_WARN);
      int i = (g->ccd->port - SERVER_PORT);
      sprintf(buf,"nohup %s/zwoserver -i%d &",getenv("HOME"),i);
      message(g,buf,MSG_INFO);
      system(buf);
      sleep(2);
      err = zwo_open(g->ccd);
    }
    if (err) {
      sprintf(buf,"cannot connect to %s:%d",g->ccd->host,g->ccd->port);
      message(g,buf,MSG_ERROR);
    } else {
#if 0 // todo how to tell connection status 
      if (!strncmp(g->ccd->serialNumber,"-E",2)) {
        sprintf(buf,"'%s' not connected to ZWO/ASI",g->ccd->host);
        message(g,buf,MSG_ERROR);
        err = E_zwo_not_connected;
      }
#endif
    }
  }
  if (!err) {
    err = (long)run_setup(g);
    if (err) { char buf[128];
      sprintf(buf,"setup failed, err=%ld",err);
      message(g,buf,MSG_ERROR);
    }
  }
  if (!err) { 
    thread_detach(run_tcpip,(void*)g);
  }
  g->init_flag = (err) ? 0 : 1;

  return (void*)err;
}

/* ---------------------------------------------------------------- */

static void* run_housekeeping(void* param)
{
  ZwoGUI *g=(ZwoGUI*)param;
  int  slpt=2,err;
  char buf[128];

  do {
    sleep(1);
    if (g->init_flag != 1) break;      /* will be restarted at _setup */
    assert(g->ccd);
    if (g->ccd->handle < 0) break;     /* not connected */
    if (cor_time(0) % slpt) continue;
    err = zwo_temperature(g->ccd);
    if (!err) {
      g->status.temp_ccd = g->ccd->tempSensor;
      sprintf(g->tmbox.text,"%.1f",g->ccd->tempSensor); 
      CBX_UpdateEditWindow(&g->tmbox);
      sprintf(g->pcbox.text,"%.0f",g->ccd->percent);
      CBX_UpdateEditWindow(&g->pcbox);
      slpt = imin(30,++slpt);
    } /* endif(!err) */
  } while (1); // todo when to stop ? -- run in image mode ?
  sprintf(buf,"%s() done",PREFUN);
#if (DEBUG > 0)
  fprintf(stderr,"%s\n",buf);
#endif
  message(g,buf,MSG_FLUSH);

  return (void*)0;
}

/* ---------------------------------------------------------------- */

static int send_frame(ZwoGUI* g,ZwoFrame* frame)
{
  int err=0;

  int sock = TCPIP_CreateClientSocket(g->send_host,g->send_port,&err);
  if (!err) { Header head;
    head.dimx = frame->w;
    head.dimy = frame->h;
    head.seqn = (int)frame->seqNumber;
    head.flag = 0;
    assert(!is_bigendian());           /* network assumed big endian */
    swap4(&head,sizeof(head)/sizeof(int)); /* htonl() */
    ssize_t n1 = send(sock,&head,sizeof(head),0);
    size_t size = frame->w * frame->h * sizeof(u_short);
    u_short *data = (u_short*)malloc(size);
    swab(frame->data,data,size);
    ssize_t n2 = send(sock,data,size,0);
    (void)close(sock);
    free((void*)data);
    if (n1+n2 != size+sizeof(Header)) err = -1;
  }
  return err;
}

/* ---------------------------------------------------------------- */

static void* run_cycle(void* param)
{
  int    err=0,dt=0,last=0;
  double t1,t2,t3,nodata=2+MAX_EXPTIME;
  char buf[128];
  u_int seqNumber=0;
  pthread_t disp_tid=0;                 
  ZwoGUI *g = (ZwoGUI*)param;
#if (DEBUG > 0)
  fprintf(stderr,"%s: %s(%p)\n",__FILE__,PREFUN,param);
#endif

  ZwoStruct *ccd = g->ccd;
  assert(ccd);
  assert(g->init_flag == 1);

  g->loop_running = True;
  g->stop_flag = False;

  err = zwo_cycle_start(ccd);

  if (!err) {
    t1 = walltime(0);
    thread_create(run_display,g,&disp_tid);
  }
  sprintf(g->dtbox.text,"%d",0); CBX_UpdateEditWindow(&g->dtbox);

  if (!err) { ZwoFrame *frame=NULL;
    t1 = t2 = walltime(0);
    while (!g->stop_flag) {
      msleep(350);
      g->status.temp_ccd = g->ccd->tempSensor; //xxx 
      if ((seqNumber % 2) == 0) { // todo remove ?
        sprintf(g->tmbox.text,"%.1f",g->ccd->tempSensor); //xxx todo percent
        CBX_UpdateEditWindow(&g->tmbox);
      }
      t3 = walltime(0);
      frame = zwo_frame4reading(ccd,seqNumber);
      if (frame) {                     /* a new frame has arrived */
        seqNumber = frame->seqNumber;
        if (g->send_flag && (t3-t2 >= g->send_flag)) {
          int err = send_frame(g,frame);
          printf("%s: sending frame %u to %s:%d, err=%d\n",
                 get_ut_timestr(buf,cor_time(0)),seqNumber,
                 g->send_host,g->send_port,err);
          if (g->send_flag < 0) g->send_flag = 0;
          t2 = t3-0.175;
        }
        zwo_frame_release(ccd,frame);
        sprintf(g->sqbox.text,"%u",seqNumber);
        CBX_UpdateEditWindow(&g->sqbox);
        sprintf(g->fpbox.text,"%.1f",my_round(g->ccd->fps,1));
        CBX_UpdateEditWindow(&g->fpbox);
        if (dt != 0) { dt = last = 0;
          sprintf(g->dtbox.text,"%d",dt); CBX_UpdateEditWindow(&g->dtbox);
        }
        t1 = t3-0.175;
        nodata = 2+MAX_EXPTIME;
      } else {                         /* no new frame */
        dt = (int)floor(t3-t1);
        if (dt != last) {
          sprintf(g->dtbox.text,"%d",dt); CBX_UpdateEditWindow(&g->dtbox);
          last = dt;
        }
        if (dt > nodata) {
          message(g,"no data",MSG_WARN);
          nodata *= 1.5;
        }
      }
    } /* endwhile(!stop_flag) */
    zwo_cycle_stop(ccd);
  } /* endif(!err) */
  g->loop_running = False;
  sprintf(g->fpbox.text,"%d",0); CBX_UpdateEditWindow(&g->fpbox);
  sprintf(g->dtbox.text,"-"); CBX_UpdateEditWindow(&g->dtbox);

  if (disp_tid) pthread_join(disp_tid,NULL);

  if (err) {
    sprintf(buf,"%s: err=%d",PREFUN,err);
    message(g,buf,MSG_ERROR);
  }
  fprintf(stderr,"%s() done\n",PREFUN);

  return (void*)(long)err;
}
 
/* ---------------------------------------------------------------- */

static void* run_display(void* param) 
{
  ZwoGUI *g = (ZwoGUI*)param;
  u_int seqNumber=0;
  double fps=1,t1,t2,slpt=0.350,tmax;
  QlTool *qltool = g->qltool;
  ZwoStruct *ccd = g->ccd;
  assert(ccd);
  assert(g->init_flag == 1);

  int gx = g->status.dimx/g->status.binning;
  int gy = g->status.dimy/g->status.binning;
  qltool_reset(qltool,gx,gy,1);

  t1 = walltime(0);
  while (g->loop_running) {
    ZwoFrame *frame = zwo_frame4reading(ccd,seqNumber);
    if (frame) {
      seqNumber = frame->seqNumber; 
      qltool_update(qltool,frame->data);
      zwo_frame_release(ccd,frame);
      qltool_redraw(qltool,True);
      t2 = walltime(0); 
      fps = 0.7*fps + 0.3/(t2-t1);
      sprintf(g->fdbox.text,"%.1f",my_round(fps,1));
      CBX_UpdateEditWindow(&g->fdbox);
      t1 = t2;
    } // endif(frame)
    slpt = (throttle) ? 0.6*slpt + 0.4*slpt*(fps/throttle) : 0.02;
    tmax = (throttle) ? 1.0/throttle : 0.35;
    slpt = fmax(0.02,fmin(tmax,fmax(slpt,g->status.exptime/3.0)));
    // printf("slpt=%.3f\n",slpt);
    msleep((int)(1000.0*slpt)); 
  } // endwhile(!stop)
  sprintf(g->fdbox.text,"%d",0); CBX_UpdateEditWindow(&g->fdbox);
  fprintf(stderr,"%s() done\n",PREFUN);

  return (void*)0;
}

/* ---------------------------------------------------------------- */

int save_fits(u_short* data,FITSpars* st)
{
  return 0; //xxx
}

/* --- */

static void* run_write(void* param) 
{
  int  weStartedTheLoop=0;
  char buf[128];
  ZwoGUI *g = (ZwoGUI*)param;
  double fps=0,t1,t2;
  ZwoStruct *ccd = g->ccd;
  FITSpars *st = &g->status;

  if (g->init_flag != 1) return (void*)0;
  if (!g->loop_running) {
    weStartedTheLoop = do_start(g,2000);
  }

  t1 = walltime(0);
  while (g->loop_running) {
    ZwoFrame *frame = zwo_frame4reading(ccd,st->seqNumber);
    if (frame) {
      st->seqNumber = frame->seqNumber;
      st->start_int = frame->timeStamp;
      st->stop_int  = frame->timeStamp + st->exptime;
      int e = save_fits(frame->data,st); //xxx
      if (e) { 
        sprintf(buf,"FITS error=%d",e);
        message(g,buf,MSG_WARN);
      } else {
        sprintf(buf,"%s.fits",st->filename);
        message(g,buf,MSG_FLUSH);
        st->run += 1;
        if (st->run > MAX_FILENUM) {
          st->run = 1;
          message(g,"file number overflow",MSG_WARN);
        }
        sprintf(buf,FMT_RUN,0);
        put_long(setup_rc,buf,st->run);
      } 
      zwo_frame_release(ccd,frame);
      t2 = walltime(0);
      fps = 0.5*fps + 0.5/(t2-t1);
      printf("%s: got frame-%u, fps=%.1f\n",PREFUN,st->seqNumber,fps);
      t1 = t2;
      g->write_flag -= 1;
    } // endif(frame)
    if (g->write_flag <= 0) break;
    msleep(50);
  } // endwhile(loop-doing)

  if (weStartedTheLoop) do_stop(g,0);
  printf("%s() done\n",PREFUN);

  return (void*)0;
}

/* ---------------------------------------------------------------- */

static int set_exptime(ZwoGUI* g,float t)
{
  int err=0,prec=1;

  if (t > 0) {
    g->status.exptime = (float)fmin(MAX_EXPTIME,fmax(MIN_EXPTIME,t));
  }
  if      (g->status.exptime < 1) prec = 3;
  else if (g->status.exptime < 10) prec = 2;
  sprintf(g->tfbox.text,"%.*f",prec,g->status.exptime);
  CBX_ResetEditWindow(&g->tfbox);

  if (t > 0) {
    err = zwo_exptime(g->ccd,g->status.exptime);
    if (err) { char buf[128];
      g->status.exptime = (float)g->ccd->expTime;
      sprintf(buf,"sending 'ExpTime' failed, err=%d",err);
      message(g,buf,MSG_ERROR);
      set_exptime(g,0);                /* just update GUI */
    }
  }
  return err;
}

/* ---------------------------------------------------------------- */

void message(const void *param,const char* text,int flags)
{
  ZwoGUI *g=NULL;
  int  i=0;
  char tstr[32];
  static FILE* fp=NULL;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%s,%x)\n",PREFUN,param,text,flags);
#endif
  assert(text);

  if (param) {
    for (i=0; i<n_guis; i++) {      /* source */
      if (param == guis[i]) { g = guis[i]; break; }
      if (param == guis[i]->ccd) { g = guis[i]; break; }
    } 
  }

  if (flags & MSG_FILE) { char who[64];  /* write to file */
    pthread_mutex_lock(&mesgMutex); 
    if (!fp) fp = fopen(logfile,"a");
    if (fp) {
      strcpy(who,(g) ? g->name : P_TITLE);
      if      (flags & MSG_RED)     strcat(who,"-ERROR");
      else if (flags & MSG_YELLOW)  strcat(who,"-WARNING");
      get_ut_datestr(tstr,cor_time(0));
      fprintf(fp,"%s - %s: %s\n",tstr,who,text);
      if (flags & MSG_FLUSH) fflush(fp);
    } 
    pthread_mutex_unlock(&mesgMutex); 
  }

  if (!g) return;
  if (!(flags & MSG_WINDOW)) return;
  assert(mwin.disp);

  get_ut_timestr(tstr,cor_time(0));
  if (flags & MSG_OVERWRITE) {         /* overwrite */
    sprintf(g->msbox[0].text,"%s: %s",tstr,text);
    CBX_UpdateEditWindow(&g->msbox[0]);
    return;
  }
 
  pthread_mutex_lock(&mesgMutex); 
  for (i=n_msg-1; i>=0; i--) {         /* prepend line */
    if (i == 0) {
      sprintf(g->msbox[i].text,"%s: %s",tstr,text);
      if      (flags & MSG_RED)    CBX_ChangeLed(&g->led[i],app->red); 
      else if (flags & MSG_YELLOW) CBX_ChangeLed(&g->led[i],app->yellow); 
      else                         CBX_ChangeLed(&g->led[i],app->green); 
    } else {
      strcpy(g->msbox[i].text,g->msbox[i-1].text);
      CBX_ChangeLed(&g->led[i],g->led[i-1].bg);
    }
    CBX_UpdateEditWindow(&g->msbox[i]);
  }
  pthread_mutex_unlock(&mesgMutex); 
}

/* ---------------------------------------------------------------- */

static int check_datapath(char* path,int interactive)
{
  char buffer[1024],cmd[1024],*h;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%d)\n",PREFUN,path,interactive);
#endif

  cut_spaces(path);
  if (*path == '\0') return 0;

  if (path[0] != '/') {
    if (interactive) {
      sprintf(buffer,"invalid path (%s), must be absolute ('/...')",path);
      CBX_MessageBox(P_TITLE,buffer);
    }
    *path = '\0';
    return 0;
  }

  if (chdir(path) == -1) {
    if (interactive) {
      if (CBX_AlertBox(path,"does not exist - create new directory ?")) {
        strcpy(buffer,path); path[0]='\0';
        h=strtok(buffer,"/");
        do {
          strcat(path,"/"); strcat(path,h);
          if (chdir(path) == -1) {       /* path does not exist */
            sprintf(cmd,"mkdir %s",path);
            (void)system(cmd);
          }
        } while ((h=strtok(NULL,"/")) != NULL);
      } else {
        strcpy(path,genv2("HOME","tmp"));
        CBX_MessageBox(P_TITLE ": set DataPath to",path);
      }
      CBX_ClearAutoQueue(&mwin);
    } else {
      *path = '\0';
    }
  }
  if (*path) next_disk = 0;

  return (*path) ? 1 : 0;
}

/* ---------------------------------------------------------------- */

static void my_shutdown(Bool force)
{
  int i;

  if (!force) {
    force = True;
    for (i=0; i<n_guis; i++) {
      if (guis[i]->loop_running) force = False;
    }
  }
  if (force) { Bool wait=False;
    for (i=0; i<n_guis; i++) {
      if (guis[i]->loop_running) {
        wait = True;
        do_stop(guis[i],0);
      }
    }
    if (wait) msleep(1000);
    done = True;
  }
}

/* ---------------------------------------------------------------- */

static int handle_tcpip(const char* command,char* answer,size_t buflen)
{
  int  r=0;
  char cmd[128]="",par[128]="";

  strcpy(answer,"-Einvalid command\n");
  if (strlen(command) >= sizeof(cmd)) return 0;

  int n = sscanf(command,"%s %s",cmd,par);
  if (n < 1) return 0;

  if (!strcasecmp(cmd,"version")) {
    sprintf(answer,"%s\n",P_VERSION);
  } else 
  if (!strcasecmp(cmd,"quit")) {
    r = 1;
    strcpy(answer,"OK\n");
  } else {
    strcpy(answer,"-Eunknown command\n");
  }

  return r;
}

/* --- */

static int receive_string(int sock,char* string,size_t buflen)
{
  int     i=0;
  ssize_t r;

  do {
    r = recv(sock,&string[i],1,0);
    if (r != 1) break;
    if (string[i] == '\n') break;      /* <LF> */
    if (string[i] == '\r') break;      /* <CR> */
    i++;
  } while (i < buflen-1);
  string[i] = '\0';                    /* overwrite term-char */

  return r;
}

/* --- */

// TODO who stops starts this thread ?

static void* run_tcpip(void* param)
{
  ZwoGUI *g = (ZwoGUI*)param;
  int    port = TCPIP_PORT;
  int    err,msgsock,rval,r=0;
  char   cmd[128],buf[128];
  struct  sockaddr sadr;
  socklen_t size=sizeof(sadr);
#if (DEBUG > 0)
  fprintf(stderr,"%s: %s(%d)\n",__FILE__,PREFUN,port);
#endif

  if (param) return (void*)0;         // TODO 

  int sock = TCPIP_CreateServerSocket(port,&err);
  if (err) { 
    sprintf(buf,"TCPIP-Server failed, err=%d",err);
    message(g,buf,MSG_WARN);
    return (void*)(long)err;
  }
  sprintf(buf,"TCPIP-Server listening (%s,%d)",g->name,port);
  message(g,buf,MSG_FLUSH);

  while (!done) {
    msgsock = accept(sock,&sadr,&size);
    sprintf(buf,"connection accepted from %u.%u.%u.%u",
        (u_char)sadr.sa_data[2],(u_char)sadr.sa_data[3],
        (u_char)sadr.sa_data[4],(u_char)sadr.sa_data[5]);
    message(g,buf,MSG_FLUSH);
#ifdef SO_NOSIGPIPE                    /* 2017-07-05 */
    int on=1; (void)setsockopt(msgsock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
    do {
      rval = receive_string(msgsock,cmd,sizeof(cmd));
      if (rval > 0) {
        fprintf(stdout,"%s(): received '%s'\n",PREFUN,cmd);
        r = handle_tcpip(cmd,buf,sizeof(buf));
        send(msgsock,buf,strlen(buf),0);
      } else {
        fprintf(stdout,"%s(): rval=%d\n",PREFUN,rval);
      }
      if (r) break;
    } while (rval > 0);                /* loop while there's something */
    (void)close(msgsock);
  } /* while(!done) */

  (void)close(sock);
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d) done\n",PREFUN,port);
#endif

  return (void*)0;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

