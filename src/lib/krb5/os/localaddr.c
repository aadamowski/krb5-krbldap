/*
 * lib/krb5/os/localaddr.c
 *
 * Copyright 1990,1991,2000 by the Massachusetts Institute of Technology.
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
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * Return the protocol addresses supported by this host.
 *
 * XNS support is untested, but "Should just work".
 */

#define NEED_SOCKETS
#include "k5-int.h"

#if !defined(HAVE_MACSOCK_H) && !defined(_MSDOS) && !defined(_WIN32)

/* needed for solaris, harmless elsewhere... */
#define BSD_COMP
#include <sys/ioctl.h>
#include <sys/time.h>
#include <errno.h>
#include <stddef.h>

/*
 * The SIOCGIF* ioctls require a socket.
 * It doesn't matter *what* kind of socket they use, but it has to be
 * a socket.
 *
 * Of course, you can't just ask the kernel for a socket of arbitrary
 * type; you have to ask for one with a valid type.
 *
 */
#ifdef HAVE_NETINET_IN_H

#include <netinet/in.h>

#ifndef USE_AF
#define USE_AF AF_INET
#define USE_TYPE SOCK_DGRAM
#define USE_PROTO 0
#endif

#endif

#ifdef KRB5_USE_NS

#include <netns/ns.h>

#ifndef USE_AF
#define USE_AF AF_NS
#define USE_TYPE SOCK_DGRAM
#define USE_PROTO 0		/* guess */
#endif

#endif
/*
 * Add more address families here.
 */

extern int errno;

/*
 * Return all the protocol addresses of this host.
 *
 * We could kludge up something to return all addresses, assuming that
 * they're valid kerberos protocol addresses, but we wouldn't know the
 * real size of the sockaddr or know which part of it was actually the
 * host part.
 *
 * This uses the SIOCGIFCONF, SIOCGIFFLAGS, and SIOCGIFADDR ioctl's.
 */

struct localaddr_data {
    int count, mem_err, cur_idx;
    krb5_address **addr_temp;
};

static int
count_addrs (void *P_data, struct sockaddr *a)
{
    struct localaddr_data *data = P_data;
    switch (a->sa_family) {
    case AF_INET:
#ifdef KRB5_USE_INET6
    case AF_INET6:
#endif
#ifdef KRB5_USE_NS
    case AF_XNS:
#endif
	data->count++;
	break;
    default:
	break;
    }
    return 0;
}

static int
allocate (void *P_data)
{
    struct localaddr_data *data = P_data;
    int i;

    data->addr_temp = (krb5_address **) malloc ((1 + data->count) * sizeof (krb5_address *));
    if (data->addr_temp == 0)
	return 1;
    for (i = 0; i <= data->count; i++)
	data->addr_temp[i] = 0;
    return 0;
}

static int
add_addr (void *P_data, struct sockaddr *a)
{
    struct localaddr_data *data = P_data;
    krb5_address *address = 0;

    switch (a->sa_family) {
#ifdef HAVE_NETINET_IN_H
    case AF_INET:
    {
	struct sockaddr_in *in = (struct sockaddr_in *) a;

	address = (krb5_address *) malloc (sizeof(krb5_address));
	if (address) {
	    address->magic = KV5M_ADDRESS;
	    address->addrtype = ADDRTYPE_INET;
	    address->length = sizeof(struct in_addr);
	    address->contents = (unsigned char *)malloc(address->length);
	    if (!address->contents) {
		krb5_xfree(address);
		address = 0;
		data->mem_err++;
	    } else {
		memcpy ((char *)address->contents, (char *)&in->sin_addr, 
			address->length);
		break;
	    }
	} else
	    data->mem_err++;
    }

#ifdef KRB5_USE_INET6
    case AF_INET6:
    {
	struct sockaddr_in6 *in = (struct sockaddr_in6 *) a;
	
	if (IN6_IS_ADDR_LINKLOCAL (&in->sin6_addr))
	    return 0;
	
	address = (krb5_address *) malloc (sizeof(krb5_address));
	if (address) {
	    address->magic = KV5M_ADDRESS;
	    address->addrtype = ADDRTYPE_INET6;
	    address->length = sizeof(struct in6_addr);
	    address->contents = (unsigned char *)malloc(address->length);
	    if (!address->contents) {
		krb5_xfree(address);
		address = 0;
		data->mem_err++;
	    } else {
		memcpy ((char *)address->contents, (char *)&in->sin6_addr,
			address->length);
		break;
	    }
	} else
	    data->mem_err++;
    }
#endif /* KRB5_USE_INET6 */
#endif /* netinet/in.h */

#ifdef KRB5_USE_NS
    case AF_XNS:
    {  
	struct sockaddr_ns *ns = (struct sockaddr_ns *) a;
	address = (krb5_address *)
	    malloc (sizeof (krb5_address) + sizeof (struct ns_addr));
	if (address) {
	    address->magic = KV5M_ADDRESS;
	    address->addrtype = ADDRTYPE_XNS; 

	    /* XXX should we perhaps use ns_host instead? */

	    address->length = sizeof(struct ns_addr);
	    address->contents = (unsigned char *)malloc(address->length);
	    if (!address->contents) {
		krb5_xfree(address);
		address = 0;
		data->mem_err++;
	    } else {
		memcpy ((char *)address->contents,
			(char *)&ns->sns_addr,
			address->length);
		break;
	    }
	} else
	    data->mem_err++;
	break;
    }
#endif
    /*
     * Add more address families here..
     */
    default:
	break;
    }
    if (address) {
	data->addr_temp[data->cur_idx++] = address;
    }

    return data->mem_err;
}

/* Keep this in sync with kdc/network.c version.  */

/*
 * BSD 4.4 defines the size of an ifreq to be
 * max(sizeof(ifreq), sizeof(ifreq.ifr_name)+ifreq.ifr_addr.sa_len
 * However, under earlier systems, sa_len isn't present, so the size is 
 * just sizeof(struct ifreq)
 */
#ifdef HAVE_SA_LEN
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#define ifreq_size(i) max(sizeof(struct ifreq),\
     sizeof((i).ifr_name)+(i).ifr_addr.sa_len)
#else
#define ifreq_size(i) sizeof(struct ifreq)
#endif /* HAVE_SA_LEN*/

/* SIOCGIFCONF:

   The behavior of this ioctl varies across systems.

   NetBSD 1.5-alpha: The returned ifc_len is the desired amount of
   space, always.  The returned list may be truncated if there isn't
   enough room; no overrun.

   Solaris 2.7: Return EINVAL if the buffer space is too small,
   including ifc_len==0.  (Not sure if this is "too small for a single
   entry" or "too small for the entire list"; my Sun has only one
   interface.)  Solaris is the only system I've found so far that
   actually returns an error.

   AIX 4.3.3: Sometimes the returned ifc_len is bigger than the
   supplied one, but it may not be big enough for *all* the
   interfaces.  Sometimes it's smaller than the supplied value, even
   if the returned list is truncated.  The list is filled in with as
   many entries as will fit; no overrun.

   Linux 2.2.12 (RH 6.1 dist, x86): The buffer is filled in with as
   many entries as will fit, and the size used is returned in ifc_len.
   The list is truncated if needed, with no indication.

   IRIX 6.5: The buffer is filled in with as many entries as will fit
   in N-1 bytes, and the size used is returned in ifc_len.  Providing
   exactly the desired number of bytes is inadequate; the buffer must
   be *bigger* than needed.  (E.g., 32->0, 33->32.)  The returned
   ifc_len is always less than the supplied one.

   Digital UNIX 4.0F: If input ifc_len is zero, return an ifc_len
   that's big enough to include all entries.  (Actually, on our
   system, it appears to be larger than that by 32.)  If input ifc_len
   is nonzero, fill in as many entries as will fit, and set ifc_len
   accordingly.

   Using this ioctl is going to be messy.  Let's just hope that
   getifaddrs() catches on quickly....  */

static int
foreach_localaddr (data, pass1fn, betweenfn, pass2fn)
    void *data;
    int (*pass1fn) (void *, struct sockaddr *);
    int (*betweenfn) (void *);
    int (*pass2fn) (void *, struct sockaddr *);
{
    struct ifreq *ifr, ifreq, *ifr2;
    struct ifconf ifc;
    int s, code, n, i, j;
    int est_if_count = 8, est_ifreq_size;
    char *buf = 0;
    size_t current_buf_size = 0;
    int fail = 0;
#ifdef SIOCGSIZIFCONF
    int ifconfsize = -1;
#endif

    s = socket (USE_AF, USE_TYPE, USE_PROTO);
    if (s < 0)
	return SOCKET_ERRNO;

    /* At least on NetBSD, an ifreq can hold an IPv4 address, but
       isn't big enough for an IPv6 or ethernet address.  So add a
       little more space.  */
    est_ifreq_size = sizeof (struct ifreq) + 16;
#ifdef SIOCGSIZIFCONF
    code = ioctl (s, SIOCGSIZIFCONF, &ifconfsize);
    if (!code) {
	current_buf_size = ifconfsize;
	est_if_count = ifconfsize / est_ifreq_size;
    }
#endif
    if (current_buf_size == 0) {
	current_buf_size = est_ifreq_size * est_if_count;
    }
    buf = malloc (current_buf_size);

 ask_again:
    memset(buf, 0, current_buf_size);
    ifc.ifc_len = current_buf_size;
    ifc.ifc_buf = buf;

    code = ioctl (s, SIOCGIFCONF, (char *)&ifc);
    if (code < 0) {
	int retval = errno;
	closesocket (s);
	return retval;
    }
    /* BSD 4.4 and similar systems truncate the address list if the
       supplied buffer isn't big enough.

       Test that the buffer was big enough that another ifreq could've
       fit easily, if the OS wanted to provide one.  That seems to be
       the only indication we get, complicated by the fact that the
       associated address may make the required storage a little
       bigger than the size of an ifreq.  */
#define SLOP (sizeof (struct ifreq) + 128)
    if ((current_buf_size - ifc.ifc_len < sizeof (struct ifreq) + SLOP
	/* On AIX 4.3.3, ifc.ifc_len may be set to a larger size than
	   provided under some circumstances.  On my test system, a
	   supplied value of 32..112 gets me 112, but with no data
	   filled in even at 112.  But larger input ifc_len values get
	   me larger output values, so it's not necessarily the full
	   desired output buffer size.  And as near as I can tell, the
	   ifc_len output has little to do with the offset of the last
	   byte in the buffer actually modified, except that both
	   input and output ifc_len values are higher (i.e., no buffer
	   overrun takes place in my testing).  */
	 || current_buf_size < ifc.ifc_len)
	/* But let's let SIOCGSIZIFCONF dominate, unless we discover
	   it's broken somewhere.  */
#ifdef SIOCGSIZIFCONF
	&& ifconfsize <= 0
#endif
	/* And we need *some* sort of bounds.  */
	&& current_buf_size <= 100000
	) {
	int new_size;
	char *newbuf;

	est_if_count *= 2;
	new_size = est_ifreq_size * est_if_count;
	newbuf = realloc (buf, new_size);
	if (newbuf == 0) {
	    krb5_error_code e = errno;
	    free (buf);
	    return e;
	}
	current_buf_size = new_size;
	buf = newbuf;
	goto ask_again;
    }

    n = ifc.ifc_len;
    if (n > current_buf_size)
	n = current_buf_size;

    /* Note: Apparently some systems put the size (used or wanted?)
       into the start of the buffer, just none that I'm actually
       using.  Fix this when there's such a test system available.
       The Samba mailing list archives mention that NTP looks for the
       size on these systems: *-fujitsu-uxp* *-ncr-sysv4*
       *-univel-sysv*.  [raeburn:20010201T2226-05]  */
    for (i = 0; i < n; i+= ifreq_size(*ifr) ) {
	ifr = (struct ifreq *)((caddr_t) ifc.ifc_buf+i);

	strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof (ifreq.ifr_name));
	if (ioctl (s, SIOCGIFFLAGS, (char *)&ifreq) < 0) {
	skip:
	    /* mark for next pass */
	    ifr->ifr_name[0] = 0;

	    continue;
	}

#ifdef IFF_LOOPBACK
	    /* None of the current callers want loopback addresses.  */
	if (ifreq.ifr_flags & IFF_LOOPBACK)
	    goto skip;
#endif
	/* Ignore interfaces that are down.  */
	if (!(ifreq.ifr_flags & IFF_UP))
	    goto skip;

	/* Make sure we didn't process this address already.  */
	for (j = 0; j < i; j += ifreq_size(*ifr2)) {
	    ifr2 = (struct ifreq *)((caddr_t) ifc.ifc_buf+j);
	    if (ifr2->ifr_name[0] == 0)
		continue;
	    if (ifr2->ifr_addr.sa_family == ifr->ifr_addr.sa_family
		&& ifreq_size (*ifr) == ifreq_size (*ifr2)
		/* Compare address info.  If this isn't good enough --
		   i.e., if random padding bytes turn out to differ
		   when the addresses are the same -- then we'll have
		   to do it on a per address family basis.  */
		&& !memcmp (&ifr2->ifr_addr.sa_data, &ifr->ifr_addr.sa_data,
			    (ifreq_size (*ifr)
			     - offsetof (struct ifreq, ifr_addr.sa_data))))
		goto skip;
	}

	if ((*pass1fn) (data, &ifr->ifr_addr)) {
	    fail = 1;
	    goto punt;
	}
    }

    if (betweenfn && (*betweenfn)(data)) {
	fail = 1;
	goto punt;
    }

    if (pass2fn)
	for (i = 0; i < n; i+= ifreq_size(*ifr) ) {
	    ifr = (struct ifreq *)((caddr_t) ifc.ifc_buf+i);

	    if (ifr->ifr_name[0] == 0)
		/* Marked in first pass to be ignored.  */
		continue;

	    if ((*pass2fn) (data, &ifr->ifr_addr)) {
		fail = 1;
		goto punt;
	    }
	}
 punt:
    closesocket(s);
    free (buf);

    return fail;
}



KRB5_DLLIMP krb5_error_code KRB5_CALLCONV
krb5_os_localaddr(context, addr)
    krb5_context context;
    krb5_address FAR * FAR * FAR *addr;
{
    struct localaddr_data data = { 0 };
    int r;

    r = foreach_localaddr (&data, count_addrs, allocate, add_addr);
    if (r != 0) {
	int i;
	if (data.addr_temp) {
	    for (i = 0; i < data.count; i++)
		krb5_xfree (data.addr_temp[i]);
	    free (data.addr_temp);
	}
	if (r == -1 && data.mem_err)
	    return ENOMEM;
	else
	    return r;
    }

    data.cur_idx++; /* null termination */
    if (data.mem_err)
	return ENOMEM;
    else if (data.cur_idx == data.count)
	*addr = data.addr_temp;
    else {
	/* This can easily happen if we have IPv6 link-local
	   addresses.  Just shorten the array.  */
	*addr = (krb5_address **) realloc (data.addr_temp,
					   (sizeof (krb5_address *)
					    * data.cur_idx));
	if (*addr == 0)
	    /* Okay, shortening failed, but the original should still
	       be intact.  */
	    *addr = data.addr_temp;
    }
    return 0;
}

#else /* Windows/Mac version */

/*
 * Hold on to your lunch!  Backup kludge method of obtaining your
 * local IP address, courtesy of Windows Socket Network Programming,
 * by Robert Quinn
 */
#if defined(_MSDOS) || defined(_WIN32)
static struct hostent *local_addr_fallback_kludge()
{
	static struct hostent	host;
	static SOCKADDR_IN	addr;
	static char *		ip_ptrs[2];
	SOCKET			sock;
	int			size = sizeof(SOCKADDR);
	int			err;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
		return NULL;

	/* connect to arbitrary port and address (NOT loopback) */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(IPPORT_ECHO);
	addr.sin_addr.s_addr = inet_addr("204.137.220.51");

	err = connect(sock, (LPSOCKADDR) &addr, sizeof(SOCKADDR));
	if (err == SOCKET_ERROR)
		return NULL;

	err = getsockname(sock, (LPSOCKADDR) &addr, (int FAR *) size);
	if (err == SOCKET_ERROR)
		return NULL;

	closesocket(sock);

	host.h_name = 0;
	host.h_aliases = 0;
	host.h_addrtype = AF_INET;
	host.h_length = 4;
	host.h_addr_list = ip_ptrs;
	ip_ptrs[0] = (char *) &addr.sin_addr.s_addr;
	ip_ptrs[1] = NULL;

	return &host;
}
#endif

/* No ioctls in winsock so we just assume there is only one networking 
 * card per machine, so gethostent is good enough. 
 */
KRB5_DLLIMP krb5_error_code KRB5_CALLCONV
krb5_os_localaddr (krb5_context context, krb5_address ***addr) {
    char host[64];                              /* Name of local machine */
    struct hostent *hostrec;
    int err, count, i;
    krb5_address ** paddr;

    *addr = 0;
    paddr = 0;
    err = 0;
    
    if (gethostname (host, sizeof(host))) {
        err = SOCKET_ERRNO;
    }

    if (!err) {
	    hostrec = gethostbyname (host);
	    if (hostrec == NULL) {
		    err = SOCKET_ERRNO;
	    }
    }

    if (err) {
	    hostrec = local_addr_fallback_kludge();
	    if (!hostrec)
		    return err;
		else
			err = 0;  /* otherwise we will die at cleanup */
    }

    for (count = 0; hostrec->h_addr_list[count]; count++);


    paddr = (krb5_address **)malloc(sizeof(krb5_address *) * (count+1));
    if (!paddr) {
        err = ENOMEM;
        goto cleanup;
    }

    memset(paddr, 0, sizeof(krb5_address *) * (count+1));

    for (i = 0; i < count; i++)
    {
        paddr[i] = (krb5_address *)malloc(sizeof(krb5_address));
        if (paddr[i] == NULL) {
            err = ENOMEM;
            goto cleanup;
        }

        paddr[i]->magic = KV5M_ADDRESS;
        paddr[i]->addrtype = hostrec->h_addrtype;
        paddr[i]->length = hostrec->h_length;
        paddr[i]->contents = (unsigned char *)malloc(paddr[i]->length);
        if (!paddr[i]->contents) {
            err = ENOMEM;
            goto cleanup;
        }
        memcpy(paddr[i]->contents,
               hostrec->h_addr_list[i],
               paddr[i]->length);
    }

 cleanup:
    if (err) {
        if (paddr) {
            for (i = 0; i < count; i++)
            {
                if (paddr[i]) {
                    if (paddr[i]->contents)
                        free(paddr[i]->contents);
                    free(paddr[i]);
                }
            }
            free(paddr);
        }
    }
    else
        *addr = paddr;

    return(err);
}
#endif
