/* ----------------------------------------------------------------
 *
 * getimages.c
 *
 * receive guider images via TCP/IP
 * 
 * ---------------------------------------------------------------- */

/* DEFINEs -------------------------------------------------------- */

#define DEBUG           1              /* debug level */

#define FITSLEN         80
#define FITSLINES       36
#define FITSBLOCK       (FITSLEN*FITSLINES)
#define KEY_LEN         8              /* length if keyword */
#define RIGHT_MAR       30             /* right margin of value */
#define SLASH_POS       39             /* start of comment field */

#define PREFUN          __func__

/* INCLUDEs ------------------------------------------------------- */

#include <stdio.h>                     /* fprintf() */
#include <stdlib.h>                    /* exit() */
#include <string.h>                    /* strcmp(),strlen(),memset() */
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <sys/socket.h>                /* socket() */
#include <netdb.h>                     /* getprotobyname() */
#include <netinet/in.h>                /* sockaddr_in */

/* ---------------------------------------------------------------- */

static char* extend(char *s, char c, int l)
{
  int i;

  i = strlen(s);
  while (i < l) {
    s[i++] = c;
  }
  s[i] = '\0';

  return(s);
}

/* ---------------------------------------------------------------- */

char* fitsg(char *line,const char* key,const char* number,const char *comment)
{
  strcpy(line,key);
  extend(line,' ',KEY_LEN);            /* fill keyword */
  strcat(line,"= ");
  extend(line,' ',RIGHT_MAR-strlen(number));  /* right margin */
  strcat(line,number);
  if (comment) {
    extend(line,' ',SLASH_POS);        /* append comment */
    strcat(line,"/ ");
    strcat(line,comment);
  }
  return(line);
}

/* ---------------------------------------------------------------- */

char* fitsi(char *line,const char* key,int value,const char *comment)
{
  char number[32];

  sprintf(number,"%d",value);          /* create number string */
  fitsg(line,key,number,comment);

  return(line);
}

/* ---------------------------------------------------------------- */

char* fitsd(char *line,const char* key,double value,int dp,const char *comment)
{
  char number[32];

  sprintf(number,"%.*f",dp,value);     /* create number string */
  fitsg(line,key,number,comment);

  return(line);
}

/* ---------------------------------------------------------------- */

static void write_line(FILE* fp,char* line)
{
  char buf[FITSLEN+1];

  strcpy(buf,line);
  fwrite((void*)extend(buf,' ',FITSLEN),sizeof(char),FITSLEN,fp);
}

/* ---------------------------------------------------------------- */

/* Note: assumes 'data' is big-endian */

static int write_fits(u_short* data,int dimx,int dimy,int frame)
{
  int  i,n=0,nbytes=FITSBLOCK,tail;
  char file[512],buf[128];
  FILE *fp;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%d,%d,%d)\n",PREFUN,data,dimx,dimy,frame);
#endif

  sprintf(file,"/tmp/frame%05d.fits",frame);
#if (DEBUG > 0)
  fprintf(stderr,"%s\n",file);
#endif
  fp = fopen(file,"wb"); if (!fp) return -1;
  //             123456789012345678901234567890
  write_line(fp,"SIMPLE  =                    T"); n++;
  write_line(fp,"BITPIX  =                   16"); n++;
  write_line(fp,"NAXIS   =                    2"); n++;
  sprintf(buf,  "NAXIS1  =                 %4d",dimx);
  write_line(fp,buf); n++; 
  sprintf(buf,  "NAXIS2  =                 %4d",dimy);
  write_line(fp,buf); n++; 
  write_line(fp,"END                           "); n++;
  for (i=n; i<FITSLINES; i++) {
    write_line(fp," "); n++;
  }
  assert(n == FITSLINES);

  fwrite((void*)data,sizeof(short),dimx*dimy,fp);
  nbytes += dimx*dimy*sizeof(short);

  tail = (FITSBLOCK - (nbytes % FITSBLOCK)) % FITSBLOCK;
  if (tail) { char tailbuf[FITSBLOCK];  /* pad if necessary */
    (void)memset((void*)tailbuf,0,tail);
    fwrite((void*)tailbuf,sizeof(char),tail,fp);
  }
  fclose(fp);

  return 0;
}

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

int main(int argc,char **argv)
{
  int     rval,i,n,nbytes,sock,msgsock,done=0;
  u_int   size;
  u_short port=5700,*data;
  char    host[128],buffer[1024];
  int     dimx=0,dimy=0,frame=0,shflag=0;

  struct  protoent     *proto;
  struct  hostent      *hostp;
  struct  sockaddr_in  saddr;
  struct  sockaddr     sadr;

  if (argc > 1) port = atoi(argv[1]);

  (void)gethostname(host,sizeof(host));   /* my host name */
#if (DEBUG > 0)
  fprintf(stderr,"host='%s', port=%d\n",host,port);
#endif

  proto = getprotobyname("tcp");
  if (proto == NULL) {
    fprintf(stderr,"%s: getprotobyname(tcp) failed\n",argv[0]);
    exit(2);
  }
  hostp = gethostbyname(host);
  if (hostp == NULL) {
    fprintf(stderr,"%s: cannot resolve host '%s'\n",argv[0],host);
    exit(3);
  }
#if (DEBUG > 0)
  fprintf(stderr,"host='%s' (%d.%d.%d.%d)\n",hostp->h_name,
    *(u_char*)(hostp->h_addr_list[0]+0),
    *(u_char*)(hostp->h_addr_list[0]+1),
    *(u_char*)(hostp->h_addr_list[0]+2),
    *(u_char*)(hostp->h_addr_list[0]+3)
  );
#endif

  if ((sock = socket(AF_INET, SOCK_STREAM, proto->p_proto)) < 0) {
    fprintf(stderr,"%s - %s\n",argv[0],"cannot create stream socket");
    exit(4);
  }
  i = 1;
  if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char*)&i,sizeof(i)) < 0) {
    close(sock);
    fprintf(stderr,"%s - %s\n",argv[0],"cannot setsockopt()");
    exit(5);
  }

  saddr.sin_family = AF_INET;
#if 0 /* only first interface */
  memcpy(&saddr.sin_addr, hostp->h_addr_list[0], hostp->h_length);
#else
  saddr.sin_addr.s_addr = INADDR_ANY;  /* allow any interface */
#endif
  saddr.sin_port = htons(port);

  if (bind(sock,(struct sockaddr *)&saddr,sizeof(saddr)) == -1) {
    (void)close(sock);
    fprintf(stderr,"%s: bind() failed, errno=%d\n",argv[0],errno);
    return(-1);
  }

  if (listen(sock,SOMAXCONN) == -1) {  /* listen to socket */
    (void)close(sock);
    fprintf(stderr,"%s: listen() failed, errno=%d\n",argv[0],errno);
    return(-1);
  }

  assert(sizeof(int) == 4);
  while (!done) {                      /* main loop  */
    size = sizeof(sadr);
    msgsock = accept(sock,&sadr,&size);
#if (DEBUG > 0)
    fprintf(stderr,"connection accepted from %u.%u.%u.%u\n",
        (u_char)sadr.sa_data[2],(u_char)sadr.sa_data[3],
        (u_char)sadr.sa_data[4],(u_char)sadr.sa_data[5]);
#endif
    do {
      rval = recv(msgsock,&dimx,sizeof(dimx),0);
      if (rval <= 0) continue;
      assert(sizeof(dimx) == 4);
      if (dimx > 2048) { char buffer[2881]; int flag=0; ssize_t total=4;
        memcpy(buffer,&dimx,4);
        rval = recv(msgsock,buffer+4,80-4,0);
        while (rval > 0) {
          total += rval;
          if (!flag) { 
            buffer[80] = '\0'; printf("%s\n",buffer);
            if (!strncmp(buffer,"END     ",8)) flag=1; 
          }
          rval = recv(msgsock,buffer,80,0);
        }
        printf("total=%ld\n",total);
        continue;
      }
      swap4(&dimx,1); printf("dimx=%d\n",dimx);
      rval = recv(msgsock,&dimy,sizeof(dimy),0);
      if (rval <= 0) continue;
      swap4(&dimy,1); printf("dimy=%d\n",dimy);
      rval = recv(msgsock,&frame,sizeof(frame),0);
      if (rval <= 0) continue;
      swap4(&frame,1); printf("frame=%d\n",frame);
      rval = recv(msgsock,&shflag,sizeof(shflag),0);
      if (rval <= 0) continue;
      swap4(&shflag,1); printf("shflag=%d\n",shflag);
      rval = recv(msgsock,(char*)buffer,24,0);
      nbytes = dimx*dimy*sizeof(u_short);
      data = (u_short*)malloc(nbytes);
      for (i=0,n=0; i<nbytes/42; i++) {
        rval = recv(msgsock,(char*)data+n,nbytes-n,0);
        if (rval <= 0) break;
        printf(".");
        n += rval; if (n >= nbytes) break;
      }
      printf("\n%d\n",n);
      write_fits(data,dimx,dimy,frame); 
      free((void*)data);
    } while (rval > 0);                /* loop while there's something */
    printf("rval=%d\n",rval);
    (void)close(msgsock);
  } /* while(!done) */

  (void)close(sock);

  return(0);
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

