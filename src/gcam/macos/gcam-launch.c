/* ----------------------------------------------------------------
 *
 * gcam-launch.c
 *
 * Executable of the macOS application bundle (CFBundleExecutable);
 * starts the 'gcamzwo' that sits next to it in Contents/MacOS.
 *
 * - The configuration file follows from the name of the bundle:
 *   "gcam-pfs-sv.app" loads "pfs-sv.ini" from $GCAMZWOINI (default
 *   /usr/local/etc/gcam).  A site makes a guider by copying the
 *   bundle under a new name; the bundle as shipped, "gcamzwo.app",
 *   loads no file.
 * - gcamzwo is an X11 client.  XQuartz publishes DISPLAY into the
 *   user's launchd domain from its own agent, so an application
 *   opened early in a session has to wait for it.
 * - An application has no terminal: stdout/stderr are appended to
 *   ~/Library/Logs/<bundle>.log and errors are shown in an alert.
 * - GCAMZWOPATH defaults to ~/gcam, because gcamzwo's own default
 *   (/opt/gcamzwo) does not exist on a Mac unless somebody made it.
 *
 * Arguments are handed on to gcamzwo after the "-f", so they
 * override the file:  open -a gcam-pfs-sv --args -t 0
 *
 * This is a program and not a shell script because LaunchServices
 * does not open a bundle whose executable is a script on every Mac
 * (error -10669).
 *
 * ---------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <mach-o/dyld.h>

/* ---------------------------------------------------------------- */

#define P_TITLE         "gcam-launch"

#define APP_NAME        "gcamzwo"      /* bundle as shipped, no .ini */
#define APP_PREFIX      "gcam-"        /* gcam-<name>.app -> <name>.ini */
#define APP_TAIL        ".app/Contents/MacOS"
#define DEF_INIPATH     "/usr/local/etc/gcam"
#define DISPLAY_WAIT    60             /* [s] */

/* ---------------------------------------------------------------- */

static char appname[NAME_MAX+1] = APP_NAME;
static int  has_tty;

/* ---------------------------------------------------------------- */

static void fatal(const char *fmt,...)
{
  char    msg[2*PATH_MAX];
  va_list ap;

  va_start(ap,fmt);
  vsnprintf(msg,sizeof(msg),fmt,ap);
  va_end(ap);
  fprintf(stderr,"%s: %s\n",P_TITLE,msg);
  fflush(stderr);

  if (!has_tty) {                      /* opened as an application */
    pid_t pid = fork();
    if (pid == 0) {
      execl("/usr/bin/osascript","osascript",
            "-e","on run argv",
            "-e","display alert (item 1 of argv) message (item 2 of argv)"
                 " as critical giving up after 300",
            "-e","end run",appname,msg,(char*)NULL);
      _exit(127);
    }
    if (pid > 0) (void)waitpid(pid,NULL,0);
  }
  exit(1);
}

/* ---------------------------------------------------------------- */

static int launchd_display(char *buf,int size)
{
  FILE *fp = popen("/bin/launchctl getenv DISPLAY","r");

  *buf = '\0';
  if (fp) {
    if (fgets(buf,size,fp) == NULL) *buf = '\0';
    (void)pclose(fp);
    buf[strcspn(buf,"\r\n")] = '\0';
  }
  return (*buf != '\0');
}

/* ---------------------------------------------------------------- */

int main(int argc,char **argv)
{
  int           i,n=0;
  char          dir[PATH_MAX],buf[PATH_MAX],bin[PATH_MAX+16];
  char          ini[2*PATH_MAX]="",tstr[32];
  char          *p,**args;
  const char    *home,*inipath;
  uint32_t      size=sizeof(buf);
  size_t        len,tail=strlen(APP_TAIL);
  time_t        now=time(NULL);
  struct passwd *pw;

  has_tty = isatty(STDERR_FILENO);

  /* <somewhere>/<bundle>.app/Contents/MacOS/gcam-launch */

  if ((_NSGetExecutablePath(buf,&size) != 0) || (realpath(buf,dir) == NULL)) {
    fatal("cannot locate my own executable");
  }
  if ((p=strrchr(dir,'/')) != NULL) *p = '\0';
  snprintf(bin,sizeof(bin),"%s/gcamzwo",dir);

  len = strlen(dir);
  if ((len > tail) && !strcmp(dir+len-tail,APP_TAIL)) {
    strcpy(buf,dir);
    buf[len-tail] = '\0';
    p = strrchr(buf,'/');
    snprintf(appname,sizeof(appname),"%s",(p) ? p+1 : buf);
  }

  /* home from the password file -- $HOME is inherited, and wrong
   * under sudo */

  pw = getpwuid(getuid());
  home = (pw && pw->pw_dir && *pw->pw_dir) ? pw->pw_dir : getenv("HOME");
  if (!home) home = "/tmp";

  if (!has_tty) {
    snprintf(buf,sizeof(buf),"%s/Library/Logs",home);
    (void)mkdir(buf,0755);
    snprintf(buf,sizeof(buf),"%s/Library/Logs/%s.log",home,appname);
    if (freopen(buf,"a",stdout)) (void)dup2(STDOUT_FILENO,STDERR_FILENO);
    setvbuf(stdout,NULL,_IOLBF,0);
  }
  strftime(tstr,sizeof(tstr),"%Y-%m-%d %H:%M:%S",localtime(&now));
  printf("=== %s starting %s ===\n",tstr,appname);

  /* configuration file */

  if (strcmp(appname,APP_NAME)) {
    p = appname;
    if (!strncmp(p,APP_PREFIX,strlen(APP_PREFIX))) p += strlen(APP_PREFIX);
    inipath = getenv("GCAMZWOINI");
    if (!inipath || !*inipath) inipath = DEF_INIPATH;
    snprintf(ini,sizeof(ini),"%s/%s.ini",inipath,p);
    if (access(ini,R_OK) != 0) {
      fatal("cannot read %s (%s)",ini,strerror(errno));
    }
    setenv("GCAMZWOINI",inipath,0);    /* "dspi <name>" looks there too */
  }

  if (!getenv("GCAMZWOPATH")) {        /* keep it short, see load_mask() */
    snprintf(buf,sizeof(buf),"%s/gcam",home);
    (void)mkdir(buf,0755);
    setenv("GCAMZWOPATH",buf,1);
  }

  /* X11 */

  p = getenv("DISPLAY");
  if (!p || !*p) {
    for (i=0; i<DISPLAY_WAIT; i++) {
      if (launchd_display(buf,sizeof(buf))) break;
      sleep(1);
    }
    if (i == DISPLAY_WAIT) {
      fatal("no DISPLAY after %d seconds -- is XQuartz installed?",
            DISPLAY_WAIT);
    }
    setenv("DISPLAY",buf,1);
  }
  printf("%s: DISPLAY=%s GCAMZWOPATH=%s%s%s\n",P_TITLE,getenv("DISPLAY"),
         getenv("GCAMZWOPATH"),(*ini) ? " -f " : "",ini);
  fflush(stdout);

  /* gcamzwo [-f <ini>] [arguments] */

  args = (char**)calloc(argc+3,sizeof(char*));
  if (!args) fatal("out of memory");
  args[n++] = "gcamzwo";
  if (*ini) { args[n++] = "-f"; args[n++] = ini; }
  for (i=1; i<argc; i++) args[n++] = argv[i];
  args[n] = NULL;

  execv(bin,args);
  fatal("cannot execute %s (%s)",bin,strerror(errno));

  return 1;
}

/* ---------------------------------------------------------------- */
