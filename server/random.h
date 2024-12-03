/* ----------------------------------------------------------------
 *
 * random.h
 * 
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_RANDOM_H
#define INCLUDE_RANDOM_H

/* ---------------------------------------------------------------- */

#ifdef IS_RANDOM_C
#define EXT_RND
#else
#define EXT_RND extern
#endif

/* ---------------------------------------------------------------- */

EXT_RND void   InitRandom (int,int,int);
EXT_RND unsigned int  URandom    (void);
EXT_RND int    IRandom    (int);
EXT_RND double DRandom    (double);
EXT_RND double GRandom    (double,double);

EXT_RND void*  InitRandom_r (int,int,int);
EXT_RND unsigned int  URandom_r    (void*);
EXT_RND int    IRandom_r    (void*,int);
EXT_RND double DRandom_r    (void*,double);
EXT_RND double GRandom_r    (void*,double,double);

/* ---------------------------------------------------------------- */

#endif /* INCLUDE_RANDOM_H */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

