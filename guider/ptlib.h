/* ----------------------------------------------------------------
 *
 * ptlib.h
 *
 * 2000-06-30, Christoph C. Birk, birk@obs.carnegiescience.edu
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_PTLIB_H
#define INCLUDE_PTLIB_H 

/* ---------------------------------------------------------------- */ 

#ifndef _REENTRANT
#define _REENTRANT
#endif

#ifdef MACOSX
#define thread_create  my_thread_create
#endif

#ifdef LINUX                          /* WARNING: depends on OS */
#define IP_MUTEX_MAX 250              /* Linux: /proc/sys/kernel/sem */
#elif MACOSX
#define IP_MUTEX_MAX 128              /* MacOS-X: >1024 */
#else
#define IP_MUTEX_MAX 16               /* Solaris: 16 */
#endif

#include <pthread.h>                   /* mutex and thread creation */
#include <signal.h>

#include <sys/types.h>                 /* key_t */

/* TYPEDEFs ------------------------------------------------------- */ 

typedef struct ip_mutex_tag {
  key_t           key;
  int             semid;
  int             mutex_id;
} IP_Mutex;                            /* mod-sizeof(int)==0 */

typedef struct semaphore_tag {
  pthread_mutex_t lock;
  int             count;
  int             maxcount;
} Semaphore;

typedef struct stack_tag {
  pthread_mutex_t lock;
  int             count;
  int             size;
  int*            data;
} Stack;

/* ---------------------------------------------------------------- */ 

int     ip_mutex_init     (IP_Mutex*,u_short,u_short);
void    ip_mutex_lock     (IP_Mutex*);
int     ip_mutex_trylock  (IP_Mutex*);
void    ip_mutex_unlock   (IP_Mutex*);

int     thread_create     (void*(*start_routine)(void*),void*,pthread_t*);
int     thread_detach     (void*(*start_routine)(void*),void*);

void    SemaphoreInit     (Semaphore*,int);
int     SemaphoreInc      (Semaphore*,int);
int     SemaphoreExc      (Semaphore*,int);
void    SemaphoreDec      (Semaphore*);
int     SemaphoreGet      (Semaphore*);
int     SemaphoreGetAdd   (Semaphore*,int);
 
int     StackInit         (Stack*,int);
int     StackPush         (Stack*,int);
int     StackPull         (Stack*,int*);

/* ---------------------------------------------------------------- */

#endif  /* INCLUDE_PTLIB_H */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

