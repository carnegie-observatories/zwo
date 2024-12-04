/* ----------------------------------------------------------------
 *
 * ptlib.c
 * 
 * POSIX thread wrapper library ...  
 *
 * 2000-06-30  Christoph C. Birk, birk@obs.carnegiescience.edu
 * 2000-10-05  Sempahores (pthread_mutex)
 * 2001-06-25  ip_mutex_()
 * 2001-12-04  SemaphoreExc()
 * 2002-12-04  Stack
 * 2011-11-10  generic version (BNC, CAPS, NewCCD)
 * 2012-07-24  MIKE
 * 2013-05-13  MagE,PFS
 * 2013-05-22  LDSS3,MIKE,IMACS
 *
 * ---------------------------------------------------------------- */

#define IS_PTLIB_C

/* DEFINEs -------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           1              /* debug level */
#endif

#ifndef PROJECT_ID
#define PROJECT_ID      0x0000
#endif

#define PREFUN          __func__

/* INCLUDES ------------------------------------------------------- */

#define _REENTRANT

#include <stdlib.h>                    /* atol(),free() */
#include <stdio.h>                     /* fprintf() */
#include <string.h>                    /* strcpy() */
#include <math.h>                      /* atof() */
#include <assert.h>

#if (DEBUG > 0)
#include <errno.h>
#endif

#include <sys/types.h>
#include <sys/ipc.h>                   /* IPC_ */
#include <sys/sem.h>                   /* semget(),semop(),semctl() */

#include "ptlib.h"

/* GLOBALs -------------------------------------------------------- */

/* none */


/* EXTERNs -------------------------------------------------------- */

/* none */

/* STATICs -------------------------------------------------------- */

static void ptlib_msleep(int msec)
{
  struct timespec delay,rem;

  if (msec <= 0) return;

  delay.tv_sec  = msec/1000;
  delay.tv_nsec = (msec % 1000) * 1000000;

  while (nanosleep(&delay,&rem) < 0) {
    delay.tv_sec = rem.tv_sec;
    delay.tv_nsec = rem.tv_nsec;
  }
}

static int imin(int a,int b) { return (a < b) ? a : b; }
static int imax(int a,int b) { return (a > b) ? a : b; }

/* ---------------------------------------------------------------- */
/* inter-process mutex */
/* ---------------------------------------------------------------- */

int ip_mutex_init(IP_Mutex* ipm,u_short project_id,u_short mutex_id)
{
  int   i;
  key_t key;
  union semun {
    int             val;
    struct semid_ds *buf;
    ushort          *array;
  } arg;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d,%d)\n",PREFUN,ipm,project_id,mutex_id);
#endif

  assert((sizeof(IP_Mutex) % sizeof(int)) == 0);

  if (mutex_id >= IP_MUTEX_MAX) return(-1);

  key = (key_t)((long)project_id << 16);    /* create key */

  ipm->semid = semget(key,IP_MUTEX_MAX,IPC_CREAT | IPC_EXCL | 00600);
  if (ipm->semid == -1) {              /* cannot create EXCL */
#if (DEBUG > 1)
    fprintf(stderr,"%s(0x%x): %s (errno=%d)\n",PREFUN,
            key,strerror(errno),errno);
#endif
    ipm->semid = semget(key,IP_MUTEX_MAX,0);  /* get semid */
    if (ipm->semid == -1) {            /* cannot get semaphore */
#if (DEBUG > 0)
      fprintf(stderr,"%s(0x%x): %s (errno=%d)\n",PREFUN,
              (int)key,strerror(errno),errno);
#endif
      return(-1);
    }
  } else {
#if (DEBUG > 1)
    fprintf(stderr,"first ip_mutex(0x%x)\n",key); 
#endif
    arg.val = 1;                       /* pre-set value to '1' */
    for (i=0; i<IP_MUTEX_MAX; i++) {
      semctl(ipm->semid,i,SETVAL,arg);
    }
  }
  ipm->key      = key;
  ipm->mutex_id = (int)mutex_id;
#if (DEBUG > 1)
  fprintf(stderr,"semval(0x%x,%d)=%d\n",ipm->key,ipm->mutex_id,
          semctl(ipm->semid,mutex_id,GETVAL));
#endif

  return(0);
}

/* ---------------------------------------------------------------- */

int ip_mutex_trylock(IP_Mutex* ipm)
{
  int err;
  struct sembuf sbuf;

  sbuf.sem_num = (short)ipm->mutex_id;
  sbuf.sem_op  = -1;
  sbuf.sem_flg = IPC_NOWAIT; 
  err = semop(ipm->semid,&sbuf,1);
#if (DEBUG > 1)
  fprintf(stderr,"%s(0x%x,%d)=%d\n",PREFUN,ipm->key,ipm->mutex_id,err);
#endif

  return(err);
}

/* ---------------------------------------------------------------- */

void ip_mutex_lock(IP_Mutex* ipm)
{
  struct sembuf sbuf;

  sbuf.sem_num = (short)ipm->mutex_id;
  sbuf.sem_op  = -1;
  sbuf.sem_flg = 0;                    /* wait */
  (void)semop(ipm->semid,&sbuf,1);
}

/* ---------------------------------------------------------------- */

void ip_mutex_unlock(IP_Mutex* ipm)
{
  struct sembuf sbuf;

  sbuf.sem_num = (short)ipm->mutex_id;
  sbuf.sem_op  = 1;
  sbuf.sem_flg = 0;
  (void)semop(ipm->semid,&sbuf,1);
}

/* ---------------------------------------------------------------- */
/* create threads */
/* ---------------------------------------------------------------- */

int thread_create(void*(*start_routine)(void*),void* arg,pthread_t* tid)
{
  int            err;
  pthread_attr_t attr;

#if (DEBUG > 1)
  static u_long  count=0;
  count++;
  fprintf(stderr,"%s(%lu)\n",PREFUN,count);
#endif

  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr,PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
  err = pthread_create(tid,&attr,start_routine,arg);
  pthread_attr_destroy(&attr);

  return(err);
}

/* ---------------------------------------------------------------- */

int thread_detach(void*(*start_routine)(void*),void* arg)
{
  int            err;
  pthread_t      tid;
  pthread_attr_t attr;
 
#if (DEBUG > 1)
  fprintf(stderr,"%s(project_id=%d)\n",PREFUN,PROJECT_ID);
#endif
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr,PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
  err = pthread_create(&tid,&attr,start_routine,arg);
  pthread_attr_destroy(&attr);
 
  return(err);
}

/* ---------------------------------------------------------------- */
/* local semaphores */
/* ---------------------------------------------------------------- */

void SemaphoreInit(Semaphore* sem,int maxcount)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d)\n",PREFUN,sem,maxcount);
#endif
  pthread_mutex_init(&(sem->lock),NULL);
  pthread_mutex_lock(&(sem->lock));
  sem->count    = 0;
  sem->maxcount = maxcount;
  pthread_mutex_unlock(&(sem->lock));
}

/* ---------------------------------------------------------------- */

int SemaphoreInc(Semaphore* sem,int timeout)
{
  int slt;

  slt = imax(50,timeout/20);
  do {
    pthread_mutex_lock(&(sem->lock));
    if ((sem->maxcount == 0) || (sem->count < sem->maxcount)) { /* ok */
      sem->count++;                    /* increment count */
#if (DEBUG > 1)
      fprintf(stderr,"sem->count=%d\n",sem->count);
#endif
      pthread_mutex_unlock(&(sem->lock));
      return(1);
    }
    pthread_mutex_unlock(&(sem->lock));         /* not ok */
    ptlib_msleep(imin(timeout,slt)); timeout -= slt;  /* wait */
  } while (timeout >= 0);
#if (DEBUG > 0)
  fprintf(stderr,"%s() failed\n",PREFUN);
#endif

  return(0);
} 

/* ---------------------------------------------------------------- */
 
int SemaphoreExc(Semaphore* sem,int timeout)
{
  int slt;
 
  slt = imax(50,timeout/20);
  do {
    pthread_mutex_lock(&(sem->lock));
    if (sem->count == 0) {             /* if counter is zero */
      sem->count = 1;                  /* increase it */
      pthread_mutex_unlock(&(sem->lock));
      return(1);
    }
    pthread_mutex_unlock(&(sem->lock));
    ptlib_msleep(imin(timeout,slt)); timeout -= slt;
  } while (timeout >= 0);
#if (DEBUG > 1)
  fprintf(stderr,"%s() failed\n",PREFUN);
#endif
 
  return(0);
}
 
/* ---------------------------------------------------------------- */

void SemaphoreDec(Semaphore* sem)
{
  pthread_mutex_lock(&(sem->lock));
  if (sem->count > 0) sem->count--;
#if (DEBUG > 1)
  fprintf(stderr,"sem->count=%d\n",sem->count);
#endif
  pthread_mutex_unlock(&(sem->lock));
}

/* ---------------------------------------------------------------- */

int SemaphoreGet(Semaphore* sem)
{
  int count;

  pthread_mutex_lock(&(sem->lock));
  count = sem->count;
  pthread_mutex_unlock(&(sem->lock));

  return(count);
}

/* ---------------------------------------------------------------- */

int SemaphoreGetAdd(Semaphore* sem,int add)
{
  int count;

  pthread_mutex_lock(&(sem->lock));
  count = sem->count;
  sem->count += add; 
  if (sem->count < 0) sem->count = 0;
  if (sem->maxcount > 0){
    if (sem->count > sem->maxcount) sem->count = sem->maxcount;
  }
  pthread_mutex_unlock(&(sem->lock));

  return(count);
}

/* ---------------------------------------------------------------- */

int StackInit(Stack* stack,int size)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d) ...\n",PREFUN,size);
#endif
  pthread_mutex_init(&(stack->lock),NULL);
  pthread_mutex_lock(&(stack->lock));
  stack->count = 0;
  stack->size  = size;
  stack->data  = (int*)malloc(size*sizeof(int));
  pthread_mutex_unlock(&(stack->lock));
#if (DEBUG > 1)
  if (stack->data == NULL) fprintf(stderr,"%s(): malloc() failed\n",PREFUN);
  else                     fprintf(stderr,"%s(): ok\n",PREFUN);
#endif
  if (stack->data == NULL) return(-1);
  return(0);
}

/* ---------------------------------------------------------------- */

int StackPush(Stack* stack,int value)
{
  int count;

  pthread_mutex_lock(&(stack->lock));
  if (stack->count == stack->size) {
    fprintf(stderr,"%s: %s(): overflow\n",__FILE__,PREFUN);
    pthread_mutex_unlock(&(stack->lock));
    return(-1);
  }
  stack->data[stack->count] = value;
  count = (stack->count += 1);
  pthread_mutex_lock(&(stack->lock));
  return(count);
}
  
/* ---------------------------------------------------------------- */

int StackPull(Stack* stack,int* value)
{
  int count;

  pthread_mutex_lock(&(stack->lock));
  if (stack->count == 0) {
    fprintf(stderr,"%s: %s(): underflow\n",__FILE__,PREFUN);
    pthread_mutex_unlock(&(stack->lock));
    return(-1);
  }
  count = (stack->count -= 1);
  *value = stack->data[count];
  pthread_mutex_lock(&(stack->lock));
  return(count);
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

