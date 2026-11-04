/* ----------------------------------------------------------------
 *
 * gcamtail_test.c -- bench for gcamtail.c and gcmd.c
 *
 * Links the real gcamtail.o and gcmd.o with recording stand-ins for what
 * gcamzwo provides (remote_command, remote_exit, message) and prints one
 * line per event:
 *
 *   ACTION <command>     a command the camera ran (remote_command)
 *   EXIT                 the camera ended the program (remote_exit)
 *   LOG <text>           an 'eds:' log line
 *   WARN <text>          one that also goes to the screen
 *   AFTER <n> ROI <r> R <x>   with --trace, after each answer fed: the
 *                        cursor, the answered rotator, gcmd_line()'s result
 *   ROI <n>              the answered rotator at the end
 *
 *   gcamtail_test [options] ANSWERS...       feed each file's lines, as
 *                                            tcsis's answers, to gcmd_line();
 *                                            'HELLO <answer>' to gcmd_hello()
 *   gcamtail_test [options] --poll H:P --seconds S   the real poll
 *
 *   --gnum N (1)  --port P (0)  --cnca 0|1 (0)  --portexit 0|1 (1)  --trace
 *
 * ../py/gcmd_bench.py derives what the camera must do from the fixtures
 * and compares (make test).
 *
 * ---------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "zwogcam.h"                   /* message() */
#include "gcmd.h"
#include "gcamtail.h"

/* stand-ins for zwogcam.c ------------------------------------------ */

static pthread_mutex_t out_lock=PTHREAD_MUTEX_INITIALIZER;
static volatile int    exited=0;

static void out(const char* tag,const char* text)
{
  pthread_mutex_lock(&out_lock);
  if (text) printf("%s %s\n",tag,text);
  else      printf("%s\n",tag);
  fflush(stdout);
  pthread_mutex_unlock(&out_lock);
}

int remote_command(const char* cmd,char* reply,size_t n)
{
  out("ACTION",cmd);
  if (reply && (n > 0)) *reply = '\0';
  return 0;
}

void remote_exit(void)
{
  out("EXIT",NULL);
  exited = 1;
  gcamtail_stop();                     /* as my_shutdown() does */
}

void message(const void* who,const char* text,int flags)
{
  (void)who;
  out((flags & MSS_WINDOW) ? "WARN" : "LOG",text);  /* WARN: also on screen */
}

/* ------------------------------------------------------------------- */

int main(int argc,char** argv)
{
  GcamTail cfg={ "",0,1,0,0,1,NULL };
  char     line[1024],host[128]="",*colon;
  int      i,r,seconds=-1,trace=0;
  FILE*    fp;

  for (i=1; (i < argc) && !strncmp(argv[i],"--",2); i++) {
    if (!strcmp(argv[i],"--trace")) { trace = 1; continue; }
    if (i+1 >= argc) break;
    if      (!strcmp(argv[i],"--gnum"))     cfg.gnum     = atoi(argv[++i]);
    else if (!strcmp(argv[i],"--port"))     cfg.port     = atoi(argv[++i]);
    else if (!strcmp(argv[i],"--cnca"))     cfg.cnca     = atoi(argv[++i]);
    else if (!strcmp(argv[i],"--portexit")) cfg.portexit = atoi(argv[++i]);
    else if (!strcmp(argv[i],"--poll"))     snprintf(host,sizeof(host),"%s",argv[++i]);
    else if (!strcmp(argv[i],"--seconds"))  seconds      = atoi(argv[++i]);
    else { fprintf(stderr,"unknown option %s\n",argv[i]); return 1; }
  }
  if ((*host == '\0') ? (i >= argc) : (seconds < 0)) {
    fprintf(stderr,"usage: %s [options] ANSWERS... | [options] --poll HOST:PORT "
            "--seconds S\n",argv[0]);
    return 1;
  }

  if (*host) {                         /* the real poll over TCP */
    if ((colon=strrchr(host,':')) == NULL) { fprintf(stderr,"--poll HOST:PORT\n"); return 1; }
    *colon = '\0';
    cfg.host = host;
    cfg.tport = atoi(colon+1);
    if (gcamtail_start(&cfg)) return 2;
    for (i=0; (i < 10*seconds) && !exited; i++) usleep(100000);
    gcamtail_stop();
    usleep(200000);
    printf("ROI %d\n",gcamtail_telroi());
    return 0;
  }

  (void)gcamtail_start(&cfg);          /* no host: answers fed below */
  for (; (i < argc) && !exited; i++) {
    if ((fp=fopen(argv[i],"r")) == NULL) { perror(argv[i]); return 2; }
    while (!exited && (fgets(line,sizeof(line),fp) != NULL)) {
      size_t n=strlen(line);
      while ((n > 0) && ((line[n-1] == '\n') || (line[n-1] == '\r'))) line[--n] = '\0';
      if ((n == 0) || (line[0] == '#')) continue;
      r = (!strncmp(line,"HELLO",5)) ? gcmd_hello(line+((line[5] == ' ') ? 6 : 5))
                                     : gcmd_line(line);
      if (trace) printf("AFTER %lld ROI %d R %d\n",gcmd_after(),gcmd_telroi(),r);
    }
    fclose(fp);
  }
  printf("ROI %d\n",gcamtail_telroi());
  return 0;
}

/* ---------------------------------------------------------------- */
