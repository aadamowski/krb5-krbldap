/*
 * lib/krb5/os/net_write.c
 *
 * Copyright 1987, 1988, 1990 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 */

#define NEED_LOWLEVEL_IO
#define NEED_SOCKETS
#include "k5-int.h"

/*
 * krb5_net_write() writes "len" bytes from "buf" to the file
 * descriptor "fd".  It returns the number of bytes written or
 * a write() error.  (The calling interface is identical to
 * write(2).)
 *
 * XXX must not use non-blocking I/O
 */

int
krb5_net_write(context, fd, buf, len)
    krb5_context context;
    int fd;
    register const char *buf;
    int len;
{
    int cc;
    register int wrlen = len;
    do {
	cc = SOCKET_WRITE((SOCKET)fd, buf, wrlen);
	if (cc < 0) {
	    if (SOCKET_ERRNO == SOCKET_EINTR)
		continue;
#if defined(_MSDOS) || (_WIN32) || defined (macintosh)
            /* XXX this interface sucks! */
            errno = SOCKET_ERRNO;
#endif            
	    return(cc);
	}
	else {
	    buf += cc;
	    wrlen -= cc;
	}
    } while (wrlen > 0);
    return(len);
}
