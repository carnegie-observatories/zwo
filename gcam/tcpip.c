/* ---------------------------------------------------------------- 
 *
 * tcpip.c         
 *
 * TCP/IP library (OCIW, Pasadena, CA)
 * 
 * 1998-05-05  Christoph C. Birk (birk@obs.carnegiescience.edu)
 * 1998-11-18  WIN32 version
 * 1999-06-22  UNIX version
 * 2000-07-27  TCPIP_GetHostent() cache
 * 2000-10-06  TCPIP_SendFinito()
 * 2002-05-02  tcpip_errors[]
 * 2002-05-28  setsockopt(SO_REUSEADDR)
 *     -08-14  improved TCPIP_GetHostent()
 *        -19  tcpip_generic_server MT-safe
 * 2003-02-10  ignore trailing white-space
 *     -07-16  udp_(), remove TCPIP_GetHostent()
 * 2014-04-15  lock around gethostbyname(),getprotobyname()
 * 2017-07-03  SO_NOSIGPIPE,MSG_NOSIGNAL
 *
 * ---------------------------------------------------------------- */

#define IS_TCPIP_C

/* DEFINEs -------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG      1                   /* debug level */
#endif

#define PREFUN     __func__

/* INCLUDEs ------------------------------------------------------- */

#define _REENTRANT

#include <stdlib.h>                    /* free(),calloc() */
#include <stdio.h>                     /* sscanf() */
#include <string.h>                    /* memset(),strlen() */
#include <unistd.h>                    /* close(),sleep() */
#include <fcntl.h>                     /* fcntl() */
#include <pthread.h>                   /* pthread_() */
#include <signal.h>                    /* pthread_kill() */
#include <assert.h>
#include <errno.h>
#include <netdb.h>                     /* getprotobyname() */

#include <netinet/tcp.h>

#include "tcpip.h" 
#include "utils.h"                     /* get_local_datestr(),tdebug() */

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL    0              /* BSD/macOS */
#endif

/* STATICs -------------------------------------------------------- */

static pthread_mutex_t *tcpip_mutex=NULL;

static void* tcpip_generic_server (void*);
static int   tcpip_thread_create  (void*(*start_routine)(void*),void*,
                                   pthread_t*);

/* local functions ------------------------------------------------ */

static int tcpip_mutex_lock(int timeout)
{
  if (!tcpip_mutex) {
    tcpip_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(tcpip_mutex,NULL);
  }
  while (pthread_mutex_trylock(tcpip_mutex)) {
    if (--timeout >= 0) sleep(1);
    if (timeout < 0) break;
  }
  if (timeout < 0) return -1;

  return 0;
}

/* --- */

static void tcpip_mutex_unlock(void)
{
  if (tcpip_mutex) pthread_mutex_unlock(tcpip_mutex);
}

/* global functions ----------------------------------------------- */

int TCPIP_CreateServerSocket(u_short port,int *err)
{
  char   hostname[256];
  int    sock,protocol=0,result;
  struct sockaddr_in saddr;

  *err = 0;
  memset((void*)&saddr,0,sizeof(saddr));
  saddr.sin_family = AF_INET; 
  saddr.sin_port = htons(port);

  if (gethostname(hostname,sizeof(hostname))) *err = E_tcpip_hostname;

  result = tcpip_mutex_lock(5); 
  if (*err == 0) { struct  protoent *proto;
    proto = getprotobyname("tcp");       /* get protocol by name */
    if (proto == NULL) *err = E_tcpip_protocol;
    else               protocol = proto->p_proto;
  }
  if (*err == 0) { struct  hostent *hostp;
    hostp = gethostbyname(hostname);     /* get host IP address */
    if (hostp == NULL) {
      *err = E_tcpip_hostbyname;
    } else {
#if 0 /* 1st interface */
      memcpy(&saddr.sin_addr,hostp->h_addr_list[0],hostp->h_length);
#else /* allow any interface */
      saddr.sin_addr.s_addr = INADDR_ANY;
#endif
    }
  }
  if (!result) tcpip_mutex_unlock();

  if (*err == 0) {                     /* create socket */
    if ((sock = socket(PF_INET,SOCK_STREAM,protocol)) == -1) {
      *err = E_tcpip_socket; 
    }
  }
  if (*err == 0) { int on=1;           /* close-on-exec flag */
    (void)fcntl(sock,F_SETFD,FD_CLOEXEC); 
    (void)setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
#ifdef SO_NOSIGPIPE                    /* 2017-07-03 */
    (void)setsockopt(sock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
  }
  if (*err == 0) {
    if (bind(sock,(struct sockaddr *)&saddr,sizeof(saddr)) == -1) {
      (void)close(sock);
      *err = E_tcpip_bind;
      fprintf(stderr,"%s: bind() failed, errno=%d\n",__FILE__,errno);
    }
  }
  if (*err == 0) {
    if (listen(sock,SOMAXCONN) == -1) {
      (void)close(sock);
      *err = E_tcpip_listen;
    }
  }
  
  return((*err) ? -1 : sock);
}

/* ---------------------------------------------------------------- */

int TCPIP_ServerAccept(int sock,IP_Address *ip)
{
  int             msgsock;
  socklen_t       size;
  struct sockaddr saddr;

  size = sizeof(saddr);
  msgsock = accept(sock,&saddr,&size); /* accept connection */

  if (msgsock != -1) {
    ip->byte1 = (u_char)saddr.sa_data[2];     /* fill address */
    ip->byte2 = (u_char)saddr.sa_data[3];
    ip->byte3 = (u_char)saddr.sa_data[4];  
    ip->byte4 = (u_char)saddr.sa_data[5];  
  }
  return(msgsock);                     /* return socket */
}

/* ---------------------------------------------------------------- */

int TCPIP_AddressCheck(IP_Address *req,IP_Address *list)
{
  int i;
  int r=FALSE;

  if (list == NULL) return(TRUE);
  if ((req->byte1==127) && (req->byte2==0) &&  /* localhost = 127.0.0.1 */
      (req->byte3==0) && (req->byte4==1)) return(TRUE);

  for (i=0; list[i].byte1 != (u_char)0; i++) { /* check list */
    if (req->byte1 != list[i].byte1) continue;
    if (req->byte2 != list[i].byte2) continue;
    if (req->byte3 != list[i].byte3) continue;
    if (list[i].byte4 != 0) {          /* specific host */
      if (req->byte4 != list[i].byte4) continue;
    }
    strcpy(req->hostname,list[i].hostname);
    r = TRUE;
    break;
  }
   
  return r;
}  

/* ---------------------------------------------------------------- */

int TCPIP_CreateClientSocket(const char *hostname,u_short port,int *err)
{
  int    sock,result,protocol=0;
  struct sockaddr_in saddr;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%d)\n",PREFUN,hostname,port);
#endif

  *err = 0;
  memset((void*)&saddr,0,sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons((u_short)port);

  result = tcpip_mutex_lock(5);
  if (*err == 0) { struct  protoent *proto;
    proto = getprotobyname("tcp");     /* get protocol by name */
    if (proto == NULL) *err = E_tcpip_protocol;
    else               protocol = proto->p_proto;
  }
  if (*err == 0) { struct  hostent *hostp;
    hostp = gethostbyname(hostname);   /* get host IP address */
    if (hostp == NULL) {
      *err = E_tcpip_hostbyname;
    } else {
      memcpy(&saddr.sin_addr,hostp->h_addr_list[0],hostp->h_length);
    }
  }
  if (!result) tcpip_mutex_unlock();

  if (*err == 0) { int i;
    for (i=0; i<3; i++) {              /* 3 connection attempts */
      if (i > 0) msleep(300);
      if ((sock = socket(PF_INET,SOCK_STREAM,protocol)) < 0) {
#if (DEBUG > 0)
        fprintf(stderr,"%s: socket() failed (%s)\n",PREFUN,strerror(errno));
#endif
        *err = E_tcpip_socket;         /* create socket */
        break;
      }
      (void)fcntl(sock,F_SETFD,FD_CLOEXEC);  /* close-on-exec flag */
#ifdef SO_NOSIGPIPE                    /* 2017-07-03 */
      { int on=1;
        (void)setsockopt(sock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
      }
#endif
#if 0 /* LINUX default=5 (18 seconds) -- non-portable */
      { int to=9; socklen_t si=sizeof(to);
        int r = getsockopt(sock,IPPROTO_TCP,TCP_SYNCNT,&to,&si);
        printf("r=%d, to=%d, si=%d\n",r,to,si);
        (void)setsockopt(sock,IPPROTO_TCP,TCP_SYNCNT,&to,sizeof(to));
      }
#endif
      if (connect(sock,(struct sockaddr *)&saddr,sizeof(saddr)) < 0) {
#if (DEBUG > 0)
        fprintf(stderr,"%s: connect() failed (%s)\n",PREFUN,strerror(errno));
#endif
        (void)close(sock);
        *err = E_tcpip_connect; if (errno != EINTR) break;
      } else { 
        *err = 0; 
        break; 
      }
    } /* endfor(i) */
  } /* endif(err) */

#if (DEBUG > 1)
  if (*err == 0) fprintf(stderr,"%s: sock=%d\n",PREFUN,sock);
#endif

  return((*err) ? -1 : sock);
}

/* ---------------------------------------------------------------- */

void TCPIP_KeepAlive(int sock)         /* v0340 */
{
#ifdef SO_KEEPALIVE
  int on=1;
  (void)setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,&on,sizeof(on));
#endif
}

/* ---------------------------------------------------------------- */

int TCPIP_Send(int sock,const char *message)
{
#if (DEBUG > 2)
  fprintf(stderr,"%s(%s)\n",PREFUN,message);
#endif

  if (send(sock,message,strlen(message),MSG_NOSIGNAL) < 0) return E_tcpip_send;

  return 0;
}

/* --- */

int TCPIP_Send2(int sock,const char *message,int len)  /* v0339 */
{
#if (DEBUG > 2)
  fprintf(stderr,"%s(%s)\n",PREFUN,message);
#endif

  if (send(sock,message,len,MSG_NOSIGNAL) < 0) return E_tcpip_send;

  return 0;
}

/* ---------------------------------------------------------------- */

int TCPIP_Receive(int sock,char *answer,int buflen) 
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d,%p,%d)\n",PREFUN,sock,answer,buflen);
#endif

  (void)memset(answer,0,buflen);

  ssize_t rval = recv(sock,answer,buflen-1,0); /* buffer-overflow safe */
  if (rval == -1) return(E_tcpip_recv);
  if (rval == 0)  return(E_tcpip_nodata);

  return 0;
}

/* ---------------------------------------------------------------- */

#if 0 // unused
int TCPIP_Receive2(int sock,char* answer,int buflen)
{
  int err;

  err = TCPIP_Receive3(sock,answer,buflen,5);

  return err;
}
#endif

/* ---------------------------------------------------------------- */

int TCPIP_Receive3(int sock,char* answer,int buflen,int timeout)
{
  int  i=0,err;
  char c;

  do {
    err = TCPIP_ReadByte(sock,&c,timeout); if (err) break;
    if ((c == (char)0x0a) || (c == (char)0x0d)) break; /* terminator */
    // if (c == (char)0x00) break; 
    answer[i] = c; i++;
  } while (i < buflen);
  if (i >= buflen) return E_tcpip_overflow; /* bug-fix 2017-07-03 */
  answer[i] = '\0';

  return (i==0) ? E_tcpip_timeout : 0;
}

/* ---------------------------------------------------------------- */

int TCPIP_ReadByte(int sock,char* c,int timeout)
{
  int            r;
  fd_set         rfd;
  struct timeval tout;

  do {                                /* read byte */
    FD_ZERO(&rfd);
    FD_SET(sock,&rfd);
    if (timeout > 1000) { tout.tv_sec=0;       tout.tv_usec=timeout; } 
    else                { tout.tv_sec=timeout; tout.tv_usec=0; }
    r = select(sock+1,&rfd,NULL,NULL,&tout);
    if (FD_ISSET(sock,&rfd)) break;
  } while (r > 0);
  if (r <  0) return(E_tcpip_nodata);
  if (r == 0) return(E_tcpip_timeout); /* timeout */

  r = recv(sock,c,1,0);                /* read character */

  return((r>0) ? 0 : E_tcpip_recv);
}

/* ---------------------------------------------------------------- */

ssize_t TCPIP_Recv(int sock,char* p,int nbytes,int timeout)
{
  ssize_t        r;
  fd_set         rfd;
  struct timeval tout;

  if (timeout > 0) { 
    do {
      FD_ZERO(&rfd);
      FD_SET(sock,&rfd);
      tout.tv_sec = timeout; tout.tv_usec = 0;
      r = select(sock+1,&rfd,NULL,NULL,&tout);
      if (FD_ISSET(sock,&rfd)) break;
    } while (r > 0);
    if (r <  0) return -1;
    if (r == 0) return  0;
  }

  r = recv(sock,p,nbytes,0);

  return r;
}

/* ---------------------------------------------------------------- */

int TCPIP_SingleCommand(const char* hostname,u_short port,const char* command)
{
  int  sock,err;
  char answer[1024];

  sock = TCPIP_CreateClientSocket(hostname,port,&err);
  if (sock == -1) return(err);

  if ((err=TCPIP_Send(sock,command)) != 0) { (void)close(sock);  return(err); }

  err = TCPIP_Receive(sock,answer,sizeof(answer));
  (void)close(sock);

  return(err);
}

/* ---------------------------------------------------------------- */

int TCPIP_SingleRequest(const char* hostname,u_short port,
                        const char* command,char* answer,int buflen)
{
  int  sock,err;

  sock = TCPIP_CreateClientSocket(hostname,port,&err);
  if (sock == -1) return(err);

  if ((err=TCPIP_Send(sock,command)) == 0) { 
    err = TCPIP_Receive(sock,answer,buflen);
  }
  (void)close(sock);

  return(err);
}

/* ---------------------------------------------------------------- */
 
int TCPIP_Request(int sock,const char* command,char* answer,int buflen)
{
  int  err;
 
  if ((err=TCPIP_Send(sock,command)) == 0) {
    err = TCPIP_Receive(sock,answer,buflen);
  }
 
  return(err);
}
 
/* ---------------------------------------------------------------- */

#if 0 // unused
int TCPIP_Request2(int sock,const char* command,char* answer,int buflen)
{
  int  err;

  if ((err=TCPIP_Send(sock,command)) == 0) {
    err = TCPIP_Receive2(sock,answer,buflen);
  }

  return(err);
}
#endif

/* ---------------------------------------------------------------- */

int TCPIP_Request3(int sock,const char* cmd,char* ans,int len,int tout)
{
  int  err;

  if ((err=TCPIP_Send(sock,cmd)) == 0) {
    err = TCPIP_Receive3(sock,ans,len,tout);
  }

  return(err);
}

/* ---------------------------------------------------------------- */

int TCPIP_StartupServerThread(TCPIP_ServerInfo* info,int timeout)
{
  long err;
  const int delta=200;

  info->running = FALSE;

  if (tcpip_thread_create(tcpip_generic_server,(void*)info,&info->tid)) {
    return(E_tcpip_thread);
  }

  while (timeout > 0) {
    msleep(delta); timeout -= delta;
    if (info->running) return(0);
  }
  if (pthread_kill(info->tid,0) != 0) {     /* thread not running */
    pthread_join(info->tid,(void*)&err);
  } else err = E_tcpip_timeout;
  
  return (int)err;
}

/* ---------------------------------------------------------------- */

static void* tcpip_generic_server(void* param)
{
  int               err,rval;
  char              command[256],answer[8192];
  int               done=FALSE;
  int               sock,msgsock;
  IP_Address        req_address;
  TCPIP_ServerInfo *info;

  info = (TCPIP_ServerInfo*)param;
 
  sock = TCPIP_CreateServerSocket(info->port,&err);  /* open socket */
  if (sock == -1) { char buffer[1024];               /* failed */
    sprintf(buffer,"TCPIP_CreateServerSocket()=%d, err=%d",sock,err);
    tdebug(info->name,buffer);
#if (DEBUG > 0)
    fprintf(stderr,"%s: '%s'\n",__FILE__,buffer);
#endif
    return((void*)(long)err);
  }
  info->running = TRUE;                /* set 'running' flag */

  while (!done) {                      /* main loop  */
    msgsock = TCPIP_ServerAccept(sock,&req_address);
    if (TCPIP_AddressCheck(&req_address,info->oklist)) { /* accept */
#if (DEBUG > 1)
      char text[128];
      sprintf(text,"connection accepted from %u.%u.%u.%u",
        req_address.byte1,req_address.byte2,
        req_address.byte3,req_address.byte4);
      tdebug(info->name,text);
#endif
#ifdef SO_NOSIGPIPE                    /* 2017-07-05 */
      { int on=1;
        (void)setsockopt(msgsock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
      }
#endif
      do {
        (void)memset(command,0,sizeof(command));    /* clear buffer */
        rval = recv(msgsock,command,sizeof(command)-1,0); /* read command */
        if (rval <= 0) continue;       /* buffer-overflow safe */
        if (!strcmp(command,TCPIP_FINITO_CMD)) { /* 'terminate' server */
          done = TRUE;
          strcpy(answer,command);
        } else {
          done = info->handler(command,answer); /* user-supplied function */
        }
        send(msgsock,answer,strlen(answer),MSG_NOSIGNAL);
        if (done) { close(msgsock); break; }    /* NECESSARY */
      } while (rval > 0);              /* loop while there's something */
    } else { char text[128],title[256],buf[128]; /* reject */
      sprintf(text,"%s\nconnection rejected from %u.%u.%u.%u",
        get_local_datestr(buf,time(NULL)),
        req_address.byte1,req_address.byte2,
        req_address.byte3,req_address.byte4);
      sprintf(title,"%s: Warning",info->name);
#if (DEBUG > 0)
      fprintf(stderr,"%s: %s\n",__FILE__,text);
#endif
      tdebug(title,text);
    } /* endif(AddressCheck) */
    (void)close(msgsock);
  } /* while(!done) */

  (void)close(sock);
  info->running = FALSE;

  return((void*)0);                    /* terminate thread */
}

/* ---------------------------------------------------------------- */

int TCPIP_TerminateServerThread(TCPIP_ServerInfo* info,int timeout)
{       
  long err=0;                          /* 64-bit */
  const int delta=100;
  char hostname[256];

#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d)\n",PREFUN,info,timeout);
#endif
  assert(sizeof(long) == sizeof(void*));
  
#if 0 /* fixed to 'localhost' */
  (void)gethostname(hostname,sizeof(hostname)); /* get local hostname */
#else
  strcpy(hostname,"localhost");
#endif
#if (DEBUG > 1)
  fprintf(stderr,"%s(): hostname=%s, port=%d\n",PREFUN,hostname,info->port);
#endif
  TCPIP_SendFinito(hostname,info->port);    /* send TCPIP_FINITO_CMD */
  while (info->running) {
    if ((timeout-=delta) < 0) break;
    msleep(delta);
  }
  if (pthread_kill(info->tid,0) != 0) {     /* thread not running */
    pthread_join(info->tid,(void*)&err);
  } else err = E_tcpip_timeout;

  if (info->oklist) free((void*)info->oklist);

  return (int)err;
}

/* ---------------------------------------------------------------- */

int TCPIP_SendFinito(const char* host,u_short port)
{
  int  sock,err;
  char answer[1024];

  sock = TCPIP_CreateClientSocket(host,port,&err);
  if (err) return FALSE;

  (void)TCPIP_Send(sock,TCPIP_FINITO_CMD);
  (void)TCPIP_Receive(sock,answer,sizeof(answer));
  msleep(50);
  (void)TCPIP_Send(sock,TCPIP_FINITO_CMD);
  (void)close(sock);

  return TRUE;
}

/* ---------------------------------------------------------------- */

#define LIST_STEP  32                  /* must be at least 2 */

IP_Address* TCPIP_LoadOkList(char* file)
{
  int        i,n=1,nmax=LIST_STEP,b1,b2,b3,b4;
  char       buffer[1024],name[128];
  FILE       *fp;
  IP_Address *list;
  struct hostent *hostp;

  list = (IP_Address*)calloc(nmax,sizeof(IP_Address)); /* alloc. empty list */
  (void)gethostname(name,sizeof(name));      /* get local hostname */
  hostp = gethostbyname(name);
  if (hostp == NULL) {
    sprintf(buffer,"cannot resolve host '%s'",name);
    tdebug(PREFUN,buffer);
  } else {
    list[0].byte1 = (u_char)hostp->h_addr_list[0][0]; /* add local host */
    list[0].byte2 = (u_char)hostp->h_addr_list[0][1]; /* to list */
    list[0].byte3 = (u_char)hostp->h_addr_list[0][2];
    if (file == NULL) list[0].byte4 = (u_char)0;      /* 'C' class network */
    else              list[0].byte4 = (u_char)hostp->h_addr_list[0][3];
    strcpy(list[0].hostname,name);
  }
  if (file == NULL) return(list);      /* no file */

  fp = fopen(file,"r"); 
  if (fp != NULL) {                    /* file exists */
    while (fgets(buffer,sizeof(buffer)-1,fp) != NULL) {
      if (buffer[0] == ';') continue;
      i = sscanf(buffer,"%d.%d.%d.%d %s",&b1,&b2,&b3,&b4,name);
      if (i < 5) continue;
      list[n].byte1 = (u_char)b1;
      list[n].byte2 = (u_char)b2;
      list[n].byte3 = (u_char)b3;
      list[n].byte4 = (u_char)b4;
      strcpy(list[n].hostname,name);
      n++;    
      if (n == nmax) {                 /* increase list size */
        nmax += LIST_STEP;
        list = (IP_Address*)realloc(list,nmax*sizeof(IP_Address));
      }
    } /* endwhile(fgets) */
    fclose(fp);
  }

  return(list);
}

#undef LIST_STEP

/* ---------------------------------------------------------------- */

char* TCPIP_HostName(char *hostdomain)
{
#if 0 /* fixed to 'localhost' */
  char           hostname[256];
  struct hostent *hostp;
#endif
  static char    host[256];

  if (hostdomain == NULL) hostdomain = host;  /* WARNING: not MT-safe */
#if 0 /* fixed to 'localhost' */
  if (gethostname(hostname,sizeof(hostname)) == -1) {
    strcpy(hostdomain,"unknown");
    return hostdomain;
  }
  hostp = gethostbyname(hostname);
  if (hostp == NULL) strcpy(hostdomain,"unknown");
  else               strcpy(hostdomain,hostp->h_name);
#else
  strcpy(hostdomain,"localhost");
#endif

  return(hostdomain);
}

/* ---------------------------------------------------------------- */

static int tcpip_thread_create(void*(*start_routine)(void*),
                               void* arg,pthread_t* tid)
{
  int            err;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr,PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
  err = pthread_create(tid,&attr,start_routine,arg);
  pthread_attr_destroy(&attr);

  return(err);
}

/* --------------------------------------------------------------- */
/* UDP functions */
/* --------------------------------------------------------------- */

int UDP_CreateServerSocket(const char* host,u_short port,int *err)
{
  char               hostname[256];
  int                sock,protocol=0,result;
  struct sockaddr_in saddr;

  saddr.sin_family = AF_INET;          /* bind socket name */
  saddr.sin_port = htons(port);
  *err = 0;

  result = tcpip_mutex_lock(5); 
  if (*err == 0) { struct protoent *proto;
    proto = getprotobyname("udp");       /* get protocol by name */
    if (proto == NULL)  *err = E_tcpip_protocol;
    else protocol =  proto->p_proto;
  }
  if (*err == 0) {
    if (host == NULL) {                /* use local hostname */
      if (gethostname(hostname,sizeof(hostname))) *err = E_tcpip_hostname;
    } else {
      assert(strlen(host) < 255);
      strcpy(hostname,host);           /* use supplied host */
    }
  }
  if (*err == 0) { struct hostent *hostp;
    hostp = gethostbyname(hostname);
    if (hostp == NULL) *err = E_tcpip_hostbyname;
    else {
#if 0 /* 1st hostname only */
      memcpy(&saddr.sin_addr,hostp->h_addr_list[0],hostp->h_length);
#else
      saddr.sin_addr.s_addr = INADDR_ANY;  /* allow any interface */
#endif
    }
  }
  if (!result) tcpip_mutex_unlock();

  if (*err == 0) {
    if ((sock = socket(PF_INET,SOCK_DGRAM,protocol)) == -1) {
      *err = E_tcpip_socket;             /* create socket */
    } else { int on=1;
#if 0 /* no 'close-on-exec' and no 'non-blocking' */
      (void)fcntl(sock,F_SETFD,FD_CLOEXEC);     /* close-on-exec flag */
      (void)fcntl(sock,F_SETFL,O_NONBLOCK);     /* non-blocking */
#endif
      (void)setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
    }
  }
  if (*err == 0) {
    if (bind(sock,(struct sockaddr *)&saddr,sizeof(saddr)) == -1) {
      (void)close(sock);
      *err = E_tcpip_bind;
#if (DEBUG > 0)
      fprintf(stderr,"%s(): bind() failed, errno=%d\n",PREFUN,errno);
#endif
    }
  }

  return((*err) ? -1 : sock);
}

/* --------------------------------------------------------------- */

int UDP_SingleCommand(const char *host,u_short port,const char* cmd)
{
  int                  sock,err=0,protocol=0,result;
  struct  sockaddr_in  saddr;

  result = tcpip_mutex_lock(5);
  if (!err) { struct protoent *proto;
    if ((proto = getprotobyname("udp")) == NULL) err = E_tcpip_protocol;
    else protocol = proto->p_proto;
  }
  if (!err) { struct hostent *hostp;
    if ((hostp = gethostbyname(host)) == NULL) err = E_tcpip_hostbyname;
    else memcpy(&saddr.sin_addr,hostp->h_addr_list[0],hostp->h_length);
  }
  if (!result) tcpip_mutex_unlock();

  if (!err) {
    if ((sock = socket(PF_INET,SOCK_DGRAM,protocol)) == -1) {
      err = E_tcpip_socket;
    }
  }
  if (!err) {
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    if (sendto(sock,cmd,strlen(cmd),0,(struct sockaddr *)&saddr,
               sizeof(saddr)) < 0) {
#if (DEBUG > 0)
      fprintf(stderr,"%s: error writing to UDP socket (%s:%u)\n",__FILE__,
              host,port);
#endif
      err = E_tcpip_send;
    }
    close(sock);
  }

  return err;
}

/* --------------------------------------------------------------- */

int UDP_SingleRequest(const char *host,u_short port,const char* cmd, 
                      char* answer,int len)
{
  socklen_t           size;
  ssize_t             rval;
  int                 sock,err=0,protocol=0,result;
  struct  sockaddr_in saddr;

  result = tcpip_mutex_lock(5);
  if (!err) { struct protoent *proto;
    if ((proto = getprotobyname("udp")) == NULL) err = E_tcpip_protocol;
    else protocol = proto->p_proto;
  }
  if (!err) { struct hostent *hostp;
    if ((hostp = gethostbyname(host)) == NULL) err = E_tcpip_hostbyname;
    else memcpy(&saddr.sin_addr,hostp->h_addr_list[0],hostp->h_length);
  }
  if (!result) tcpip_mutex_unlock();

  if (!err) {
    if ((sock = socket(PF_INET,SOCK_DGRAM,protocol)) == -1) {
      err = E_tcpip_socket;
    }
  }
  if (!err) {
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    if (sendto(sock,cmd,strlen(cmd),0,(struct sockaddr *)&saddr,
               sizeof(saddr)) < 0) {
#if (DEBUG > 0)
      fprintf(stderr,"%s: error writing to UDP socket (%s:%u)\n",__FILE__,
              host,port);
#endif
      err = E_tcpip_send;
    }
    if (!err) {                          /* read answer */
      memset(answer,0,len);              /* clear buffer */
      size = sizeof(saddr);              /* buffer-overflow safe */
      rval = recvfrom(sock,answer,len,0,(struct sockaddr *)&saddr,&size);
      if (rval == -1) err = E_tcpip_recv;
      if (rval == 0)  err = E_tcpip_nodata;
    }
    close(sock);
  }

  return err;
}

/* --------------------------------------------------------------- */
/* --------------------------------------------------------------- */
/* --------------------------------------------------------------- */

