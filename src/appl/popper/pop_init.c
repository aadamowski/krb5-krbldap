/*
 * Copyright (c) 1989 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * added kerberos authentication (tom coppeto, 1/15/91)
 */

#ifndef lint
static char copyright[] = "Copyright (c) 1990 Regents of the University of California.\nAll rights reserved.\n";
static char SccsId[] = "@(#)pop_init.c  1.12    8/16/90";
#endif not lint

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "popper.h"

#ifdef KERBEROS
#include <krb.h>
    
AUTH_DAT kdata;
#endif /* KERBEROS */

extern int      errno;
int             sp = 0;             /*  Socket pointer */
                                    /*  we use this later */
/* 
 *  init:   Start a Post Office Protocol session
 */

pop_init(p,argcount,argmessage)
POP     *       p;
int             argcount;
char    **      argmessage;
{

    struct sockaddr_in      cs;                 /*  Communication parameters */
    struct hostent      *   ch;                 /*  Client host information */
    int                     errflag = 0;
    int                     c;
    int                     len;
    extern char         *   optarg;
    int                     options = 0;
    char                *   trace_file_name;

    /*  Initialize the POP parameter block */
    bzero ((char *)p,(int)sizeof(POP));

    /*  Save my name in a global variable */
    p->myname = argmessage[0];

    /*  Get the name of our host */
    (void)gethostname(p->myhost,MAXHOSTNAMELEN);

    /*  Open the log file */
#ifdef SYSLOG42
    (void)openlog(p->myname,0);
#else
    (void)openlog(p->myname,POP_LOGOPTS,POP_FACILITY);
#endif

    /*  Process command line arguments */
    while ((c = getopt(argcount,argmessage,"dt:")) != EOF)
        switch (c) {

            /*  Debugging requested */
            case 'd':
                p->debug++;
                options |= SO_DEBUG;
                break;

            /*  Debugging trace file specified */
            case 't':
                p->debug++;
                if ((p->trace = fopen(optarg,"a+")) == NULL) {
                    pop_log(p,POP_ERROR,
                        "Unable to open trace file \"%s\", err = %d",
                            optarg,errno);
                    exit(-1);
                }
                trace_file_name = optarg;
                break;

            /*  Unknown option received */
            default:
                errflag++;
        }

    /*  Exit if bad options specified */
    if (errflag) {
        (void)fprintf(stderr,"Usage: %s [-d]\n",argmessage[0]);
        exit(-1);
    }

    /*  Get the address and socket of the client to whom I am speaking */
    len = sizeof(cs);
    if (getpeername(sp,(struct sockaddr *)&cs,&len) < 0){
        pop_log(p,POP_ERROR,
            "Unable to obtain socket and address of client, err = %d",errno);
        exit(-1);
    }

    /*  Save the dotted decimal form of the client's IP address 
        in the POP parameter block */
    p->ipaddr = inet_ntoa(cs.sin_addr);

    /*  Save the client's port */
    p->ipport = ntohs(cs.sin_port);

    /*  Get the canonical name of the host to whom I am speaking */
    ch = gethostbyaddr((char *) &cs.sin_addr, sizeof(cs.sin_addr), AF_INET);
    if (ch == NULL){
        pop_log(p,POP_PRIORITY,
            "%s: unable to get canonical name of client, err = %d", 
		p->ipaddr, errno);
        p->client = p->ipaddr;
    }
    /*  Save the cannonical name of the client host in 
        the POP parameter block */
    else {

#ifndef BIND43HACK
        p->client = ch->h_name;
#else
	/*
	 * There's no reason for this. In any case, reset res options.
	 */

#       include <arpa/nameser.h>
#       include <resolv.h>

        /*  Distrust distant nameservers */
        extern struct state     _res;
        struct hostent      *   ch_again;
        char            *   *   addrp;

        /*  We already have a fully-qualified name */
        _res.options &= ~RES_DEFNAMES;

        /*  See if the name obtained for the client's IP 
            address returns an address */
        if ((ch_again = gethostbyname(ch->h_name)) == NULL) {
	  pop_log(p,POP_PRIORITY,
                "%s: resolves to an unknown host name \"%s\".",
                    p->ipaddr,ch->h_name);
            p->client = p->ipaddr;
        }
        else {
            /*  Save the host name (the previous value was 
                destroyed by gethostbyname) */
            p->client = ch_again->h_name;

            /*  Look for the client's IP address in the list returned 
                for its name */
            for (addrp=ch_again->h_addr_list; *addrp; ++addrp)
                if (bcmp(*addrp,&(cs.sin_addr),sizeof(cs.sin_addr)) == 0) break;

            if (!*addrp) {
                pop_log (p,POP_PRIORITY,
			 "%s: not listed for its host name \"%s\".",
			 p->ipaddr,ch->h_name);
                p->client = p->ipaddr;
            }
        }
	/*  restore bind state */
        _res.options |= RES_DEFNAMES;

#endif BIND43HACK
    }

    fcntl(sp, F_SETFL, SO_KEEPALIVE);

    /*  Create input file stream for TCP/IP communication */
    if ((p->input = fdopen(sp,"r")) == NULL){
        pop_log(p,POP_ERROR,
            "%s: unable to open communication stream for input, err = %d",
		p->client, errno);
        exit (-1);
    }

    /*  Create output file stream for TCP/IP communication */
    if ((p->output = fdopen(sp,"w")) == NULL){
      pop_log(p,POP_ERROR,
	      "%s: unable to open communication stream for output, err = %d",
	      p->client, errno);
        exit (-1);
    }

    pop_log(p,POP_INFO,
        "%s: (v%s) Servicing request at %s\n", p->client, VERSION, p->ipaddr);

#ifdef DEBUG
    if (p->trace)
        pop_log(p,POP_PRIORITY,
            "Tracing session and debugging information in file \"%s\"",
                trace_file_name);
    else if (p->debug)
        pop_log(p,POP_PRIORITY,"Debugging turned on");
#endif DEBUG
    
    return(authenticate(p, &cs));
}



authenticate(p, addr)
     POP     *p;
     struct sockaddr_in *addr;
{

#ifdef KERBEROS
    Key_schedule schedule;
    KTEXT_ST ticket;
    char instance[INST_SZ];  
    char version[9];
    int auth;
  
    strcpy(instance, "*");
    auth = krb_recvauth(0L, 0, &ticket, "pop", instance,
	  	        addr, (struct sockaddr_in *) NULL,
			&kdata, "", schedule, version);
    
    if (auth != KSUCCESS) {
        pop_msg(p, POP_FAILURE, "Kerberos authentication failure: %s", 
		krb_err_txt[auth]);
	pop_log(p, POP_WARNING, "%s: (%s.%s@%s) %s", p->client, 
		kdata.pname, kdata.pinst, kdata.prealm, krb_err_txt[auth]);
        exit(-1);
    }

#ifdef DEBUG
    pop_log(p, POP_DEBUG, "%s.%s@%s (%s): ok", kdata.pname, 
	    kdata.pinst, kdata.prealm, inet_ntoa(addr->sin_addr));
#endif /* DEBUG */

    strcpy(p->user, kdata.pname);

#endif /* KERBEROS */

    return(POP_SUCCESS);
}
