/*
 *	appl/bsd/krlogind.c
 */

/*
 * Copyright (c) 1983 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
char copyright[] =
  "@(#) Copyright (c) 1983 The Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

/* based on @(#)rlogind.c	5.17 (Berkeley) 8/31/88 */
     
     /*
      * remote login server:
      *	remuser\0
      *	locuser\0
      *	terminal info\0
      *	data
      */
     
/*
 * This is the rlogin daemon. The very basic protocol for checking 
 * authentication and authorization is:
 * 1) Check authentication.
 * 2) Check authorization via the access-control files: 
 *    ~/.k5login (using krb5_kuserok) and/or
 *    ~/.rhosts  (using ruserok).
 * 3) Prompt for password if any checks fail, or if so configured.
 * Allow login if all goes well either by calling the accompanying 
 * login.krb5 or /bin/login, according to the definition of 
 * DO_NOT_USE_K_LOGIN.
 * 
 * The configuration is done either by command-line arguments passed by 
 * inetd, or by the name of the daemon. If command-line arguments are
 * present, they  take priority. The options are:
 * -k and -K means check .k5login (using krb5_kuserok).
 * -r and -R means check .rhosts  (using ruserok).
 * -p and -P means prompt for password.
 * The difference between upper and lower case is as follows:
 *    If lower case -r or -k, then as long as one of krb5_kuserok or 
 * ruserok passes, allow login without password. If the -p option is
 * passed with -r or -k, then if both checks fail, allow login but
 * only after password verification. 
 *    If uppercase -R or -K, then those checks must be passed,
 * regardless of other checks, else no login with or without password.
 *    If the -P option is passed, then the password is verified in 
 * addition to all other checks. If -p is not passed with -k or -r,
 * and both checks fail, then login permission is denied.
 *    -x and -e means use encryption.
 *
 *    If no command-line arguments are present, then the presence of the 
 * letters kKrRexpP in the program-name before "logind" determine the 
 * behaviour of the program exactly as with the command-line arguments.
 *
 * If the ruserok check is to be used, then the client should connect
 * from a privileged port, else deny permission.
 */ 
     
/* DEFINES:
 *   KERBEROS - Define this if application is to be kerberised.
 *   CRYPT    - Define this if encryption is to be an option.
 *   DO_NOT_USE_K_LOGIN - Define this if you want to use /bin/login
 *              instead  of the accompanying login.krb5. In that case,
 *              the remote user's name must be present in the local
 *              .rhosts file, regardless of any options specified.
 *   KRB5_KRB4_COMPAT - Define this if v4 rlogin clients are also to be served.
 *   ALWAYS_V5_KUSEROK - Define this if you want .k5login to be
 *              checked even for v4 clients (instead of .klogin).
 *   LOG_ALL_LOGINS - Define this if you want to log all logins.
 *   LOG_OTHER_USERS - Define this if you want to log all principals
 *              that do not map onto the local user.
 *   LOG_REMOTE_REALM - Define this if you want to log all principals from 
 *              remote realms.
 *       Note:  Root logins are always logged.
 */

/*
 * This is usually done in the Makefile.  Actually, these sources may
 * not work without the KERBEROS #defined.
 *
 * #define KERBEROS
 */
#define LOG_REMOTE_REALM
#define CRYPT


#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef __SCO__
#include <sys/unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/time.h>
#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <pwd.h>
     
#ifdef HAVE_SYS_LABEL_H
/* only SunOS 4? */
#include <sys/label.h>
#include <sys/audit.h>
#include <pwdadj.h>
#endif
     
#include <signal.h>

#ifdef hpux
#include <sys/ptyio.h>
#endif
#ifdef sysvimp
#include <compat.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_STREAMS
#include <sys/stream.h>
#include <sys/stropts.h>
#endif

#if defined(POSIX_TERMIOS) && !defined(ultrix)
#include <termios.h>
#else
#include <sgtty.h>
#endif
     
#include <netdb.h>
#include <syslog.h>
#include <string.h>
#include <sys/param.h>

#ifdef HAVE_STREAMS
/* krlogin doesn't test sys/tty... */
#ifdef HAVE_SYS_TTY_H
#include <sys/tty.h>
#endif

#ifdef HAVE_SYS_PTYVAR_H
/* Solaris actually uses packet mode, so the real macros are needed too */
#include <sys/ptyvar.h>
#endif
#endif


#ifndef TIOCPKT_NOSTOP
/* These values are over-the-wire protocol, *not* local values */
#define TIOCPKT_NOSTOP          0x10
#define TIOCPKT_DOSTOP          0x20
#define TIOCPKT_FLUSHWRITE      0x02
#endif

#ifdef HAVE_SYS_FILIO_H
/* get FIONBIO from sys/filio.h, so what if it is a compatibility feature */
#include <sys/filio.h>
#endif

#ifndef SETPGRP_TWOARG
#define setpgrp(a,b) setpgrp()
#endif

#ifndef HAVE_KILLPG
#define killpg(pid, sig) kill(-(pid), (sig))
#endif

#ifdef NO_WINSIZE
struct winsize {
    unsigned short ws_row, ws_col;
    unsigned short ws_xpixel, ws_ypixel;
};
#endif /* NO_WINSIZE */
     
#ifndef roundup
#define roundup(x,y) ((((x)+(y)-1)/(y))*(y))
#endif

#ifdef KERBEROS
     
#include "krb5.h"
#include <kerberosIV/krb.h>
#include <libpty.h>
#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif

int auth_sys = 0;	/* Which version of Kerberos used to authenticate */

#define KRB5_RECVAUTH_V4	4
#define KRB5_RECVAUTH_V5	5

int non_privileged = 0; /* set when connection is seen to be from */
			/* a non-privileged port */

AUTH_DAT	*v4_kdata;
Key_schedule v4_schedule;
int v4_des_read(), v4_des_write();

#define RLOGIND_BUFSIZ 5120

int v5_des_read(), v5_des_write();

#include "com_err.h"
     
#define SECURE_MESSAGE  "This rlogin session is using DES encryption for all data transmissions.\r\n"

int (*des_read)(), (*des_write)();
char des_inbuf[2*RLOGIND_BUFSIZ]; /* needs to be > largest read size */
char des_outbuf[2*RLOGIND_BUFSIZ];/* needs to be > largest write size */
krb5_data desinbuf,desoutbuf;
krb5_encrypt_block eblock;        /* eblock for encrypt/decrypt */

krb5_authenticator      *kdata;
krb5_ticket     *ticket = 0;
krb5_context bsd_context;

krb5_keytab keytab = NULL;

#define ARGSTR	"rRkKeExXpPD:S:M:L:?"
#else /* !KERBEROS */
#define ARGSTR	"rRpPD:?"
#define (*des_read)  read
#define (*des_write) write
#endif /* KERBEROS */

#ifndef LOGIN_PROGRAM
#ifdef DO_NOT_USE_K_LOGIN
#ifdef sysvimp
#define LOGIN_PROGRAM "/bin/remlogin"
#else
#define LOGIN_PROGRAM "/bin/login"
#endif
#else /* DO_NOT_USE_K_LOGIN */
#define LOGIN_PROGRAM KRB5_PATH_LOGIN
#endif /* DO_NOT_USE_K_LOGIN */
#endif /* LOGIN_PROGRAM */

char *login_program = LOGIN_PROGRAM;

#define MAXRETRIES 4
#define MAX_PROG_NAME 16

#ifndef UT_NAMESIZE	/* linux defines it directly in <utmp.h> */
#define	UT_NAMESIZE	sizeof(((struct utmp *)0)->ut_name)
#endif

char		lusername[UT_NAMESIZE+1];
char		rusername[UT_NAMESIZE+1];
char            *krusername = 0;
char		term[64];
char            rhost_name[128];
krb5_principal  client;

int	reapchild();
char 	*progname;

static	int Pfd;

#if (defined(_AIX) && defined(i386)) || defined(ibm032) || (defined(vax) && !defined(ultrix)) || (defined(SunOS) && SunOS > 40) || defined(solaris20)
#define VHANG_FIRST
#endif

#if defined(ultrix)
#define VHANG_LAST		/* vhangup must occur on close, not open */
#endif

void	fatal(), fatalperror(), doit(), usage(), do_krb_login();
int	princ_maps_to_lname(), default_realm();
krb5_sigtype	cleanup();

int must_pass_rhosts = 0, must_pass_k5 = 0, must_pass_one = 0;
int do_encrypt = 0, passwd_if_fail = 0, passwd_req = 0;

main(argc, argv)
     int argc;
     char **argv;
{
    extern int opterr, optind;
    extern char * optarg;
    int on = 1, fromlen, ch, i;
    struct sockaddr_in from;
    char *options;
    int debug_port = 0;
    int fd;
#ifdef KERBEROS
    krb5_error_code status;
#endif
    
    progname = *argv;
    
pty_init();
    
#ifndef LOG_NDELAY
#define LOG_NDELAY 0
#endif
    
#ifdef KERBEROS
    krb5_init_context(&bsd_context);
    krb5_init_ets(bsd_context);
#endif
    
#ifndef LOG_AUTH /* 4.2 syslog */
    openlog(progname, LOG_PID | LOG_NDELAY);
#else
    openlog(progname, LOG_PID | LOG_NDELAY, LOG_AUTH);
#endif /* 4.2 syslog */
    
    if (argc == 1) { /* Get parameters from program name. */
	if (strlen(progname) > MAX_PROG_NAME) {
	    usage();
	    exit(1);
	}
	options = (char *) malloc(MAX_PROG_NAME+1);
	options[0] = '\0';
	for (i = 0; (progname[i] != '\0') && (i < MAX_PROG_NAME); i++)
	  if (!strcmp(progname+i, "logind")) {
	      char **newargv;

	      newargv = (char **) malloc(sizeof(char *) * 3);

	      strcpy(options, "-");
	      strncat(options, progname, i);

	      argc = 2;
	      
	      newargv[0] = argv[0];
	      newargv[1] = options;
	      newargv[2] = NULL;

	      argv = newargv;
	      break;
	  }
	if (options[0] == '\0') {
	    usage();
	    exit(1);
	}
    }
    
    /* Analyse parameters. */
    opterr = 0;
    while ((ch = getopt(argc, argv, ARGSTR)) != EOF)
      switch (ch) {
	case 'r':         
	  must_pass_one = 1; /* If just 'r', any one check must succeed */
	  break;
	case 'R':         /* If 'R', must pass .rhosts check*/
	  must_pass_rhosts = 1;
	  if (must_pass_one)
	    must_pass_one = 0;
	  break;
#ifdef KERBEROS
	case 'k':
	  must_pass_one = 1; /* If just 'k', any one check must succeed */
	  break;
	case 'K':         /* If 'K', must pass .k5login check*/
	  must_pass_k5 = 1;
	  if (must_pass_one)
	    must_pass_one = 0;
	  break;
#ifdef CRYPT
	case 'x':         /* Use encryption. */
	case 'X':
	case 'e':
	case 'E':
	  do_encrypt = 1;
	  break;
#endif
	case 'S':
	  if (status = krb5_kt_resolve(bsd_context, optarg, &keytab)) {
		  com_err(progname, status, "while resolving srvtab file %s",
			  optarg);
		  exit(2);
	  }
	  break;
	case 'M':
	  krb5_set_default_realm(bsd_context, optarg);
	  break;
#endif
	case 'p':
	  passwd_if_fail = 1; /* Passwd reqd if any check fails */
	  break;
	case 'P':         /* passwd is a must */
	  passwd_req = 1;
	  break;
	case 'D':
	  debug_port = atoi(optarg);
	  break;
	case 'L':
#ifndef DO_NOT_USE_K_LOGIN
	  login_program = optarg;
#endif
	  break;
	case '?':
	default:
	  usage();
	  exit(1);
	  break;
      }
    argc -= optind;
    argv += optind;
    
    fromlen = sizeof (from);

     if (debug_port) {
	 int s;
	 struct sockaddr_in sin;
	 
	 if ((s = socket(AF_INET, SOCK_STREAM, PF_UNSPEC)) < 0) {
	     fprintf(stderr, "Error in socket: %s\n", strerror(errno));
	     exit(2);
	 }
	 
	 memset((char *) &sin, 0,sizeof(sin));
	 sin.sin_family = AF_INET;
	 sin.sin_port = htons(debug_port);
	 sin.sin_addr.s_addr = INADDR_ANY;
	 
	 (void) setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
			   (char *)&on, sizeof(on));

	 if ((bind(s, (struct sockaddr *) &sin, sizeof(sin))) < 0) {
	     fprintf(stderr, "Error in bind: %s\n", strerror(errno));
	     exit(2);
	 }
	 
	 if ((listen(s, 5)) < 0) {
	     fprintf(stderr, "Error in listen: %s\n", strerror(errno));
	     exit(2);
	 }
	 
	 if ((fd = accept(s, (struct sockaddr *) &from, &fromlen)) < 0) {
	     fprintf(stderr, "Error in accept: %s\n", strerror(errno));
	     exit(2);
	 }
	 
	 close(s);
     } 
     else {
	 if (getpeername(0, (struct sockaddr *)&from, &fromlen) < 0) {
	     syslog(LOG_ERR,"Can't get peer name of remote host: %m");
#ifdef STDERR_FILENO
	     fatal(STDERR_FILENO, "Can't get peer name of remote host", 1);
#else
	     fatal(3, "Can't get peer name of remote host", 1);
#endif
	 }
	 fd = 0;
     }

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
		   (const char *) &on, sizeof (on)) < 0)
      syslog(LOG_WARNING, "setsockopt (SO_KEEPALIVE): %m");
    
    doit(fd, &from);
}



#ifndef LOG_AUTH
#define LOG_AUTH 0
#endif

int	child;
int	netf;
char	line[MAXPATHLEN];
extern	char	*inet_ntoa();

#ifdef TIOCSWINSZ
struct winsize win = { 0, 0, 0, 0 };
#endif

int pid; /* child process id */

void doit(f, fromp)
     int f;
     struct sockaddr_in *fromp;
{
    int i, p, t, vfd, on = 1;
    register struct hostent *hp;
    char c;
    char buferror[255];
    struct passwd *pwd;
#ifdef POSIX_SIGNALS
    struct sigaction sa;
#endif
    int retval;
int syncpipe[2];
    netf = -1;
    alarm(60);
    read(f, &c, 1);
    
    if (c != 0){
	exit(1);
    }

    alarm(0);
    /* Initialize syncpipe */
    if (pipe( syncpipe ) < 0 )
	fatalperror ( f , "");
    

#ifdef POSIX_SIGNALS
    /* Initialize "sa" structure. */
    (void) sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
#endif

    fromp->sin_port = ntohs((u_short)fromp->sin_port);
    hp = gethostbyaddr((char *) &fromp->sin_addr, sizeof (struct in_addr),
		       fromp->sin_family);
    if (hp == 0) {
	/*
	 * Only the name is used below.
	 */
	sprintf(rhost_name,"%s",inet_ntoa(fromp->sin_addr));
    }
    
    /* Save hostent information.... */
    else strcpy(rhost_name,hp->h_name);
    
    if (fromp->sin_family != AF_INET)
      fatal(f, "Permission denied - Malformed from address\n");
    
#ifdef KERBEROS
    if (must_pass_k5 || must_pass_one) {
	/* setup des buffers */
	desinbuf.data = des_inbuf;
	desoutbuf.data = des_outbuf;    /* Set up des buffers */
    }
    /* Must come from privileged port when .rhosts is being looked into */
    if ((must_pass_rhosts || must_pass_one) 
	&& (fromp->sin_port >= IPPORT_RESERVED ||
	    fromp->sin_port < IPPORT_RESERVED/2))
      non_privileged = 1;
#else /* !KERBEROS */
    if (fromp->sin_port >= IPPORT_RESERVED ||
	fromp->sin_port < IPPORT_RESERVED/2)
      fatal(f, "Permission denied - Connection from bad port");
#endif /* KERBEROS */
    
    /* Set global netf to f now : we may need to drop everything
       in do_krb_login. */
    netf = f;
    
#if defined(KERBEROS)
    /* All validation, and authorization goes through do_krb_login() */
    do_krb_login(rhost_name);
#else
    getstr(f, rusername, sizeof(rusername), "remuser");
    getstr(f, lusername, sizeof(lusername), "locuser");
    getstr(f, term, sizeof(term), "Terminal type");
#endif
    
    write(f, "", 1);
    if (retval = pty_getpty(&p,line, sizeof(line))) {
	com_err(progname, retval, "while getting master pty");
	exit(2);
    }
    
    Pfd = p;
#ifdef TIOCSWINSZ
    (void) ioctl(p, TIOCSWINSZ, &win);
#endif
    




#ifdef POSIX_SIGNALS
    sa.sa_handler = cleanup;
    (void) sigaction(SIGCHLD, &sa, (struct sigaction *)0);
    (void) sigaction(SIGTERM, &sa, (struct sigaction *)0);
#else
    signal(SIGCHLD, cleanup);
    signal(SIGTERM, cleanup);
#endif
    pid = fork();
    if (pid < 0)
      fatalperror(f, "");
    if (pid == 0) {
#if defined(POSIX_TERMIOS) && !defined(ultrix)
	struct termios new_termio;
#else
	struct sgttyb b;
#endif /* POSIX_TERMIOS */
	if ( retval = pty_open_slave(line, &t)) {
	    fatal(f, error_message(retval));
	    exit(1);
	}
	

#if defined(POSIX_TERMIOS) && !defined(ultrix)
	tcgetattr(t,&new_termio);
	new_termio.c_lflag &=  ~(ICANON|ECHO|ISIG|IEXTEN);
	/* so that login can read the authenticator */
	new_termio.c_iflag &= ~(IXON|IXANY|BRKINT|INLCR|ICRNL|ISTRIP);
	/* new_termio.c_iflag = 0; */
	/* new_termio.c_oflag = 0; */
	new_termio.c_cc[VMIN] = 1;
	new_termio.c_cc[VTIME] = 0;
	tcsetattr(t,TCSANOW,&new_termio);
#else
	(void)ioctl(t, TIOCGETP, &b);
	b.sg_flags = RAW|ANYP;
	(void)ioctl(t, TIOCSETP, &b);
#endif /* POSIX_TERMIOS */

	pid = 0;			/*reset pid incase exec fails*/
	    
	/*
	 **      signal the parent that we have turned off echo
	 **      on the slave side of the pty ... he's waiting
	 **      because otherwise the rlogin protocol junk gets
	 **      echo'd to the user (locuser^@remuser^@term/baud)
	 **      and we don't get the right tty affiliation, and
	 **      other kinds of hell breaks loose ...
	 */
	(void) write(syncpipe[1], &c, 1);
	(void) close(syncpipe[1]);
	(void) close(syncpipe[0]);
		
	close(f), close(p);
	dup2(t, 0), dup2(t, 1), dup2(t, 2);
	if (t > 2)
	  close(t);

	
#if defined(sysvimp)
	setcompat (COMPAT_CLRPGROUP | (getcompat() & ~COMPAT_BSDTTY));
#endif
	
	/* Log access to account */
	pwd = (struct passwd *) getpwnam(lusername);
	if (pwd && (pwd->pw_uid == 0)) {
	    if (passwd_req)
	      syslog(LOG_NOTICE, "ROOT login by %s (%s@%s) forcing password access",
		     krusername ? krusername : "", rusername, rhost_name);
	    else
	      syslog(LOG_NOTICE, "ROOT login by %s (%s@%s) ", 
		     krusername ? krusername : "", rusername, rhost_name);
	}
#ifdef KERBEROS
#if defined(LOG_REMOTE_REALM) && !defined(LOG_OTHER_USERS) && !defined(LOG_ALL_LOGINS)
	/* Log if principal is from a remote realm */
        else if (client && !default_realm(client))
#endif /* LOG_REMOTE_REALM */
  
#if defined(LOG_OTHER_USERS) && !defined(LOG_ALL_LOGINS) 
	/* Log if principal name does not map to local username */
        else if (client && !princ_maps_to_lname(client, lusername))
#endif /* LOG_OTHER_USERS */

#if defined(LOG_ALL_LOGINS)
        else
#endif /* LOG_ALL_LOGINS */

#if defined(LOG_REMOTE_REALM) || defined(LOG_OTHER_USERS) || defined(LOG_ALL_LOGINS)
	{
	    if (passwd_req)
	      syslog(LOG_NOTICE,
		     "login by %s (%s@%s) as %s forcing password access\n",
		     krusername ? krusername : "", rusername,
		     rhost_name, lusername);
	    else 
	      syslog(LOG_NOTICE,
		     "login by %s (%s@%s) as %s\n",
		     krusername ? krusername : "", rusername,
		     rhost_name, lusername); 
	}
#endif /* LOG_REMOTE_REALM || LOG_OTHER_USERS || LOG_ALL_LOGINS */
#endif /* KERBEROS */

#ifndef NO_UT_PID
	{

	    pty_update_utmp(PTY_LOGIN_PROCESS, getpid(), "rlogin", line,
			    ""/*host*/, PTY_TTYSLOT_USABLE);
	}
#endif

#ifdef DO_NOT_USE_K_LOGIN
	execl(login_program, "login", "-r", rhost_name, 0);
#else
	if (passwd_req)
	  execl(login_program, "login","-h", rhost_name, lusername, 0);
	else
	  execl(login_program, "login", "-h", rhost_name, "-e", lusername, 0);
#endif
	
	fatalperror(2, login_program);
	/*NOTREACHED*/
    } /* if (pid == 0) */

    /*
     **      wait for child to start ... read one byte
     **      -- see the child, who writes one byte after
     **      turning off echo on the slave side ...
     **      The master blocks here until it reads a byte.
     */
    
(void) close(syncpipe[1]);
    if (read(syncpipe[0], &c, 1) != 1) {
	/*
	 * Problems read failed ...
	 */
	sprintf(buferror, "Cannot read slave pty %s ",line);
	fatalperror(p,buferror);
    }
    close(syncpipe[0]);

    
#if defined(KERBEROS) 
    if (do_encrypt) {
	if (((*des_write)(f, SECURE_MESSAGE, sizeof(SECURE_MESSAGE))) < 0){
	    sprintf(buferror, "Cannot encrypt-write network.");
	    fatal(p,buferror);
	}
    }
    else 
      /*
       * if encrypting, don't turn on NBIO, else the read/write routines
       * will fail to work properly
       */
#endif /* KERBEROS */
      ioctl(f, FIONBIO, &on);
    ioctl(p, FIONBIO, &on);

    /* FIONBIO doesn't always work on ptys, use fcntl to set O_NDELAY? */
    (void) fcntl(p,F_SETFL,fcntl(p,F_GETFL,0) | O_NDELAY);

/*** XXX -- make this portable ***/
#if defined(TIOCPKT) && !defined(__svr4__) || defined(solaris20)
    ioctl(p, TIOCPKT, &on);
#endif

#ifdef POSIX_SIGNALS
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGTSTP, &sa, (struct sigaction *)0);
#else
    signal(SIGTSTP, SIG_IGN);
#endif

#ifdef hpux
    setpgrp2(0, 0);
#else
    setpgrp(0, 0);
#endif
    
#ifdef DO_NOT_USE_K_LOGIN
    /* Pass down rusername and lusername to login. */
    (void) write(p, rusername, strlen(rusername) +1);
    (void) write(p, lusername, strlen(lusername) +1);
#endif
    /* stuff term info down to login */
    if( write(p, term, strlen(term)+1) != strlen(term)+1 ){
	/*
	 * Problems write failed ...
	 */
	sprintf(buferror,"Cannot write slave pty %s ",line);
	fatalperror(f,buferror);
    }
    protocol(f, p);
    signal(SIGCHLD, SIG_IGN);
    cleanup();
}

unsigned char	magic[2] = { 0377, 0377 };
#ifdef TIOCSWINSZ
#ifndef TIOCPKT_WINDOW
#define TIOCPKT_WINDOW 0x80
#endif
unsigned char	oobdata[] = {TIOCPKT_WINDOW};
#else
char    oobdata[] = {0};
#endif

/*
 * Handle a "control" request (signaled by magic being present)
 * in the data stream.  For now, we are only willing to handle
 * window size changes.
 */
control(pty, cp, n)
     int pty;
     unsigned char *cp;
     int n;
{
    struct winsize w;
    int pgrp;
    
    if (n < 4+sizeof (w) || cp[2] != 's' || cp[3] != 's')
      return (0);
#ifdef TIOCSWINSZ
    oobdata[0] &= ~TIOCPKT_WINDOW;	/* we know he heard */
    memcpy((char *)&w,cp+4, sizeof(w));
    w.ws_row = ntohs(w.ws_row);
    w.ws_col = ntohs(w.ws_col);
    w.ws_xpixel = ntohs(w.ws_xpixel);
    w.ws_ypixel = ntohs(w.ws_ypixel);
    (void)ioctl(pty, TIOCSWINSZ, &w);
    if (ioctl(pty, TIOCGPGRP, &pgrp) >= 0)
      (void) killpg(pgrp, SIGWINCH);
#endif
    return (4+sizeof (w));
}



/*
 * rlogin "protocol" machine.
 */
protocol(f, p)
     int f, p;
{
    unsigned char pibuf[1024], fibuf[1024], *pbp, *fbp;
    register pcc = 0, fcc = 0;
    int cc;
    char cntl;
#ifdef POSIX_SIGNALS
    struct sigaction sa;
#endif
    
    /*
     * Must ignore SIGTTOU, otherwise we'll stop
     * when we try and set slave pty's window shape
     * (our controlling tty is the master pty).
     */
#ifdef POSIX_SIGNALS
    (void) sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGTTOU, &sa, (struct sigaction *)0);
#else
    signal(SIGTTOU, SIG_IGN);
#endif
#ifdef TIOCSWINSZ
    send(f, oobdata, 1, MSG_OOB);	/* indicate new rlogin */
#endif
    for (;;) {
	fd_set ibits, obits, ebits;

	FD_ZERO(&ibits);
	FD_ZERO(&obits);
	FD_ZERO(&ebits);

	if (fcc)
	    FD_SET(p, &obits);
	else
	    FD_SET(f, &ibits);
	if (pcc >= 0)
	    if (pcc)
		FD_SET(f, &obits);
	    else
		FD_SET(p, &ibits);
	FD_SET(p, &ebits);
	
	if (select(8*sizeof(ibits), &ibits, &obits, &ebits, 0) < 0) {
	    if (errno == EINTR)
	      continue;
	    fatalperror(f, "select");
	}
#define	pkcontrol(c)	((c)&(TIOCPKT_FLUSHWRITE|TIOCPKT_NOSTOP|TIOCPKT_DOSTOP))
	if (FD_ISSET(p, &ebits)) {
	    cc = read(p, &cntl, 1);
	    if (cc == 1 && pkcontrol(cntl)) {
		cntl |= oobdata[0];
		send(f, &cntl, 1, MSG_OOB);
		if (cntl & TIOCPKT_FLUSHWRITE) {
		    pcc = 0;
		    FD_CLR(p, &ibits);
		}
	    }
	}
	if (FD_ISSET(f, &ibits)) {
	    fcc = (*des_read)(f, fibuf, sizeof (fibuf));
	    if (fcc < 0 && ((errno == EWOULDBLOCK) || (errno == EAGAIN)))
	      fcc = 0;
	    else {
		register unsigned char *cp;
		int left, n;
		
		if (fcc <= 0)
		  break;
		fbp = fibuf;
		
	      top:
		for (cp = fibuf; cp < fibuf+fcc-1; cp++)
		  if (cp[0] == magic[0] &&
		      cp[1] == magic[1]) {
		      left = fcc - (cp-fibuf);
		      n = control(p, cp, left);
		      if (n) {
			  left -= n;
			  if (left > 0)
			    memmove(cp, cp+n, left);
			  fcc -= n;
			  goto top; /* n^2 */
		      }
		  }
	    }
	}
	
	if (FD_ISSET(p, &obits) && fcc > 0) {
	    cc = write(p, fbp, fcc);
	    if (cc > 0) {
		fcc -= cc;
		fbp += cc;
	    }
	}
	
	if (FD_ISSET(p, &ibits)) {
	    pcc = read(p, pibuf, sizeof (pibuf));
	    pbp = pibuf;
	    if (pcc < 0 && ((errno == EWOULDBLOCK) || (errno == EAGAIN)))
	      pcc = 0;
	    else if (pcc <= 0)
	      break;
	    else if (pibuf[0] == 0)
	      pbp++, pcc--;
#ifndef sun
	    else {
		if (pkcontrol(pibuf[0])) {
		    pibuf[0] |= oobdata[0];
		    send(f, &pibuf[0], 1, MSG_OOB);
		}
		pcc = 0;
	    }
#endif
	}
	if (FD_ISSET(f, &obits) && pcc > 0) {
	    cc = (*des_write)(f, pbp, pcc);
	    if (cc < 0 && ((errno == EWOULDBLOCK) || (errno == EAGAIN))) {
		/* also shouldn't happen */
		sleep(5);
		continue;
	    }
	    if (cc > 0) {
		pcc -= cc;
		pbp += cc;
	    }
	}
    }
}



krb5_sigtype cleanup()
{
pty_cleanup (line, pid, 1);
    shutdown(netf, 2);
    exit(1);
}


void fatal(f, msg)
     int f;
     char *msg;
{
    char buf[512];
    int out = 1 ;          /* Output queue of f */
#ifdef POSIX_SIGNALS
    struct sigaction sa;
#endif
    
    buf[0] = '\01';		/* error indicator */
    (void) sprintf(buf + 1, "%s: %s.\r\n",progname, msg);
    if ((f == netf) && (pid > 0))
      (void) (*des_write)(f, buf, strlen(buf));
    else
      (void) write(f, buf, strlen(buf));
    syslog(LOG_ERR,"%s\n",msg);
    if (pid > 0) {
#ifdef POSIX_SIGNALS
	(void) sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	(void) sigaction(SIGCHLD, &sa, (struct sigaction *)0);
#else
	signal(SIGCHLD,SIG_IGN);
#endif
	kill(pid,SIGKILL);
#ifdef  TIOCFLUSH
	(void) ioctl(f, TIOCFLUSH, (char *)&out);
#else
	(void) ioctl(f, TCFLSH, out);
#endif
	cleanup();
    }
    exit(1);
}



void fatalperror(f, msg)
     int f;
     char *msg;
{
    char buf[512];
    extern int sys_nerr;
    extern char *sys_errlist[];
    
    if ((unsigned)errno < sys_nerr)
      (void) sprintf(buf, "%s: %s", msg, sys_errlist[errno]);
    else
      (void) sprintf(buf, "%s: Error %d", msg, errno);
    fatal(f, buf);
}

#ifdef KERBEROS

void
do_krb_login(host)
     char *host;
{
    krb5_error_code status;
    struct passwd *pwd;
    int	passed_krb, passed_rhosts;
    char *msg_fail;

    passed_krb = passed_rhosts = 0;

    if (getuid()) {
	exit(1);
    }

    /* Check authentication. This can be either Kerberos V5, */
    /* Kerberos V4, or host-based. */
    if (status = recvauth()) {
	if (ticket)
	  krb5_free_ticket(bsd_context, ticket);
	if (status != 255)
	  syslog(LOG_ERR,
		 "Authentication failed from %s: %s\n",
		 host,error_message(status));
	fatal(netf, "Kerberos authentication failed");
	return;
    }
    
    /* OK we have authenticated this user - now check authorization. */
    /* The Kerberos authenticated programs must use krb5_kuserok or kuserok*/
    
#ifndef KRB5_KRB4_COMPAT
    if (auth_sys == KRB5_RECVAUTH_V4) {
	  fatal(netf, "This server does not support Kerberos V4");
  }
#endif
    
    if (must_pass_k5 || must_pass_one) {
#if (defined(ALWAYS_V5_KUSEROK) || !defined(KRB5_KRB4_COMPAT))
	/* krb5_kuserok returns 1 if OK */
	if (client && krb5_kuserok(bsd_context, client, lusername))
	    passed_krb++;
#else
	if (auth_sys == KRB5_RECVAUTH_V4) {
	    /* kuserok returns 0 if OK */
	    if (!kuserok(v4_kdata, lusername))
		passed_krb++;
	} else {
	    /* krb5_kuserok returns 1 if OK */
	    if (client && krb5_kuserok(bsd_context, client, lusername))
		passed_krb++;
	}
#endif
    }
    
    /*  The kerberos authenticated request must pass ruserok also
	if asked for. */
    
    if (!must_pass_k5 &&
	(must_pass_rhosts || (!passed_krb && must_pass_one))) {
	/* Cannot check .rhosts unless connection from a privileged port. */
	if (non_privileged) 
	  fatal(netf, "Permission denied - Connection from bad port");

	pwd = (struct passwd *) getpwnam(lusername);
	if (pwd &&
	    !ruserok(rhost_name, pwd->pw_uid == 0, rusername, lusername))
	    passed_rhosts++;
    }

    if ((must_pass_k5 && passed_krb) ||
	(must_pass_rhosts && passed_rhosts) ||
	(must_pass_one && (passed_krb || passed_rhosts)))
	    return;
    
    if (ticket)
	krb5_free_ticket(bsd_context, ticket);

    msg_fail =  (char *) malloc( strlen(krusername) + strlen(lusername) + 80 );
    if (!msg_fail)
	fatal(netf, "User is not authorized to login to specified account");
    sprintf(msg_fail, "User %s is not authorized to login to account %s",
	    krusername, lusername);
    fatal(netf, msg_fail);
    /* NOTREACHED */
}



getstr(fd, buf, cnt, err)
     int fd;
     char *buf;
     int cnt;
     char *err;
{
    
    char c;
    
    do {
	if (read(fd, &c, 1) != 1) {
	    exit(1);
	}
	if (--cnt < 0) {
	    printf("%s too long\r\n", err);
	    exit(1);
	}
	*buf++ = c;
    } while (c != 0);
}



char storage[2*RLOGIND_BUFSIZ];             /* storage for the decryption */
int nstored = 0;
char *store_ptr = storage;

int
v5_des_read(fd, buf, len)
     int fd;
     register char *buf;
     int len;
{
    int nreturned = 0;
    krb5_ui_4 net_len,rd_len;
    int cc,retry;
    unsigned char len_buf[4];
    
    if (!do_encrypt)
      return(read(fd, buf, len));
    
    if (nstored >= len) {
	memcpy(buf, store_ptr, len);
	store_ptr += len;
	nstored -= len;
	return(len);
    } else if (nstored) {
	memcpy(buf, store_ptr, nstored);
	nreturned += nstored;
	buf += nstored;
	len -= nstored;
	nstored = 0;
    }
    
#if 0
    if ((cc = krb5_net_read(bsd_context, fd, (char *)len_buf, 4)) != 4) {
	if ((cc < 0)  && ((errno == EWOULDBLOCK) || (errno == EAGAIN)))
	    return(cc);
	/* XXX can't read enough, pipe must have closed */
	return(0);
    }
    rd_len =
	(((krb5_ui_4)len_buf[0]<<24) |
	 ((krb5_ui_4)len_buf[1]<<16) |
	 ((krb5_ui_4)len_buf[2]<<8) |
	 (krb5_ui_4)len_buf[3]);
#else
	{
	    unsigned char c;
	    int gotzero = 0;

	    /* See the comment in v4_des_read. */
	    do {
		cc = krb5_net_read(bsd_context, fd, &c, 1);
		/* we should check for non-blocking here, but we'd have
		   to make it save partial reads as well. */
		if (cc < 0) return 0; /* read error */
		if (cc == 1) {
		    if (c == 0) gotzero = 1;
		}
	    } while (!gotzero);

	    if ((cc = krb5_net_read(bsd_context, fd, &c, 1)) != 1) return 0;
	    rd_len = c;
	    if ((cc = krb5_net_read(bsd_context, fd, &c, 1)) != 1) return 0;
	    rd_len = (rd_len << 8) | c;
	    if ((cc = krb5_net_read(bsd_context, fd, &c, 1)) != 1) return 0;
	    rd_len = (rd_len << 8) | c;
	}
#endif
    net_len = krb5_encrypt_size(rd_len,eblock.crypto_entry);
    if (net_len < 0 || net_len > sizeof(des_inbuf)) {
	/* XXX preposterous length, probably out of sync.
	   act as if pipe closed */
	syslog(LOG_ERR,"Read size problem.");
	return(0);
    }
    retry = 0;
  datard:
    if ((cc = krb5_net_read(bsd_context,fd,desinbuf.data,net_len)) != net_len) {
	/* XXX can't read enough, pipe must have closed */
	if ((cc < 0)  && ((errno == EWOULDBLOCK) || (errno == EAGAIN))) {
	    retry++;
	    sleep(1);
	    if (retry > MAXRETRIES){
		syslog(LOG_ERR,
		       "des_read retry count exceeded %d\n",
		       retry);
		return(0);
	    }
	    goto datard;
	}
	syslog(LOG_ERR,
	       "Read data received %d != expected %d.",
	       cc, net_len);
	return(0);
    }
    /* decrypt info */
    if ((krb5_decrypt(bsd_context, desinbuf.data,
		      (krb5_pointer) storage,
		      net_len,
		      &eblock, 0))) {
	syslog(LOG_ERR,"Read decrypt problem.");
	return(0);
    }
    store_ptr = storage;
    nstored = rd_len;
    if (nstored > len) {
	memcpy(buf, store_ptr, len);
	nreturned += len;
	store_ptr += len;
	nstored -= len;
    } else {
	memcpy(buf, store_ptr, nstored);
	nreturned += nstored;
	nstored = 0;
    }
    return(nreturned);
}
    

int
v5_des_write(fd, buf, len)
     int fd;
     char *buf;
     int len;
{
    unsigned char len_buf[4];
    
    if (!do_encrypt)
      return(write(fd, buf, len));
    
    
    desoutbuf.length = krb5_encrypt_size(len,eblock.crypto_entry);
    if (desoutbuf.length > sizeof(des_outbuf)){
	syslog(LOG_ERR,"Write size problem.");
	return(-1);
    }
    if ((krb5_encrypt(bsd_context, (krb5_pointer)buf,
		      desoutbuf.data,
		      len,
		      &eblock,
		      0))){
	syslog(LOG_ERR,"Write encrypt problem.");
	return(-1);
    }

    len_buf[0] = (len & 0xff000000) >> 24;
    len_buf[1] = (len & 0xff0000) >> 16;
    len_buf[2] = (len & 0xff00) >> 8;
    len_buf[3] = (len & 0xff);
    (void) write(fd, len_buf, 4);
    if (write(fd, desoutbuf.data,desoutbuf.length) != desoutbuf.length){
	syslog(LOG_ERR,"Could not write out all data.");
	return(-1);
    }
    else return(len);
}
#endif /* KERBEROS */



getpty(fd,slave)
     int *fd;
     char *slave;
{
    char c;
    char *p;
    int i,ptynum;
    struct stat stb;

#ifdef HAVE_OPENPTY
    int slavefd;

    if(openpty(fd, &slavefd, slave, (struct termios *) 0,
         (struct winsize *) 0)) return 1;
    return 0;
#else

    *fd = open("/dev/ptmx", O_RDWR|O_NDELAY);	/* Solaris, IRIX */
    if (*fd < 0) *fd = open("/dev/ptc", O_RDWR|O_NDELAY); /* AIX */
    if (*fd < 0) *fd = open("/dev/pty", O_RDWR|O_NDELAY); /* sysvimp */

    if (*fd >= 0) {

#ifdef HAVE_GRANTPT
	if (grantpt(*fd) || unlockpt(*fd)) return 1;
#endif
    
#ifdef HAVE_PTSNAME
	p = ptsname(*fd);
#else
#ifdef	HAVE_TTYNAME
	p = ttyname(*fd);
#else
	/* XXX If we don't have either what do we do */
#endif
#endif
	if (p) {
	    strcpy(slave, p);
	    return 0;
	}

	if (fstat(*fd, &stb) < 0) {
	    close(*fd);
	    return 1;
	}
	ptynum = (int)(stb.st_rdev&0xFF);
	sprintf(slave, "/dev/ttyp%x", ptynum);
	return 0;

    } else {
    
	for (c = 'p'; c <= 's'; c++) {
	    sprintf(slave,"/dev/ptyXX");
	    slave[strlen("/dev/pty")] = c;
	    slave[strlen("/dev/ptyp")] = '0';
	    if (stat(slave, &stb) < 0)
		break;
	    for (i = 0; i < 16; i++) {
		slave[sizeof("/dev/ptyp") - 1] = "0123456789abcdef"[i];
		*fd = open(slave, O_RDWR);
		if (*fd < 0) continue;

		/* got pty */
		slave[strlen("/dev/")] = 't';
		return 0;
	    }
	}
	return 1;
    }
#endif /* HAVE_OPENPTY */
}



void usage()
{
#ifdef KERBEROS
    syslog(LOG_ERR, 
	   "usage: klogind [-rRkKxpP] [-D port] or [r/R][k/K][x/e][p/P]logind");
#else
    syslog(LOG_ERR, 
	   "usage: rlogind [-rRpP] [-D port] or [r/R][p/P]logind");
#endif
}



#ifdef KERBEROS
int princ_maps_to_lname(principal, luser)	
     krb5_principal principal;
     char *luser;
{
    char kuser[10];
    if (!(krb5_aname_to_localname(bsd_context, principal,
				  sizeof(kuser), kuser))
	&& (strcmp(kuser, luser) == 0)) {
	return 1;
    }
    return 0;
}

int default_realm(principal)
     krb5_principal principal;
{
    char *def_realm;
    int realm_length;
    int retval;
    
    realm_length = krb5_princ_realm(bsd_context, principal)->length;
    
    if (retval = krb5_get_default_realm(bsd_context, &def_realm)) {
	return 0;
    }
    
    if ((realm_length != strlen(def_realm)) ||
	(memcmp(def_realm, krb5_princ_realm(bsd_context, principal)->data, realm_length))) {
	free(def_realm);
	return 0;
    }	
    free(def_realm);
    return 1;
}


#ifndef KRB_SENDAUTH_VLEN
#define	KRB_SENDAUTH_VLEN 8	    /* length for version strings */
#endif

#define	KRB_SENDAUTH_VERS	"AUTHV0.1" /* MUST be KRB_SENDAUTH_VLEN
					      chars */

krb5_error_code
recvauth()
{
    krb5_auth_context auth_context = NULL;
    krb5_error_code status;
    struct sockaddr_in peersin, laddr;
    char krb_vers[KRB_SENDAUTH_VLEN + 1];
    int len;
    krb5_principal server;
    krb5_data inbuf;
    char v4_instance[INST_SZ];	/* V4 Instance */
    char v4_version[9];

    len = sizeof(laddr);
    if (getsockname(netf, (struct sockaddr *)&laddr, &len)) {
	    exit(1);
    }
	
    len = sizeof(peersin);
    if (getpeername(netf, (struct sockaddr *)&peersin, &len)) {
	syslog(LOG_ERR, "get peer name failed %d", netf);
	exit(1);
    }

    if (status = krb5_sname_to_principal(bsd_context, NULL, "host", 
					 KRB5_NT_SRV_HST, &server)) {
	    syslog(LOG_ERR, "parse server name %s: %s", "host",
		   error_message(status));
	    exit(1);
    }

    strcpy(v4_instance, "*");

    if (status = krb5_auth_con_init(bsd_context, &auth_context))
        return status;
 
    /* Only need remote address for rd_cred() to verify client */
    if (status = krb5_auth_con_genaddrs(bsd_context, auth_context, netf,
			KRB5_AUTH_CONTEXT_GENERATE_REMOTE_FULL_ADDR))
	return status;

    if (status = krb5_compat_recvauth(bsd_context, &auth_context, &netf,
				  "KCMDV0.1",
				  server, 	/* Specify daemon principal */
				  0, 		/* no flags */
				  keytab, /* normally NULL to use v5srvtab */

				  do_encrypt ? KOPT_DO_MUTUAL : 0, /*v4_opts*/
				  "rcmd", 	/* v4_service */
				  v4_instance, 	/* v4_instance */
				  &peersin, 	/* foriegn address */
				  &laddr, 	/* our local address */
				  "", 		/* use default srvtab */

				  &ticket, 	/* return ticket */
				  &auth_sys, 	/* which authentication system*/
				  &v4_kdata, v4_schedule, v4_version)) {

	if (auth_sys == KRB5_RECVAUTH_V5) {
	    /*
	     * clean up before exiting
	     */
	    getstr(netf, lusername, sizeof (lusername), "locuser");
	    getstr(netf, term, sizeof(term), "Terminal type");
	    getstr(netf, rusername, sizeof(rusername), "remuser");
	}
	return status;
    }

    getstr(netf, lusername, sizeof (lusername), "locuser");
    getstr(netf, term, sizeof(term), "Terminal type");

#ifdef KRB5_KRB4_COMPAT
    if (auth_sys == KRB5_RECVAUTH_V4) {

	des_read  = v4_des_read;
	des_write = v4_des_write;

	/* We do not really know the remote user's login name.
         * Assume it to be the same as the first component of the
	 * principal's name. 
         */
	strcpy(rusername, v4_kdata->pname);
	krusername = (char *) malloc(strlen(v4_kdata->pname) + 1 +
				     strlen(v4_kdata->pinst) + 1 +
				     strlen(v4_kdata->prealm) + 1);
	sprintf(krusername, "%s/%s@%s", v4_kdata->pname,
		v4_kdata->pinst, v4_kdata->prealm);
	
	if (status = krb5_parse_name(bsd_context, krusername, &client))
	  return(status);
	return 0;
    }
#endif

    /* Must be V5  */
	
    if (status = krb5_copy_principal(bsd_context, ticket->enc_part2->client, 
				     &client))
	return status;

    des_read  = v5_des_read;
    des_write = v5_des_write;

    getstr(netf, rusername, sizeof(rusername), "remuser");

    if (status = krb5_unparse_name(bsd_context, client, &krusername))
	return status;
    
    /* Setup up eblock if encrypted login session */
    /* otherwise zero out session key */
    if (do_encrypt) {
	krb5_use_enctype(bsd_context, &eblock,
			 ticket->enc_part2->session->enctype);
	if (status = krb5_process_key(bsd_context, &eblock,
				      ticket->enc_part2->session))
	    fatal(netf, "Permission denied");
    }      

    if (status = krb5_read_message(bsd_context, (krb5_pointer)&netf, &inbuf))
	fatal(netf, "Error reading message");

    if ((inbuf.length) && /* Forwarding being done, read creds */
	(status = rd_and_store_for_creds(bsd_context, auth_context, &inbuf, 
					  ticket, lusername))) {
         fatal(netf, "Can't get forwarded credentials");
    }
    return 0;
}


#ifdef KRB5_KRB4_COMPAT

int
v4_des_read(fd, buf, len)
int fd;
register char *buf;
int len;
{
	int nreturned = 0;
	krb5_ui_4 net_len, rd_len;
	int cc;
	unsigned char len_buf[4];

	if (!do_encrypt)
		return(read(fd, buf, len));

	if (nstored >= len) {
		memcpy(buf, store_ptr, len);
		store_ptr += len;
		nstored -= len;
		return(len);
	} else if (nstored) {
		memcpy(buf, store_ptr, nstored);
		nreturned += nstored;
		buf += nstored;
		len -= nstored;
		nstored = 0;
	}

#if 0
	if ((cc = krb_net_read(fd, (char *)len_buf, 4)) != 4) {
		/* XXX can't read enough, pipe
		   must have closed */
		return(0);
	}
 	net_len = (((krb5_ui_4)len_buf[0]<<24) |
		   ((krb5_ui_4)len_buf[1]<<16) |
		   ((krb5_ui_4)len_buf[2]<<8) |
		   (krb5_ui_4)len_buf[3]);
#else
	{
	    unsigned char c;
	    int gotzero = 0;

	    /* We're fetching the length which is MSB first, and the MSB
	       has to be zero unless the client is sending more than 2^24
	       (16M) bytes in a single write (which is why this code is in
	       rlogin but not rcp or rsh.) The only reasons we'd get something
	       other than zero are:
	           -- corruption of the tcp stream (which will show up when
		      everything else is out of sync too)
		   -- un-caught Berkeley-style "pseudo out-of-band data" which
		      happens any time the user hits ^C twice.
	       The latter is *very* common, as shown by an 'rlogin -x -d' 
	       using the CNS V4 rlogin.         Mark EIchin 1/95
	      */
	    do {
		cc = krb_net_read(fd, &c, 1);
		if (cc < 0) return 0; /* read error */
		if (cc == 1) {
		    if (c == 0) gotzero = 1;
		}
	    } while (!gotzero);

	    if ((cc = krb_net_read(fd, &c, 1)) != 1) return 0;
	    net_len = c;
	    if ((cc = krb_net_read(fd, &c, 1)) != 1) return 0;
	    net_len = (net_len << 8) | c;
	    if ((cc = krb_net_read(fd, &c, 1)) != 1) return 0;
	    net_len = (net_len << 8) | c;
	}

#endif
	if (net_len < 0 || net_len > sizeof(des_inbuf)) {
		/* XXX preposterous length, probably out of sync.
		   act as if pipe closed */
		return(0);
	}
	/* the writer tells us how much real data we are getting, but
	   we need to read the pad bytes (8-byte boundary) */
	rd_len = roundup(net_len, 8);
	if ((cc = krb_net_read(fd, des_inbuf, rd_len)) != rd_len) {
		/* XXX can't read enough, pipe
		   must have closed */
		return(0);
	}
	(void) pcbc_encrypt(des_inbuf,
			    storage,
			    (net_len < 8) ? 8 : net_len,
			    v4_schedule,
			    v4_kdata->session,
			    DECRYPT);
	/* 
	 * when the cleartext block is < 8 bytes, it is "right-justified"
	 * in the block, so we need to adjust the pointer to the data
	 */
	if (net_len < 8)
		store_ptr = storage + 8 - net_len;
	else
		store_ptr = storage;
	nstored = net_len;
	if (nstored > len) {
		memcpy(buf, store_ptr, len);
		nreturned += len;
		store_ptr += len;
		nstored -= len;
	} else {
		memcpy(buf, store_ptr, nstored);
		nreturned += nstored;
		nstored = 0;
	}
	
	return(nreturned);
}

int
v4_des_write(fd, buf, len)
int fd;
char *buf;
int len;
{
	static char garbage_buf[8];
	unsigned char len_buf[4];

	if (!do_encrypt)
		return(write(fd, buf, len));

	/* 
	 * pcbc_encrypt outputs in 8-byte (64 bit) increments
	 *
	 * it zero-fills the cleartext to 8-byte padding,
	 * so if we have cleartext of < 8 bytes, we want
	 * to insert random garbage before it so that the ciphertext
	 * differs for each transmission of the same cleartext.
	 * if len < 8 - sizeof(long), sizeof(long) bytes of random
	 * garbage should be sufficient; leave the rest as-is in the buffer.
	 * if len > 8 - sizeof(long), just garbage fill the rest.
	 */

#ifdef min
#undef min
#endif
#define min(a,b) ((a < b) ? a : b)

	if (len < 8) {
		krb5_random_confounder(8 - len, garbage_buf);
		/* this "right-justifies" the data in the buffer */
		(void) memcpy(garbage_buf + 8 - len, buf, len);
	}
	(void) pcbc_encrypt((len < 8) ? garbage_buf : buf,
			    des_outbuf,
			    (len < 8) ? 8 : len,
			    v4_schedule,
			    v4_kdata->session,
			    ENCRYPT);

	/* tell the other end the real amount, but send an 8-byte padded
	   packet */
	len_buf[0] = (len & 0xff000000) >> 24;
	len_buf[1] = (len & 0xff0000) >> 16;
	len_buf[2] = (len & 0xff00) >> 8;
	len_buf[3] = (len & 0xff);
	(void) write(fd, len_buf, 4);
	(void) write(fd, des_outbuf, roundup(len,8));
	return(len);
}

#endif /* KRB5_KRB4_COMPAT */
#endif /* KERBEROS */
