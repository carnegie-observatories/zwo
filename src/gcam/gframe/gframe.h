/* ----------------------------------------------------------------
 *
 * gframe.h -- guide corrections as 'G' frames for the Swope TCS
 *
 * The Swope TCS takes guide corrections as the 7-byte frames the DOS
 * guider PC writes on its serial line, "Gxxyy" CR LF.  gframe serves
 * them on a TCP port and the TCS connects to it, as it connects to
 * the serial-to-Ethernet converter today.  Wire format, behaviour,
 * the hooks in gcamzwo and how to remove this directory: README.md.
 *
 * Standard headers only (POSIX sockets, poll, pthreads), nothing from
 * gcamzwo, so the bench builds it alone.
 *
 * 2026-11-09  first version
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_GFRAME_H
#define INCLUDE_GFRAME_H

/* DEFINEs -------------------------------------------------------- */

#define GFRAME_LEN         7           /* 'G' xx yy CR LF */
#define GFRAME_CLIENTS_MAX 4           /* the TCS, plus a listener or two */

/* function prototype(s) ------------------------------------------ */

int   gframe_ini    (const char* key,const char* val); /* 1 = a gframe_* key, taken */
int   gframe_enabled(void);            /* gframe_port is set */
int   gframe_start  (void (*on_log)(int level,const char* text)); /* 0 or errno */
void  gframe_stop   (void);            /* close the port, end the thread */
void  gframe_add    (double x,double y); /* arcsec, the axes rotate() gives */
void  gframe_clear  (void);            /* drop what is pending */
int   gframe_clients(void);            /* connected clients */
char* gframe_status (char* buf,int len); /* " gframe=N gpend=X,Y gsent=N" */
int   gframe_encode (double* x,double* y,char* frame); /* the bench's entry */

#endif /* INCLUDE_GFRAME_H */

/* ---------------------------------------------------------------- */
