/* ----------------------------------------------------------------
 *
 * tcpip.h
 *
 * Project: TCP/IP library (OCIW, Pasadenda, CA)
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_TCPIP_H
#define INCLUDE_TCPIP_H

/* INCLUDEs ------------------------------------------------------- */

#include <sys/types.h>
#include <sys/socket.h>

/* TYPEDEFs ------------------------------------------------------- */

typedef struct ip_address_tag {
  u_char byte1,byte2,byte3,byte4;
  char   hostname[128];
} IP_Address;

typedef int (*TCPIP_HandlerFunction)(char*,char*);
 
typedef struct tcpip_server_info_tag {
  char*                 name;
  u_short               port;
  int                   running;
  pthread_t             tid;
  TCPIP_HandlerFunction handler;
  IP_Address*           oklist;
} TCPIP_ServerInfo;

/* DEFINEs -------------------------------------------------------- */

#define TRUE              1
#define FALSE             0
#define TCPIP_FINITO_CMD  "QUITfinito"
#define TCPIP_MAXCONN     SOMAXCONN

enum tcpip_error_codes {
  E_tcpip=1000,
  E_tcpip_protocol,
  E_tcpip_hostname,
  E_tcpip_hostbyname,
  E_tcpip_socket,
  E_tcpip_bind,
  E_tcpip_listen,
  E_tcpip_connect,
  E_tcpip_send,
  E_tcpip_recv,
  E_tcpip_nodata,
  E_tcpip_thread,
  E_tcpip_timeout,
  E_tcpip_overflow,
  E_tcpip_last
};

#ifdef IS_TCPIP_C
const char* tcpip_errstr[] = {
  "tcpip: generic error",
  "tcpip: getprotobyname() failed",
  "tcpip: gethostname() failed",
  "tcpip: gethostbyname() failed",
  "tcpip: socket() failed",
  "tcpip: bind() failed",
  "tcpip: listen() failed",
  "tcpip: connect() failed",
  "tcpip: send() failed",
  "tcpip: recv() failed",
  "tcpip: recv() return 0 bytes",
  "tcpip: thread_detach(tcpip_generic_server) failed",
  "tcpip: timed out",
  "tcpip: buffer overflow",
  "tcpip: last error"
};
#else
extern const char* tcpip_errstr[];
#endif

/* function prototype(s) ------------------------------------------ */

int  TCPIP_CreateServerSocket    (u_short,int*);
int  TCPIP_ServerAccept          (int,IP_Address*);
int  TCPIP_AddressCheck          (IP_Address*,IP_Address*);

int  TCPIP_CreateClientSocket    (const char*,u_short,int*);
int  TCPIP_Send                  (int,const char*);
int  TCPIP_Receive               (int,char*,int);
int  TCPIP_Receive2              (int,char*,int);
int  TCPIP_Receive3              (int,char*,int,int);
int  TCPIP_ReadByte              (int,char*,int);
ssize_t TCPIP_Recv(int sock,char* p,int nbytes,int timeout);
int  TCPIP_ReceiveFromServer     (int,char*,int);
int  TCPIP_SingleCommand         (const char*,u_short,const char*);
int  TCPIP_SingleRequest         (const char*,u_short,const char*,char*,int);
int  TCPIP_Request               (int,const char*,char*,int);
int  TCPIP_Request2              (int,const char*,char*,int);
int  TCPIP_Request3              (int,const char*,char*,int,int);

int  TCPIP_StartupServerThread   (TCPIP_ServerInfo*,int);
int  TCPIP_TerminateServerThread (TCPIP_ServerInfo*,int);
int  TCPIP_SendFinito            (const char*,u_short);

IP_Address* TCPIP_LoadOkList     (char*);
char*       TCPIP_HostName       (char*);

/* ---------------------------------------------------------------- */

int UDP_CreateServerSocket (const char*,u_short,int*);
int UDP_SingleCommand      (const char*,u_short,const char*);
int UDP_SingleRequest      (const char*,u_short,const char*,char*,int);

/* ---------------------------------------------------------------- */

#endif /* INCLUDE_TCPIP_H */

/* ---------------------------------------------------------------- */ 
/* ---------------------------------------------------------------- */ 
/* ---------------------------------------------------------------- */ 

