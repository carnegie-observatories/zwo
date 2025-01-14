/* ----------------------------------------------------------------
 *
 * random.c
 *
 * random number library
 *
 * 1994-03-28  Christoph C. Birk
 * 1995-05-05  gauss(), fgauss(), assume ANSI
 * 2006-08-23  Mersenne Twister
 * 2007-10-23  multi-host safe
 * 2007-11-02  multi-thread safe
 * 2010-10-28  lockless access (uses lock for generation only)
 * 2011-11-09  use /dev/urandom, (unsigned int)
 * 2012-05-02  MT-safe using lrand48() as fallback
 * 2013-03-08  state structure to enable lock-less MT
 * 2013-04-12  use MAC-address as host signature
 * 2018-03-22  fully MT-safe global generator, multiple instances
 * 2018-05-10  URandom()
 *
 * ---------------------------------------------------------------- */

/* DEFINEs -------------------------------------------------------- */

#define IS_RANDOM_C

#ifndef DEBUG
#define DEBUG      0                   /* debug level */
#endif

#define PREFUN     __func__

/* INCLUDEs ------------------------------------------------------- */

#ifdef  _MT                            /* Windows */
#define _REENTRANT                     /* UN*X */
#endif
#undef  USE_URANDOM 
#undef  USE_MACADDR

#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <assert.h> 
#ifdef MACOSX
#include <sys/types.h>
#endif

#ifdef USE_URANDOM
#include <fcntl.h>
#endif
#include <sys/time.h>

#ifdef USE_MACADDR
#include <sys/socket.h>
#include <ifaddrs.h>
#ifdef LINUX
#include <netdb.h>
#else
#include <net/if_dl.h>
#endif
#endif

#if (DEBUG > 0)
#include <stdio.h>
#endif

#ifdef _REENTRANT
#include <pthread.h>
#endif

#include "random.h"

/* ---------------------------------------------------------------- */

#define N          624
#define M          397
#define MATRIX_A   0x9908b0df          /* constant vector a */
#define UPPER_MASK 0x80000000          /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff          /* least significant r bits */

/* STATICs -------------------------------------------------------- */

typedef struct {
  unsigned int mt[N];
  volatile int mti;
} mt_state_t;

static mt_state_t *mt_state=NULL;      /* global state */

#ifdef _REENTRANT
static pthread_mutex_t mt_lock; 
#endif

static unsigned int    mt_19937_r    (mt_state_t*);
static void            mt_shuffle_r  (mt_state_t*);
static void            mt_init       (mt_state_t*,unsigned int);
static void            mt_init_array (mt_state_t*,unsigned int*,int);

/* ---------------------------------------------------------------- */
/*
 * MT19937
 * Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
 * http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
 *
 */
/* ---------------------------------------------------------------- */

static unsigned int mt_19937_r(mt_state_t* state)
{
  if (state->mti >= N) mt_shuffle_r(state);

  assert(state->mti < N);
  unsigned int y = state->mt[state->mti];
  state->mti += 1;

  y ^= (y >> 11);                      /* tempering */
  y ^= (y <<  7) & 0x9d2c5680;
  y ^= (y << 15) & 0xefc60000;
  y ^= (y >> 18);

  return y;
}

/* ---------------------------------------------------------------- */

static void mt_shuffle_r(mt_state_t* state)
{
  auto  int          kk;
  auto  unsigned int y,*mt;
  const unsigned int mag01[2]={0x0,MATRIX_A};
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,state);
#endif

  mt = state->mt;

  for (kk=0; kk<N-M; kk++) {
    y = (mt[kk]&UPPER_MASK) | (mt[kk+1]&LOWER_MASK);
    mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1];
  }
  for ( ; kk<N-1; kk++) {
    y = (mt[kk]&UPPER_MASK) | (mt[kk+1]&LOWER_MASK);
    mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1];
  }
  y = (mt[N-1]&UPPER_MASK) | (mt[0]&LOWER_MASK);
  mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1];

  state->mti = 0;
}

/* ---------------------------------------------------------------- */

static void mt_init(mt_state_t* state,unsigned int s)
{
  int j;
  unsigned int *mt;

  mt = state->mt;

  mt[0]= s & 0xffffffff;
  for (j=1; j<N; j++) {
    mt[j] = (1812433253 * (mt[j-1] ^ (mt[j-1] >> 30)) + j); 
  }
  /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
  /* In the previous versions, MSBs of the seed affect   */
  /* only MSBs of the array state[].                     */
  /* 2002/01/09 modified by Makoto Matsumoto             */
}

/* ---------------------------------------------------------------- */

static void mt_init_array(mt_state_t* state,unsigned int* init_key, 
                          int key_length)
{
  int i,j,k;
  unsigned int *mt;
#if (DEBUG > 1) 
  fprintf(stderr,"%s(%p,%p,%d)\n",PREFUN,state,init_key,key_length);
#if (DEBUG > 2)
  for (i=0; i<key_length; i++) printf("%08x ",init_key[i]); 
  printf("\n");
#endif
#endif

  mt_init(state,19650218);
  mt = state->mt;

  i=1; j=0;
  k = (N>key_length ? N : key_length);
  for (; k; k--) {
    mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1664525))
            + init_key[j] + j;         /* non linear */
    i++; j++;
    if (i >= N) { mt[0] = mt[N-1]; i=1; }
    if (j >= key_length) j=0;
  }
  for (k=N-1; k; k--) {
    mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1566083941)) 
             - i; /* non linear */
    i++;
    if (i>=N) { mt[0] = mt[N-1]; i=1; }
  }

  mt[0] = 0x80000000;   /* MSB is 1; assuring non-zero initial array */ 
}

/* ---------------------------------------------------------------- */

#ifdef USE_MACADDR
static int listmacaddrs(char* buffer,size_t size)
{
  struct ifaddrs *ifap, *ifaptr;
  int count=0,i;

  if (getifaddrs(&ifap) != 0) return 0;

  for (ifaptr = ifap; ifaptr != NULL; ifaptr = (ifaptr)->ifa_next) {
#ifdef LINUX
    int family,s; char host[NI_MAXHOST];
    family = ifaptr->ifa_addr->sa_family;
     if (family == AF_INET || family == AF_INET6) {
       s = getnameinfo(ifaptr->ifa_addr,
             (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                   sizeof(struct sockaddr_in6),
              host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
      if (s == 0) {
#if (DEBUG > 1)
        printf("\taddress: <%s>\n", host);
#endif
        if (buffer) {
          i = 0;
          while (host[i]) {
            if (count >= size) break;
            buffer[count++] = host[i++];
          }
        }
      }
    }
#else
    if (((ifaptr)->ifa_addr)->sa_family == AF_LINK) {
      unsigned char *ptr;
      ptr = (unsigned char *)LLADDR((struct sockaddr_dl *)(ifaptr)->ifa_addr);
#if (DEBUG > 1)
      printf("%s: %02x:%02x:%02x:%02x:%02x:%02x\n",(ifaptr)->ifa_name,
             *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4), *(ptr+5));
#endif
      if (buffer) {
        for (i=0; i<6; i++) {
          if (count >= size) break;
          buffer[count++] = (char)ptr[i];
        }
      }
    }
#endif
  }
  freeifaddrs(ifap);

  return count;
}
#endif

/* ---------------------------------------------------------------- */

void InitRandom(int seed1,int seed2,int seed3)
{
#ifdef _REENTRANT
#if (DEBUG > 0)
  fprintf(stderr,"%s(): _REENTRANT defined\n",PREFUN);
#endif
  pthread_mutex_init(&mt_lock,NULL);  
#endif
  assert(!mt_state);

  mt_state = InitRandom_r(seed1,seed2,seed3);
}

/* --- */

void* InitRandom_r(int seed1,int seed2,int seed3)
{
  unsigned int key[N]; 
  int          len=1;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d,%d,%d)\n",PREFUN,seed1,seed2,seed3);
#endif

  mt_state_t* state = (mt_state_t*)malloc(sizeof(mt_state_t));
  assert(sizeof(int) == 4);

  key[0] = 0x80000000;                 /* key never '0' */
  if ((seed1==0) && (seed2==0) && (seed3==0)) { struct timeval tp;
    key[len++] = (unsigned int)((u_long)state & 0xffffffff);
    gettimeofday(&tp,NULL);
    key[len++] = (unsigned int)tp.tv_sec;  /*  2 bits */
    key[len++] = (unsigned int)tp.tv_usec; /* 19 bits */
    key[len++] = (unsigned int)getpid();   /* 15 bits */
#ifdef USE_URANDOM
    { int fd,i;
      fd = open("/dev/urandom",O_RDONLY);
      if (fd != -1) {                  /* add 128 bits */
        for (i=0; i<4; i++) read(fd,&key[len++],sizeof(unsigned int));
        close(fd);
      } else { u_int seed;
        seed = (((u_int)getpid()) << 17) ^ ((u_int)time(NULL));
        srand48(seed);
        for (i=0; i<8; i++) key[len++] = lrand48(); /* 2013-09-16 */
      }
    }
#endif
#ifdef USE_MACADDR
    { char buffer[1024]; int cnt,i;    /* add kind-of 'host-signature' */
      cnt = listmacaddrs(buffer,sizeof(buffer));
      for (i=0; i<(cnt/4); i++,len++) {
        if (len == N) break;
        key[len] = *(((unsigned int*)buffer)+i);
      }
      /* for (i=0; i<len; i++) printf("0x%08x\n",key[i]); */
    }
#endif
  } else {                             /* seed given */
    key[1] = (unsigned int)seed1; len++;
    key[2] = (unsigned int)seed2; len++;
    key[3] = (unsigned int)seed3; len++;
  }

  mt_init_array(state,key,len);
  state->mti = N; mt_shuffle_r(state);
  assert(state->mti == 0);

#if (DEBUG > 1)
  { int i; for (i=0; i<8; i++) printf("%08x ",state->mt[i]); printf("\n"); }
#endif

  return state;
}

/* ---------------------------------------------------------------- */

u_int URandom_r(void* param)
{
  return mt_19937_r(param);
}

/* --- */

u_int URandom(void)
{
#ifdef _REENTRANT
  pthread_mutex_lock(&mt_lock);
#endif
  u_int u = URandom_r(mt_state);
#ifdef _REENTRANT
  pthread_mutex_unlock(&mt_lock);
#endif
  return u;
}

/* ---------------------------------------------------------------- */

int IRandom(int x)
{
#ifdef _REENTRANT
  pthread_mutex_lock(&mt_lock);
#endif
  int i = IRandom_r(mt_state,x);
#ifdef _REENTRANT
  pthread_mutex_unlock(&mt_lock);
#endif
  return i;
}

/* --- */

int IRandom_r(void* param,int x)
{
  return((int)(mt_19937_r(param) >> 1) % x);  /* [0,2^31) -> [0,x) */
}

/* ---------------------------------------------------------------- */

double DRandom(double x)
{
#ifdef _REENTRANT
  pthread_mutex_lock(&mt_lock);
#endif
  double d = DRandom_r(mt_state,x);
#ifdef _REENTRANT
  pthread_mutex_unlock(&mt_lock);
#endif
  return d;
}

/* --- */

double DRandom_r(void* param,double x) /* [0,x) */
{
  return x * (double)mt_19937_r(param) * (1.0/4294967296.0);
}

/* --- */

#if 0 // unused
double DRandom53(double x)             /* [0,1) */
{
  unsigned int a,b;                    /* 53 bits */

  a = mt_19937(0) >> 5;
  b = mt_19937(0) >> 6;
  return x * ((double)a*67108864.0+(double)b) * (1.0/9007199254740992.0);
}
#endif

/* ---------------------------------------------------------------- */

double GRandom(double mean,double sigma)
{
#ifdef _REENTRANT
  pthread_mutex_lock(&mt_lock);
#endif
  double g = GRandom_r(mt_state,mean,sigma);
#ifdef _REENTRANT
  pthread_mutex_unlock(&mt_lock);
#endif
  return g;
}
/* --- */

double GRandom_r(void* param,double mean,double sigma)
{
  double x,y,r;

  do {
    x = DRandom_r(param,2.0)-1.0;
    y = DRandom_r(param,2.0)-1.0;
    r = x*x + y*y;
  } while (r >= 1.0);

  return(mean+(sigma*x*sqrt(-2.0*log(r)/r)));
}

/* ---------------------------------------------------------------- */

int PRandom(double mean)               /* poisson */
{
  int k=0;

  if (mean < 20.0) { 
    double p = exp(-mean);
    double s = p;
    double u = DRandom(1.0);
    while (u > s) {  
      k += 1;
      p *= mean/(double)k;
      s += p;
    }
  } else {
    double x = GRandom(mean,sqrt(mean));
    if (x >= 0.5) k = (int)floor(x+0.5);
  } 
  return k;
}
 


/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

