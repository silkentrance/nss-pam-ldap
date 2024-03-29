/*
   nslcd.c - ldap local connection daemon

   Copyright (C) 2006 West Consulting
   Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011 Arthur de Jong

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA
*/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif /* HAVE_GETOPT_H */
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#ifdef HAVE_GRP_H
#include <grp.h>
#endif /* HAVE_GRP_H */
#ifdef HAVE_NSS_H
#include <nss.h>
#endif /* HAVE_NSS_H */
#include <pthread.h>
#ifndef HAVE_GETOPT_LONG
#include "compat/getopt_long.h"
#endif /* not HAVE_GETOPT_LONG */
#include "compat/daemon.h"
#include <dlfcn.h>
#include <libgen.h>
#include <limits.h>

#include "nslcd.h"
#include "log.h"
#include "cfg.h"
#include "common.h"
#include "compat/attrs.h"
#include "compat/getpeercred.h"

/* buffer sizes for I/O */
#define READBUFFER_MINSIZE 32
#define READBUFFER_MAXSIZE 64
#define WRITEBUFFER_MINSIZE 64
#define WRITEBUFFER_MAXSIZE 1*1024*1024

/* flag to indictate if we are in debugging mode */
static int nslcd_debugging=0;

/* flag to indicate user requested the --check option */
static int nslcd_checkonly=0;

/* the exit flag to indicate that a signal was received */
static volatile int nslcd_exitsignal=0;

/* the server socket used for communication */
static int nslcd_serversocket=-1;

/* thread ids of all running threads */
static pthread_t *nslcd_threads;

/* if we don't have clearenv() we have to do this the hard way */
#ifndef HAVE_CLEARENV

/* the definition of the environment */
extern char **environ;

/* the environment we want to use */
static char *sane_environment[] = {
  "HOME=/",
  "TMPDIR=/tmp",
  "LDAPNOINIT=1",
  NULL
};

#endif /* not HAVE_CLEARENV */

/* display version information */
static void display_version(FILE *fp)
{
  fprintf(fp,"%s\n",PACKAGE_STRING);
  fprintf(fp,"Written by Luke Howard and Arthur de Jong.\n\n");
  fprintf(fp,"Copyright (C) 1997-2011 Luke Howard, Arthur de Jong and West Consulting\n"
             "This is free software; see the source for copying conditions.  There is NO\n"
             "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
}

/* display usage information */
static void display_usage(FILE *fp,const char *program_name)
{
  fprintf(fp,"Usage: %s [OPTION]...\n",program_name);
  fprintf(fp,"Name Service LDAP connection daemon.\n");
  fprintf(fp,"  -c, --check        check if the daemon already is running\n");
  fprintf(fp,"  -d, --debug        don't fork and print debugging to stderr\n");
  fprintf(fp,"      --help         display this help and exit\n");
  fprintf(fp,"      --version      output version information and exit\n");
  fprintf(fp,"\n"
             "Report bugs to <%s>.\n",PACKAGE_BUGREPORT);
}

/* the definition of options for getopt(). see getopt(2) */
static struct option const nslcd_options[] =
{
  { "check",       no_argument,       NULL, 'c' },
  { "debug",       no_argument,       NULL, 'd' },
  { "help",        no_argument,       NULL, 'h' },
  { "version",     no_argument,       NULL, 'V' },
  { NULL, 0, NULL, 0 }
};
#define NSLCD_OPTIONSTRING "cdhV"

/* parse command line options and save settings in struct  */
static void parse_cmdline(int argc,char *argv[])
{
  int optc;
  while ((optc=getopt_long(argc,argv,NSLCD_OPTIONSTRING,nslcd_options,NULL))!=-1)
  {
    switch (optc)
    {
    case 'c': /* -c, --check        check if the daemon already is running */
      nslcd_checkonly=1;
      break;
    case 'd': /* -d, --debug        don't fork and print debugging to stderr */
      nslcd_debugging++;
      log_setdefaultloglevel(LOG_DEBUG);
      break;
    case 'h': /*     --help         display this help and exit */
      display_usage(stdout,argv[0]);
      exit(EXIT_SUCCESS);
    case 'V': /*     --version      output version information and exit */
      display_version(stdout);
      exit(EXIT_SUCCESS);
    case ':': /* missing required parameter */
    case '?': /* unknown option character or extraneous parameter */
    default:
      fprintf(stderr,"Try '%s --help' for more information.\n",
              argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  /* check for remaining arguments */
  if (optind<argc)
  {
    fprintf(stderr,"%s: unrecognized option '%s'\n",
            argv[0],argv[optind]);
    fprintf(stderr,"Try '%s --help' for more information.\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }
}

/* get a name of a signal with a given signal number */
static const char *signame(int signum)
{
  switch (signum)
  {
  case SIGHUP:  return "SIGHUP";  /* Hangup detected */
  case SIGINT:  return "SIGINT";  /* Interrupt from keyboard */
  case SIGQUIT: return "SIGQUIT"; /* Quit from keyboard */
  case SIGILL:  return "SIGILL";  /* Illegal Instruction */
  case SIGABRT: return "SIGABRT"; /* Abort signal from abort(3) */
  case SIGFPE:  return "SIGFPE";  /* Floating point exception */
  case SIGKILL: return "SIGKILL"; /* Kill signal */
  case SIGSEGV: return "SIGSEGV"; /* Invalid memory reference */
  case SIGPIPE: return "SIGPIPE"; /* Broken pipe */
  case SIGALRM: return "SIGALRM"; /* Timer signal from alarm(2) */
  case SIGTERM: return "SIGTERM"; /* Termination signal */
  case SIGUSR1: return "SIGUSR1"; /* User-defined signal 1 */
  case SIGUSR2: return "SIGUSR2"; /* User-defined signal 2 */
  case SIGCHLD: return "SIGCHLD"; /* Child stopped or terminated */
  case SIGCONT: return "SIGCONT"; /* Continue if stopped */
  case SIGSTOP: return "SIGSTOP"; /* Stop process */
  case SIGTSTP: return "SIGTSTP"; /* Stop typed at tty */
  case SIGTTIN: return "SIGTTIN"; /* tty input for background process */
  case SIGTTOU: return "SIGTTOU"; /* tty output for background process */
#ifdef SIGBUS
  case SIGBUS:  return "SIGBUS";  /* Bus error */
#endif
#ifdef SIGPOLL
  case SIGPOLL: return "SIGPOLL"; /* Pollable event */
#endif
#ifdef SIGPROF
  case SIGPROF: return "SIGPROF"; /* Profiling timer expired */
#endif
#ifdef SIGSYS
  case SIGSYS:  return "SIGSYS";  /* Bad argument to routine */
#endif
#ifdef SIGTRAP
  case SIGTRAP: return "SIGTRAP"; /* Trace/breakpoint trap */
#endif
#ifdef SIGURG
  case SIGURG:  return "SIGURG";  /* Urgent condition on socket */
#endif
#ifdef SIGVTALRM
  case SIGVTALRM: return "SIGVTALRM"; /* Virtual alarm clock */
#endif
#ifdef SIGXCPU
  case SIGXCPU: return "SIGXCPU"; /* CPU time limit exceeded */
#endif
#ifdef SIGXFSZ
  case SIGXFSZ: return "SIGXFSZ"; /* File size limit exceeded */
#endif
  default:      return "UNKNOWN";
  }
}

/* signal handler for closing down */
static void sigexit_handler(int signum)
{
  /* just save the signal to indicate that we're stopping */
  nslcd_exitsignal=signum;
}

/* do some cleaning up before terminating */
static void exithandler(void)
{
  /* close socket if it's still in use */
  if (nslcd_serversocket >= 0)
  {
    if (close(nslcd_serversocket))
      log_log(LOG_WARNING,"problem closing server socket (ignored): %s",strerror(errno));
  }
  /* remove existing named socket */
  if (unlink(NSLCD_SOCKET)<0)
  {
    log_log(LOG_DEBUG,"unlink() of "NSLCD_SOCKET" failed (ignored): %s",
            strerror(errno));
  }
  /* remove pidfile */
  if (unlink(NSLCD_PIDFILE)<0)
  {
    log_log(LOG_DEBUG,"unlink() of "NSLCD_PIDFILE" failed (ignored): %s",
            strerror(errno));
  }
  /* log exit */
  log_log(LOG_INFO,"version %s bailing out",VERSION);
}

/* create the directory for the specified file to reside in */
static void mkdirname(const char *filename)
{
  char *tmpname;
  tmpname=strdup(filename);
  if (tmpname==NULL) return;
  (void)mkdir(dirname(tmpname),(mode_t)0755);
  free(tmpname);
}

/* returns a socket ready to answer requests from the client,
   exit()s on error */
static int create_socket(const char *filename)
{
  int sock;
  int i;
  struct sockaddr_un addr;
  /* create a socket */
  if ( (sock=socket(PF_UNIX,SOCK_STREAM,0))<0 )
  {
    log_log(LOG_ERR,"cannot create socket: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* remove existing named socket */
  if (unlink(filename)<0)
  {
    log_log(LOG_DEBUG,"unlink() of %s failed (ignored): %s",
            filename,strerror(errno));
  }
  /* do not block on accept() */
  if ((i=fcntl(sock,F_GETFL,0))<0)
  {
    log_log(LOG_ERR,"fctnl(F_GETFL) failed: %s",strerror(errno));
    if (close(sock))
      log_log(LOG_WARNING,"problem closing socket: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (fcntl(sock,F_SETFL,i|O_NONBLOCK)<0)
  {
    log_log(LOG_ERR,"fctnl(F_SETFL,O_NONBLOCK) failed: %s",strerror(errno));
    if (close(sock))
      log_log(LOG_WARNING,"problem closing socket: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* create the directory if needed */
  mkdirname(filename);
  /* create socket address structure */
  memset(&addr,0,sizeof(struct sockaddr_un));
  addr.sun_family=AF_UNIX;
  strncpy(addr.sun_path,filename,sizeof(addr.sun_path));
  addr.sun_path[sizeof(addr.sun_path)-1]='\0';
  /* bind to the named socket */
  if (bind(sock,(struct sockaddr *)&addr,(sizeof(addr.sun_family)+strlen(addr.sun_path))))
  {
    log_log(LOG_ERR,"bind() to %s failed: %s",filename,strerror(errno));
    if (close(sock))
      log_log(LOG_WARNING,"problem closing socket: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* close the file descriptor on exit */
  if (fcntl(sock,F_SETFD,FD_CLOEXEC)<0)
  {
    log_log(LOG_ERR,"fctnl(F_SETFL,FD_CLOEXEC) failed: %s",strerror(errno));
    if (close(sock))
      log_log(LOG_WARNING,"problem closing socket: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* set permissions of socket so anybody can do requests */
  /* Note: we use chmod() here instead of fchmod() because
     fchmod does not work on sockets
     http://www.opengroup.org/onlinepubs/009695399/functions/fchmod.html
     http://lkml.org/lkml/2005/5/16/11 */
  if (chmod(filename,(mode_t)0666))
  {
    log_log(LOG_ERR,"chmod(0666) failed: %s",strerror(errno));
    if (close(sock))
      log_log(LOG_WARNING,"problem closing socket: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* start listening for connections */
  if (listen(sock,SOMAXCONN)<0)
  {
    log_log(LOG_ERR,"listen() failed: %s",strerror(errno));
    if (close(sock))
      log_log(LOG_WARNING,"problem closing socket: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* we're done */
  return sock;
}

/* read the version information and action from the stream
   this function returns the read action in location pointer to by action */
static int read_header(TFILE *fp,int32_t *action)
{
  int32_t tmpint32;
  /* read the protocol version */
  READ_TYPE(fp,tmpint32,int32_t);
  if (tmpint32 != (int32_t)NSLCD_VERSION)
  {
    log_log(LOG_DEBUG,"wrong nslcd version id (%d)",(int)tmpint32);
    return -1;
  }
  /* read the request type */
  READ(fp,action,sizeof(int32_t));
  return 0;
}

/* read a request message, returns <0 in case of errors,
   this function closes the socket */
static void handleconnection(int sock,MYLDAP_SESSION *session)
{
  TFILE *fp;
  int32_t action;
  struct timeval readtimeout,writetimeout;
  uid_t uid;
  gid_t gid;
  pid_t pid;
  /* log connection */
  if (getpeercred(sock,&uid,&gid,&pid))
    log_log(LOG_DEBUG,"connection from unknown client: %s",strerror(errno));
  else
    log_log(LOG_DEBUG,"connection from pid=%d uid=%d gid=%d",
                      (int)pid,(int)uid,(int)gid);
  /* set the timeouts */
  readtimeout.tv_sec=0; /* clients should send their request quickly */
  readtimeout.tv_usec=500000;
  writetimeout.tv_sec=60; /* clients could be taking some time to process the results */
  writetimeout.tv_usec=0;
  /* create a stream object */
  if ((fp=tio_fdopen(sock,&readtimeout,&writetimeout,
                     READBUFFER_MINSIZE,READBUFFER_MAXSIZE,
                     WRITEBUFFER_MINSIZE,WRITEBUFFER_MAXSIZE))==NULL)
  {
    log_log(LOG_WARNING,"cannot create stream for writing: %s",strerror(errno));
    (void)close(sock);
    return;
  }
  /* read request */
  if (read_header(fp,&action))
  {
    (void)tio_close(fp);
    return;
  }
  /* handle request */
  switch (action)
  {
    case NSLCD_ACTION_ALIAS_BYNAME:     (void)nslcd_alias_byname(fp,session); break;
    case NSLCD_ACTION_ALIAS_ALL:        (void)nslcd_alias_all(fp,session); break;
    case NSLCD_ACTION_ETHER_BYNAME:     (void)nslcd_ether_byname(fp,session); break;
    case NSLCD_ACTION_ETHER_BYETHER:    (void)nslcd_ether_byether(fp,session); break;
    case NSLCD_ACTION_ETHER_ALL:        (void)nslcd_ether_all(fp,session); break;
    case NSLCD_ACTION_GROUP_BYNAME:     (void)nslcd_group_byname(fp,session); break;
    case NSLCD_ACTION_GROUP_BYGID:      (void)nslcd_group_bygid(fp,session); break;
    case NSLCD_ACTION_GROUP_BYMEMBER:   (void)nslcd_group_bymember(fp,session); break;
    case NSLCD_ACTION_GROUP_ALL:        (void)nslcd_group_all(fp,session); break;
    case NSLCD_ACTION_HOST_BYNAME:      (void)nslcd_host_byname(fp,session); break;
    case NSLCD_ACTION_HOST_BYADDR:      (void)nslcd_host_byaddr(fp,session); break;
    case NSLCD_ACTION_HOST_ALL:         (void)nslcd_host_all(fp,session); break;
    case NSLCD_ACTION_NETGROUP_BYNAME:  (void)nslcd_netgroup_byname(fp,session); break;
    case NSLCD_ACTION_NETWORK_BYNAME:   (void)nslcd_network_byname(fp,session); break;
    case NSLCD_ACTION_NETWORK_BYADDR:   (void)nslcd_network_byaddr(fp,session); break;
    case NSLCD_ACTION_NETWORK_ALL:      (void)nslcd_network_all(fp,session); break;
    case NSLCD_ACTION_PASSWD_BYNAME:    (void)nslcd_passwd_byname(fp,session,uid); break;
    case NSLCD_ACTION_PASSWD_BYUID:     (void)nslcd_passwd_byuid(fp,session,uid); break;
    case NSLCD_ACTION_PASSWD_ALL:       (void)nslcd_passwd_all(fp,session,uid); break;
    case NSLCD_ACTION_PROTOCOL_BYNAME:  (void)nslcd_protocol_byname(fp,session); break;
    case NSLCD_ACTION_PROTOCOL_BYNUMBER:(void)nslcd_protocol_bynumber(fp,session); break;
    case NSLCD_ACTION_PROTOCOL_ALL:     (void)nslcd_protocol_all(fp,session); break;
    case NSLCD_ACTION_RPC_BYNAME:       (void)nslcd_rpc_byname(fp,session); break;
    case NSLCD_ACTION_RPC_BYNUMBER:     (void)nslcd_rpc_bynumber(fp,session); break;
    case NSLCD_ACTION_RPC_ALL:          (void)nslcd_rpc_all(fp,session); break;
    case NSLCD_ACTION_SERVICE_BYNAME:   (void)nslcd_service_byname(fp,session); break;
    case NSLCD_ACTION_SERVICE_BYNUMBER: (void)nslcd_service_bynumber(fp,session); break;
    case NSLCD_ACTION_SERVICE_ALL:      (void)nslcd_service_all(fp,session); break;
    case NSLCD_ACTION_SHADOW_BYNAME:    if (uid==0) (void)nslcd_shadow_byname(fp,session); break;
    case NSLCD_ACTION_SHADOW_ALL:       if (uid==0) (void)nslcd_shadow_all(fp,session); break;
    case NSLCD_ACTION_PAM_AUTHC:        (void)nslcd_pam_authc(fp,session,uid); break;
    case NSLCD_ACTION_PAM_AUTHZ:        (void)nslcd_pam_authz(fp,session); break;
    case NSLCD_ACTION_PAM_SESS_O:       (void)nslcd_pam_sess_o(fp,session); break;
    case NSLCD_ACTION_PAM_SESS_C:       (void)nslcd_pam_sess_c(fp,session); break;
    case NSLCD_ACTION_PAM_PWMOD:        (void)nslcd_pam_pwmod(fp,session,uid); break;
    default:
      log_log(LOG_WARNING,"invalid request id: %d",(int)action);
      break;
  }
  /* we're done with the request */
  myldap_session_cleanup(session);
  (void)tio_close(fp);
  return;
}

/* test to see if we can lock the specified file */
static int is_locked(const char* filename)
{
  int fd;
  if (filename!=NULL)
  {
    errno=0;
    if ((fd=open(filename,O_RDWR,0644))<0)
    {
      if (errno==ENOENT)
        return 0; /* if file doesn't exist it cannot be locked */
      log_log(LOG_ERR,"cannot open lock file (%s): %s",filename,strerror(errno));
      exit(EXIT_FAILURE);
    }
    if (lockf(fd,F_TEST,0)<0)
    {
      close(fd);
      return -1;
    }
    close(fd);
  }
  return 0;
}

/* write the current process id to the specified file */
static void create_pidfile(const char *filename)
{
  int fd;
  char buffer[20];
  if (filename!=NULL)
  {
    mkdirname(filename);
    if ((fd=open(filename,O_RDWR|O_CREAT,0644))<0)
    {
      log_log(LOG_ERR,"cannot create pid file (%s): %s",filename,strerror(errno));
      exit(EXIT_FAILURE);
    }
    if (lockf(fd,F_TLOCK,0)<0)
    {
      log_log(LOG_ERR,"cannot lock pid file (%s): %s",filename,strerror(errno));
      exit(EXIT_FAILURE);
    }
    ftruncate(fd,0);
    mysnprintf(buffer,sizeof(buffer),"%d\n",(int)getpid());
    if (write(fd,buffer,strlen(buffer))!=(int)strlen(buffer))
    {
      log_log(LOG_ERR,"error writing pid file (%s): %s",filename,strerror(errno));
      exit(EXIT_FAILURE);
    }
    /* we keep the pidfile open so the lock remains valid */
  }
}

/* try to install signal handler and check result */
static void install_sighandler(int signum,void (*handler) (int))
{
  struct sigaction act;
  memset(&act,0,sizeof(struct sigaction));
  act.sa_handler=handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags=SA_RESTART|SA_NOCLDSTOP;
  if (sigaction(signum,&act,NULL)!=0)
  {
    log_log(LOG_ERR,"error installing signal handler for '%s': %s",signame(signum),strerror(errno));
    exit(EXIT_FAILURE);
  }
}

static void worker_cleanup(void *arg)
{
  MYLDAP_SESSION *session=(MYLDAP_SESSION *)arg;
  myldap_session_close(session);
}

static void *worker(void UNUSED(*arg))
{
  MYLDAP_SESSION *session;
  int csock;
  int j;
  struct sockaddr_storage addr;
  socklen_t alen;
  fd_set fds;
  struct timeval tv;
  /* create a new LDAP session */
  session=myldap_create_session();
  /* clean up the session if we're done */
  pthread_cleanup_push(worker_cleanup,session);
  /* start waiting for incoming connections */
  while (1)
  {
    /* time out connection to LDAP server if needed */
    myldap_session_check(session);
    /* set up the set of fds to wait on */
    FD_ZERO(&fds);
    FD_SET(nslcd_serversocket,&fds);
    /* set up our timeout value */
    tv.tv_sec=nslcd_cfg->ldc_idle_timelimit;
    tv.tv_usec=0;
    /* wait for a new connection */
    j=select(nslcd_serversocket+1,&fds,NULL,NULL,nslcd_cfg->ldc_idle_timelimit>0?&tv:NULL);
    /* check result of select() */
    if (j<0)
    {
      if (errno==EINTR)
        log_log(LOG_DEBUG,"debug: select() failed (ignored): %s",strerror(errno));
      else
        log_log(LOG_ERR,"select() failed: %s",strerror(errno));
      continue;
    }
    /* see if our file descriptor is actually ready */
    if (!FD_ISSET(nslcd_serversocket,&fds))
      continue;
    /* wait for a new connection */
    alen=(socklen_t)sizeof(struct sockaddr_storage);
    csock=accept(nslcd_serversocket,(struct sockaddr *)&addr,&alen);
    if (csock<0)
    {
      if ((errno==EINTR)||(errno==EAGAIN)||(errno==EWOULDBLOCK))
        log_log(LOG_DEBUG,"accept() failed (ignored): %s",strerror(errno));
      else
        log_log(LOG_ERR,"accept() failed: %s",strerror(errno));
      continue;
    }
    /* make sure O_NONBLOCK is not inherited */
    if ((j=fcntl(csock,F_GETFL,0))<0)
    {
      log_log(LOG_ERR,"fctnl(F_GETFL) failed: %s",strerror(errno));
      if (close(csock))
        log_log(LOG_WARNING,"problem closing socket: %s",strerror(errno));
      continue;
    }
    if (fcntl(csock,F_SETFL,j&~O_NONBLOCK)<0)
    {
      log_log(LOG_ERR,"fctnl(F_SETFL,~O_NONBLOCK) failed: %s",strerror(errno));
      if (close(csock))
        log_log(LOG_WARNING,"problem closing socket: %s",strerror(errno));
      continue;
    }
    /* indicate new connection to logging module (genrates unique id) */
    log_newsession();
    /* handle the connection */
    handleconnection(csock,session);
    /* indicate end of session in log messages */
    log_clearsession();
  }
  pthread_cleanup_pop(1);
  return NULL;
}

/* function to disable lookups through the nss_ldap module to avoid lookup
   loops */
static void disable_nss_ldap(void)
{
  void *handle;
  char *error;
  int *enable_flag;
  /* try to load the NSS module */
#ifdef RTLD_NODELETE
  handle=dlopen(NSS_LDAP_SONAME,RTLD_LAZY|RTLD_NODELETE);
#else /* not RTLD_NODELETE */
  handle=dlopen(NSS_LDAP_SONAME,RTLD_LAZY);
#endif /* RTLD_NODELETE */
  if (handle==NULL)
  {
    log_log(LOG_WARNING,"Warning: LDAP NSS module not loaded: %s",dlerror());
    return;
  }
  /* clear any existing errors */
  dlerror();
  /* try to look up the flag */
  enable_flag=(int *)dlsym(handle,"_nss_ldap_enablelookups");
  error=dlerror();
  if (error!=NULL)
  {
    log_log(LOG_WARNING,"Warning: %s (probably older NSS module loaded)",error);
    /* fall back to changing the way host lookup is done */
#ifdef HAVE___NSS_CONFIGURE_LOOKUP
    if (__nss_configure_lookup("hosts","files dns"))
      log_log(LOG_ERR,"unable to override hosts lookup method: %s",strerror(errno));
#endif /* HAVE___NSS_CONFIGURE_LOOKUP */
    dlclose(handle);
    return;
  }
  /* disable nss_ldap */
  *enable_flag=0;
#ifdef RTLD_NODELETE
  /* only close the handle if RTLD_NODELETE was used */
  dlclose(handle);
#endif /* RTLD_NODELETE */
}

/* the main program... */
int main(int argc,char *argv[])
{
  int i;
  sigset_t signalmask,oldmask;
#ifdef HAVE_PTHREAD_TIMEDJOIN_NP
  struct timespec ts;
#endif /* HAVE_PTHREAD_TIMEDJOIN_NP */
  /* parse the command line */
  parse_cmdline(argc,argv);
  /* clean the environment */
#ifdef HAVE_CLEARENV
  if ( clearenv() ||
       putenv("HOME=/") ||
       putenv("TMPDIR=/tmp") ||
       putenv("LDAPNOINIT=1") )
  {
    log_log(LOG_ERR,"clearing environment failed");
    exit(EXIT_FAILURE);
  }
#else /* not HAVE_CLEARENV */
  /* this is a bit ugly */
  environ=sane_environment;
#endif /* not HAVE_CLEARENV */
  /* disable the nss_ldap module for this process */
  disable_nss_ldap();
  /* set LDAP log level */
  if (myldap_set_debuglevel(nslcd_debugging)!=LDAP_SUCCESS)
    exit(EXIT_FAILURE);
  /* read configuration file */
  cfg_init(NSLCD_CONF_PATH);
  /* set default mode for pidfile and socket */
  (void)umask((mode_t)0022);
  /* see if someone already locked the pidfile
     if --check option was given:
       exit TRUE if daemon runs (pidfile locked), FALSE otherwise */
  if (nslcd_checkonly)
  {
    if (is_locked(NSLCD_PIDFILE))
    {
      log_log(LOG_DEBUG,"pidfile (%s) is locked",NSLCD_PIDFILE);
      exit(EXIT_SUCCESS);
    }
    else
    {
      log_log(LOG_DEBUG,"pidfile (%s) is not locked",NSLCD_PIDFILE);
      exit(EXIT_FAILURE);
    }
  }
  /* normal check for pidfile locked */
  if (is_locked(NSLCD_PIDFILE))
  {
    log_log(LOG_ERR,"daemon may already be active, cannot acquire lock (%s): %s",NSLCD_PIDFILE,strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* close all file descriptors (except stdin/out/err) */
  i=sysconf(_SC_OPEN_MAX);
  /* if the system does not have OPEN_MAX just close the first 32 and
     hope we closed enough */
  if (i<0)
    i=32;
  for (;i>3;i--)
    close(i);
  /* daemonize */
  if ((!nslcd_debugging)&&(daemon(0,0)<0))
  {
    log_log(LOG_ERR,"unable to daemonize: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* intilialize logging */
  if (!nslcd_debugging)
    log_startlogging();
  log_log(LOG_INFO,"version %s starting",VERSION);
  /* write pidfile */
  create_pidfile(NSLCD_PIDFILE);
  /* install handler to close stuff off on exit and log notice */
  if (atexit(exithandler))
  {
    log_log(LOG_ERR,"atexit() failed: %s",strerror(errno));
    exit(EXIT_FAILURE);
  }
  /* create socket */
  nslcd_serversocket=create_socket(NSLCD_SOCKET);
#ifdef HAVE_SETGROUPS
  /* drop all supplemental groups */
  if (setgroups(0,NULL)<0)
    log_log(LOG_WARNING,"cannot setgroups(0,NULL) (ignored): %s",strerror(errno));
  else
    log_log(LOG_DEBUG,"setgroups(0,NULL) done");
#else /* HAVE_SETGROUPS */
  log_log(LOG_DEBUG,"setgroups() not available");
#endif /* not HAVE_SETGROUPS */
  /* change to nslcd gid */
  if (nslcd_cfg->ldc_gid!=NOGID)
  {
    if (setgid(nslcd_cfg->ldc_gid)!=0)
    {
      log_log(LOG_ERR,"cannot setgid(%d): %s",(int)nslcd_cfg->ldc_gid,strerror(errno));
      exit(EXIT_FAILURE);
    }
    log_log(LOG_DEBUG,"setgid(%d) done",(int)nslcd_cfg->ldc_gid);
  }
  /* change to nslcd uid */
  if (nslcd_cfg->ldc_uid!=NOUID)
  {
    if (setuid(nslcd_cfg->ldc_uid)!=0)
    {
      log_log(LOG_ERR,"cannot setuid(%d): %s",(int)nslcd_cfg->ldc_uid,strerror(errno));
      exit(EXIT_FAILURE);
    }
    log_log(LOG_DEBUG,"setuid(%d) done",(int)nslcd_cfg->ldc_uid);
  }
  /* block all these signals so our worker threads won't handle them */
  sigemptyset(&signalmask);
  sigaddset(&signalmask,SIGHUP);
  sigaddset(&signalmask,SIGINT);
  sigaddset(&signalmask,SIGQUIT);
  sigaddset(&signalmask,SIGABRT);
  sigaddset(&signalmask,SIGPIPE);
  sigaddset(&signalmask,SIGTERM);
  sigaddset(&signalmask,SIGUSR1);
  sigaddset(&signalmask,SIGUSR2);
  pthread_sigmask(SIG_BLOCK,&signalmask,&oldmask);
  /* start worker threads */
  log_log(LOG_INFO,"accepting connections");
  nslcd_threads=(pthread_t *)malloc(nslcd_cfg->ldc_threads*sizeof(pthread_t));
  if (nslcd_threads==NULL)
  {
    log_log(LOG_CRIT,"main(): malloc() failed to allocate memory");
    exit(EXIT_FAILURE);
  }
  for (i=0;i<nslcd_cfg->ldc_threads;i++)
  {
    if (pthread_create(&nslcd_threads[i],NULL,worker,NULL))
    {
      log_log(LOG_ERR,"unable to start worker thread %d: %s",i,strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
  pthread_sigmask(SIG_SETMASK,&oldmask,NULL);
  /* install signalhandlers for some signals */
  install_sighandler(SIGHUP, sigexit_handler);
  install_sighandler(SIGINT, sigexit_handler);
  install_sighandler(SIGQUIT,sigexit_handler);
  install_sighandler(SIGABRT,sigexit_handler);
  install_sighandler(SIGPIPE,SIG_IGN);
  install_sighandler(SIGTERM,sigexit_handler);
  install_sighandler(SIGUSR1,sigexit_handler);
  install_sighandler(SIGUSR2,sigexit_handler);
  /* wait until we received a signal */
  while (nslcd_exitsignal==0)
  {
    sleep(INT_MAX); /* sleep as long as we can or until we receive a signal */
  }
  /* print something about received signal */
  log_log(LOG_INFO,"caught signal %s (%d), shutting down",
               signame(nslcd_exitsignal),nslcd_exitsignal);
  /* cancel all running threads */
  for (i=0;i<nslcd_cfg->ldc_threads;i++)
    if (pthread_cancel(nslcd_threads[i]))
      log_log(LOG_WARNING,"failed to stop thread %d (ignored): %s",i,strerror(errno));
  /* close server socket to trigger failures in threads waiting on accept() */
  close(nslcd_serversocket);
  nslcd_serversocket=-1;
  /* if we can, wait a few seconds for the threads to finish */
#ifdef HAVE_PTHREAD_TIMEDJOIN_NP
  ts.tv_sec=time(NULL)+3;
  ts.tv_nsec=0;
#endif /* HAVE_PTHREAD_TIMEDJOIN_NP */
  for (i=0;i<nslcd_cfg->ldc_threads;i++)
  {
#ifdef HAVE_PTHREAD_TIMEDJOIN_NP
    pthread_timedjoin_np(nslcd_threads[i],NULL,&ts);
#endif /* HAVE_PTHREAD_TIMEDJOIN_NP */
    if (pthread_kill(nslcd_threads[i],0)==0)
      log_log(LOG_ERR,"thread %d is still running, shutting down anyway",i);
  }
  /* we're done */
  return EXIT_FAILURE;
}
