/* ----------------------------------------------------------------
 *
 * graph.h
 *
 * ---------------------------------------------------------------- */

#include <cxt.h>

/* ---------------------------------------------------------------- */

typedef struct {
  char            name[32];
  Display         *disp;
  Window          win;
  int             x,y,w,h;
  GfC             gfc;
  int             nc;                  /* # channels */
  int             *m,*cur;
  float           **data;
  pthread_mutex_t lock;                /* MT-safe lock */
  float           vmin,vmax;
  int             half,quarter,eighth;
} GraphWindow;

/* ---------------------------------------------------------------- */

GraphWindow* graph_create(MainWindow*,Window,const char*,const char*,
                          int,int,int,int,int,int);
int  graph_event (GraphWindow*,XEvent*);
void graph_scale (GraphWindow*,float,float,int);
void graph_redraw(GraphWindow*);
void graph_add   (GraphWindow*,int,double,int);
void graph_add1  (GraphWindow*,double,int);

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */


