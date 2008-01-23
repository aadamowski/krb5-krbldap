/*
 * kdc/network.c
 *
 * Copyright 1990,2000,2007 by the Massachusetts Institute of Technology.
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
 * Network code for Kerberos v5 KDC.
 */

#include "k5-int.h"
#include "com_err.h"
#include "kdc_util.h"
#include "extern.h"
#include "kdc5_err.h"
#include "adm_proto.h"
#include <sys/ioctl.h>
#include <syslog.h>

#include <stddef.h>
#include <ctype.h>
#include "port-sockets.h"
#include "socket-utils.h"

#ifdef HAVE_NETINET_IN_H
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
/* for SIOCGIFCONF, etc. */
#include <sys/sockio.h>
#endif
#include <sys/time.h>
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <arpa/inet.h>

#ifndef ARPHRD_ETHER /* OpenBSD breaks on multiple inclusions */
#include <net/if.h>
#endif

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>		/* FIONBIO */
#endif

#include "fake-addrinfo.h"

#ifdef USE_THREADS
#define UDP 0
#define TCP 1

#include <signal.h>

static krb5_error_code mt_dispatch(krb5_data *, krb5_fulladdr *, krb5_fulladdr *, int, int);
static krb5_error_code init_multithread(const char *prog);

static pthread_t main_thread_id;

static int notifier[2];

#include "kdc_request_queue.h"
#endif /* USE_THREADS */

/* Misc utility routines.  */
static void
set_sa_port(struct sockaddr *addr, int port)
{
    switch (addr->sa_family) {
    case AF_INET:
	sa2sin(addr)->sin_port = port;
	break;
#ifdef KRB5_USE_INET6
    case AF_INET6:
	sa2sin6(addr)->sin6_port = port;
	break;
#endif
    default:
	break;
    }
}

static int ipv6_enabled()
{
#ifdef KRB5_USE_INET6
    static int result = -1;
    if (result == -1) {
	int s;
	s = socket(AF_INET6, SOCK_STREAM, 0);
	if (s >= 0) {
	    result = 1;
	    close(s);
	} else
	    result = 0;
    }
    return result;
#else
    return 0;
#endif
}

static int
setreuseaddr(int sock, int value)
{
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
}

#if defined(KRB5_USE_INET6) && defined(IPV6_V6ONLY)
static int
setv6only(int sock, int value)
{
    return setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &value, sizeof(value));
}
#endif

static int
set_pktinfo(int sock, int family)
{
    int sockopt = 1;
    int option = 0, proto = 0;

    switch (family) {
#if defined(IP_PKTINFO) && defined(HAVE_STRUCT_IN_PKTINFO)
    case AF_INET:
	proto = IPPROTO_IP;
	option = IP_PKTINFO;
	break;
#endif
#if defined(IPV6_PKTINFO) && defined(HAVE_STRUCT_IN6_PKTINFO)
    case AF_INET6:
	proto = IPPROTO_IPV6;
	option = IPV6_PKTINFO;
	break;
#endif
    default:
	return EINVAL;
    }
    if (setsockopt(sock, proto, option, &sockopt, sizeof(sockopt)))
	return errno;
    return 0;
}


static const char *paddr (struct sockaddr *sa)
{
    static char buf[100];
    char portbuf[10];
    if (getnameinfo(sa, socklen(sa),
		    buf, sizeof(buf), portbuf, sizeof(portbuf),
		    NI_NUMERICHOST|NI_NUMERICSERV))
	strcpy(buf, "<unprintable>");
    else {
	unsigned int len = sizeof(buf) - strlen(buf);
	char *p = buf + strlen(buf);
	if (len > 2+strlen(portbuf)) {
	    *p++ = '.';
	    len--;
	    strncpy(p, portbuf, len);
	}
    }
    return buf;
}

/* KDC data.  */

#ifdef USE_THREADS
enum kdc_conn_type { CONN_UDP, CONN_UDP_PKTINFO, CONN_TCP_LISTENER, CONN_TCP, CONN_NOTIFIER };
#else
enum kdc_conn_type { CONN_UDP, CONN_UDP_PKTINFO, CONN_TCP_LISTENER, CONN_TCP };
#endif

/* Per-connection info.  */
struct connection {
    int fd;
    enum kdc_conn_type type;
    void (*service)(struct connection *, const char *, int);
    union {
	/* Type-specific information.  */
#if 0
	struct {
	    int x;
	} udp;
	struct {
	    int x;
	} udp_pktinfo;
	struct {
	    int x;
	} tcp_listener;
#endif
	struct {
	    /* connection */
	    struct sockaddr_storage addr_s;
	    socklen_t addrlen;
	    char addrbuf[56];
	    krb5_fulladdr faddr;
	    krb5_address kaddr;
	    /* incoming */
	    size_t bufsiz;
	    size_t offset;
	    char *buffer;
	    size_t msglen;
	    /* outgoing */
	    krb5_data *response;
	    unsigned char lenbuf[4];
	    sg_buf sgbuf[2];
	    sg_buf *sgp;
	    int sgnum;
	    /* crude denial-of-service avoidance support */
	    time_t start_time;
	} tcp;
    } u;
};


#define SET(TYPE) struct { TYPE *data; int n, max; }

/* Start at the top and work down -- this should allow for deletions
   without disrupting the iteration, since we delete by overwriting
   the element to be removed with the last element.  */
#define FOREACH_ELT(set,idx,vvar) \
  for (idx = set.n-1; idx >= 0 && (vvar = set.data[idx], 1); idx--)

#define GROW_SET(set, incr, tmpptr) \
  (((int)(set.max + incr) < set.max					\
    || (((size_t)((int)(set.max + incr) * sizeof(set.data[0]))		\
	 / sizeof(set.data[0]))						\
	!= (set.max + incr)))						\
   ? 0				/* overflow */				\
   : ((tmpptr = realloc(set.data,					\
			(int)(set.max + incr) * sizeof(set.data[0])))	\
      ? (set.data = tmpptr, set.max += incr, 1)				\
      : 0))

/* 1 = success, 0 = failure */
#define ADD(set, val, tmpptr) \
  ((set.n < set.max || GROW_SET(set, 10, tmpptr))			\
   ? (set.data[set.n++] = val, 1)					\
   : 0)

#define DEL(set, idx) \
  (set.data[idx] = set.data[--set.n], 0)

#define FREE_SET_DATA(set) \
  (free(set.data), set.data = 0, set.max = 0)


/* Set<struct connection *> connections; */
static SET(struct connection *) connections;
#define n_sockets	connections.n
#define conns		connections.data

/* Set<u_short> udp_port_data, tcp_port_data; */
static SET(u_short) udp_port_data, tcp_port_data;

#include "cm.h"

static struct select_state sstate;

static krb5_error_code add_udp_port(int port)
{
    int	i;
    void *tmp;
    u_short val;
    u_short s_port = port;

    if (s_port != port)
	return EINVAL;

    FOREACH_ELT (udp_port_data, i, val)
	if (s_port == val)
	    return 0;
    if (!ADD(udp_port_data, s_port, tmp))
	return ENOMEM;
    return 0;
}

static krb5_error_code add_tcp_port(int port)
{
    int	i;
    void *tmp;
    u_short val;
    u_short s_port = port;

    if (s_port != port)
	return EINVAL;

    FOREACH_ELT (tcp_port_data, i, val)
	if (s_port == val)
	    return 0;
    if (!ADD(tcp_port_data, s_port, tmp))
	return ENOMEM;
    return 0;
}


#define USE_AF AF_INET
#define USE_TYPE SOCK_DGRAM
#define USE_PROTO 0
#define SOCKET_ERRNO errno
#include "foreachaddr.h"

struct socksetup {
    const char *prog;
    krb5_error_code retval;
    int udp_flags;
#define UDP_DO_IPV4 1
#define UDP_DO_IPV6 2
};

static struct connection *
add_fd (struct socksetup *data, int sock, enum kdc_conn_type conntype,
	void (*service)(struct connection *, const char *, int))
{
    struct connection *newconn;
    void *tmp;

    if (sock > FD_SETSIZE) {
	data->retval = EMFILE;	/* XXX */
	com_err(data->prog, 0,
		"file descriptor number %d too high", sock);
	return 0;
    }
    newconn = malloc(sizeof(*newconn));
    if (newconn == 0) {
	data->retval = errno;
	com_err(data->prog, errno,
		"cannot allocate storage for connection info");
	return 0;
    }
    if (!ADD(connections, newconn, tmp)) {
	data->retval = errno;
	com_err(data->prog, data->retval, "cannot save socket info");
	free(newconn);
	return 0;
    }

    memset(newconn, 0, sizeof(*newconn));
    newconn->type = conntype;
    newconn->fd = sock;
    newconn->service = service;
    return newconn;
}

static void process_packet(struct connection *, const char *, int);
static void accept_tcp_connection(struct connection *, const char *, int);
static void process_tcp_connection(struct connection *, const char *, int);

static struct connection *
add_udp_fd (struct socksetup *data, int sock, int pktinfo)
{
    return add_fd(data, sock, pktinfo ? CONN_UDP_PKTINFO : CONN_UDP,
		  process_packet);
}

static struct connection *
add_tcp_listener_fd (struct socksetup *data, int sock)
{
    return add_fd(data, sock, CONN_TCP_LISTENER, accept_tcp_connection);
}

static struct connection *
add_tcp_data_fd (struct socksetup *data, int sock)
{
    return add_fd(data, sock, CONN_TCP, process_tcp_connection);
}

static void
delete_fd (struct connection *xconn)
{
    struct connection *conn;
    int i;

    FOREACH_ELT(connections, i, conn)
	if (conn == xconn) {
	    DEL(connections, i);
	    break;
	}
    free(xconn);
}

#ifdef USE_THREADS
static struct connection * get_conn_from_fd (int fd) {
    struct connection *conn;
    int i, found = 0;

    FOREACH_ELT(connections, i, conn)
        if (conn->fd == fd) {
            found = 1;
            break;
        }
    if (found == 1)
        return conn;
    else
        return NULL;
}
#endif

static int
setnbio(int sock)
{
    static const int one = 1;
    return ioctlsocket(sock, FIONBIO, (const void *)&one);
}

static int
setnolinger(int s)
{
    static const struct linger ling = { 0, 0 };
    return setsockopt(s, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
}

/* Returns -1 or socket fd.  */
static int
setup_a_tcp_listener(struct socksetup *data, struct sockaddr *addr)
{
    int sock;

    sock = socket(addr->sa_family, SOCK_STREAM, 0);
    if (sock == -1) {
	com_err(data->prog, errno, "Cannot create TCP server socket on %s",
		paddr(addr));
	return -1;
    }
    set_cloexec_fd(sock);
    if (sock > FD_SETSIZE) {
	close(sock);
	com_err(data->prog, 0, "TCP socket fd number %d (for %s) too high",
		sock, paddr(addr));
	return -1;
    }
    if (setreuseaddr(sock, 1) < 0)
	com_err(data->prog, errno,
		"Cannot enable SO_REUSEADDR on fd %d", sock);
#ifdef KRB5_USE_INET6
    if (addr->sa_family == AF_INET6) {
#ifdef IPV6_V6ONLY
	if (setv6only(sock, 1))
	    com_err(data->prog, errno, "setsockopt(%d,IPV6_V6ONLY,1) failed",
		    sock);
	else
	    com_err(data->prog, 0, "setsockopt(%d,IPV6_V6ONLY,1) worked",
		    sock);
#else
	krb5_klog_syslog(LOG_INFO, "no IPV6_V6ONLY socket option support");
#endif /* IPV6_V6ONLY */
    }
#endif /* KRB5_USE_INET6 */
    if (bind(sock, addr, socklen(addr)) == -1) {
	com_err(data->prog, errno,
		"Cannot bind TCP server socket on %s", paddr(addr));
	close(sock);
	return -1;
    }
    if (listen(sock, 5) < 0) {
	com_err(data->prog, errno, "Cannot listen on TCP server socket on %s",
		paddr(addr));
	close(sock);
	return -1;
    }
    if (setnbio(sock)) {
	com_err(data->prog, errno,
		"cannot set listening tcp socket on %s non-blocking",
		paddr(addr));
	close(sock);
	return -1;
    }
    if (setnolinger(sock)) {
	com_err(data->prog, errno, "disabling SO_LINGER on TCP socket on %s",
		paddr(addr));
	close(sock);
	return -1;
    }
    return sock;
}

static int
setup_tcp_listener_ports(struct socksetup *data)
{
    struct sockaddr_in sin4;
#ifdef KRB5_USE_INET6
    struct sockaddr_in6 sin6;
#endif
    int i, port;

    memset(&sin4, 0, sizeof(sin4));
    sin4.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
    sin4.sin_len = sizeof(sin4);
#endif
    sin4.sin_addr.s_addr = INADDR_ANY;

#ifdef KRB5_USE_INET6
    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
#ifdef SIN6_LEN
    sin6.sin6_len = sizeof(sin6);
#endif
    sin6.sin6_addr = in6addr_any;
#endif

    FOREACH_ELT (tcp_port_data, i, port) {
	int s4, s6;

	set_sa_port((struct sockaddr *)&sin4, htons(port));
	if (!ipv6_enabled()) {
	    s4 = setup_a_tcp_listener(data, (struct sockaddr *)&sin4);
	    if (s4 < 0)
		return -1;
	    s6 = -1;
	} else {
#ifndef KRB5_USE_INET6
	    abort();
#else
	    s4 = s6 = -1;

	    set_sa_port((struct sockaddr *)&sin6, htons(port));

	    s6 = setup_a_tcp_listener(data, (struct sockaddr *)&sin6);
	    if (s6 < 0)
		return -1;

	    s4 = setup_a_tcp_listener(data, (struct sockaddr *)&sin4);
#endif /* KRB5_USE_INET6 */
	}

	/* Sockets are created, prepare to listen on them.  */
	if (s4 >= 0) {
	    FD_SET(s4, &sstate.rfds);
	    if (s4 >= sstate.max)
		sstate.max = s4 + 1;
	    if (add_tcp_listener_fd(data, s4) == 0)
		close(s4);
	    else
		krb5_klog_syslog(LOG_INFO, "listening on fd %d: tcp %s",
				 s4, paddr((struct sockaddr *)&sin4));
	}
#ifdef KRB5_USE_INET6
	if (s6 >= 0) {
	    FD_SET(s6, &sstate.rfds);
	    if (s6 >= sstate.max)
		sstate.max = s6 + 1;
	    if (add_tcp_listener_fd(data, s6) == 0) {
		close(s6);
		s6 = -1;
	    } else
		krb5_klog_syslog(LOG_INFO, "listening on fd %d: tcp %s",
				 s6, paddr((struct sockaddr *)&sin6));
	    if (s4 < 0)
		krb5_klog_syslog(LOG_INFO,
				 "assuming IPv6 socket accepts IPv4");
	}
#endif
    }
    return 0;
}

#if defined(CMSG_SPACE) && defined(HAVE_STRUCT_CMSGHDR) && (defined(IP_PKTINFO) || defined(IPV6_PKTINFO))
union pktinfo {
#ifdef HAVE_STRUCT_IN6_PKTINFO
    struct in6_pktinfo pi6;
#endif
#ifdef HAVE_STRUCT_IN_PKTINFO
    struct in_pktinfo pi4;
#endif
    char c;
};

static int
setup_udp_port_1(struct socksetup *data, struct sockaddr *addr,
		 char *haddrbuf, int pktinfo);

static void
setup_udp_pktinfo_ports(struct socksetup *data)
{
#ifdef IP_PKTINFO
    {
	struct sockaddr_in sa;
	int r;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
	sa.sin_len = sizeof(sa);
#endif
	r = setup_udp_port_1(data, (struct sockaddr *) &sa, "0.0.0.0", 4);
	if (r == 0)
	    data->udp_flags &= ~UDP_DO_IPV4;
    }
#endif
#ifdef IPV6_PKTINFO
    {
	struct sockaddr_in6 sa;
	int r;

	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
#ifdef HAVE_SA_LEN
	sa.sin6_len = sizeof(sa);
#endif
	r = setup_udp_port_1(data, (struct sockaddr *) &sa, "::", 6);
	if (r == 0)
	    data->udp_flags &= ~UDP_DO_IPV6;
    }
#endif
}
#else /* no pktinfo compile-time support */
static void
setup_udp_pktinfo_ports(struct socksetup *data)
{
}
#endif

static int
setup_udp_port_1(struct socksetup *data, struct sockaddr *addr,
		 char *haddrbuf, int pktinfo)
{
    int sock = -1, i, r;
    u_short port;

    FOREACH_ELT (udp_port_data, i, port) {
	sock = socket (addr->sa_family, SOCK_DGRAM, 0);
	if (sock == -1) {
	    data->retval = errno;
	    com_err(data->prog, data->retval,
		    "Cannot create server socket for port %d address %s",
		    port, haddrbuf);
	    return 1;
	}
	set_cloexec_fd(sock);
#ifdef KRB5_USE_INET6
	if (addr->sa_family == AF_INET6) {
#ifdef IPV6_V6ONLY
	    if (setv6only(sock, 1))
		com_err(data->prog, errno,
			"setsockopt(%d,IPV6_V6ONLY,1) failed", sock);
	    else
		com_err(data->prog, 0, "setsockopt(%d,IPV6_V6ONLY,1) worked",
			sock);
#else
	    krb5_klog_syslog(LOG_INFO, "no IPV6_V6ONLY socket option support");
#endif /* IPV6_V6ONLY */
	}
#endif
	set_sa_port(addr, htons(port));
	if (bind (sock, (struct sockaddr *)addr, socklen (addr)) == -1) {
	    data->retval = errno;
	    com_err(data->prog, data->retval,
		    "Cannot bind server socket to port %d address %s",
		    port, haddrbuf);
	    close(sock);
	    return 1;
	}
#if !(defined(CMSG_SPACE) && defined(HAVE_STRUCT_CMSGHDR) && (defined(IP_PKTINFO) || defined(IPV6_PKTINFO)))
	assert(pktinfo == 0);
#endif
	if (pktinfo) {
	    r = set_pktinfo(sock, addr->sa_family);
	    if (r) {
		com_err(data->prog, r,
			"Cannot request packet info for udp socket address %s port %d",
			haddrbuf, port);
		close(sock);
		return 1;
	    }
	}
	FD_SET (sock, &sstate.rfds);
	if (sock >= sstate.max)
	    sstate.max = sock + 1;
	krb5_klog_syslog (LOG_INFO, "listening on fd %d: udp %s%s", sock,
			  paddr((struct sockaddr *)addr),
			  pktinfo ? " (pktinfo)" : "");
	if (add_udp_fd (data, sock, pktinfo) == 0) {
	    close(sock);
	    return 1;
	}
    }
    return 0;
}

static int
setup_udp_port(void *P_data, struct sockaddr *addr)
{
    struct socksetup *data = P_data;
    char haddrbuf[NI_MAXHOST];
    int err;

    if (addr->sa_family == AF_INET && !(data->udp_flags & UDP_DO_IPV4))
	return 0;
#ifdef AF_INET6
    if (addr->sa_family == AF_INET6 && !(data->udp_flags & UDP_DO_IPV6))
	return 0;
#endif
    err = getnameinfo(addr, socklen(addr), haddrbuf, sizeof(haddrbuf),
		      0, 0, NI_NUMERICHOST);
    if (err)
	strcpy(haddrbuf, "<unprintable>");

    switch (addr->sa_family) {
    case AF_INET:
	break;
#ifdef AF_INET6
    case AF_INET6:
#ifdef KRB5_USE_INET6
	break;
#else
	{
	    static int first = 1;
	    if (first) {
		krb5_klog_syslog (LOG_INFO, "skipping local ipv6 addresses");
		first = 0;
	    }
	    return 0;
	}
#endif
#endif
#ifdef AF_LINK /* some BSD systems, AIX */
    case AF_LINK:
	return 0;
#endif
#ifdef AF_DLI /* Direct Link Interface - DEC Ultrix/OSF1 link layer? */
    case AF_DLI:
	return 0;
#endif
#ifdef AF_APPLETALK
    case AF_APPLETALK:
	return 0;
#endif
    default:
	krb5_klog_syslog (LOG_INFO,
			  "skipping unrecognized local address family %d",
			  addr->sa_family);
	return 0;
    }
    return setup_udp_port_1(data, addr, haddrbuf, 0);
}

#ifdef USE_THREADS
static void no_service(struct connection *con, const char *prog, int i)
{
    char c[32];

    while (read(con->fd, c, sizeof (c)) != 0)
        ;
}

static krb5_error_code setup_notifier(struct socksetup *s)
{
    int flag;
    krb5_error_code ret;

    if (pipe(notifier) != 0) {
        ret = errno;
        com_err("krb5kdc", ret, "while creating pipe");
        return ret;
    }
    flag = fcntl(notifier[0], F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(notifier[0], F_SETFL, flag);

    FD_SET(notifier[0], &sstate.rfds);
    if (notifier[0] >= sstate.max)
        sstate.max = notifier[0] + 1;

    if (add_fd(s, notifier[0], CONN_NOTIFIER, no_service) == NULL) {
        return s->retval;
    }

    return 0;
}
#endif /* USE_THREADS */

#if 1
static void klog_handler(const void *data, size_t len)
{
    static char buf[BUFSIZ];
    static int bufoffset;
    void *p;

#define flush_buf() \
  (bufoffset						\
   ? (((buf[0] == 0 || buf[0] == '\n')			\
       ? (fork()==0?abort():(void)0)			\
       : (void)0),					\
      krb5_klog_syslog(LOG_INFO, "%s", buf),		\
      memset(buf, 0, sizeof(buf)),			\
      bufoffset = 0)					\
   : 0)

    p = memchr(data, 0, len);
    if (p)
	len = (const char *)p - (const char *)data;
scan_for_newlines:
    if (len == 0)
	return;
    p = memchr(data, '\n', len);
    if (p) {
	if (p != data)
	    klog_handler(data, (size_t)((const char *)p - (const char *)data));
	flush_buf();
	len -= ((const char *)p - (const char *)data) + 1;
	data = 1 + (const char *)p;
	goto scan_for_newlines;
    } else if (len > sizeof(buf) - 1 || len + bufoffset > sizeof(buf) - 1) {
	size_t x = sizeof(buf) - len - 1;
	klog_handler(data, x);
	flush_buf();
	len -= x;
	data = (const char *)data + x;
	goto scan_for_newlines;
    } else {
	memcpy(buf + bufoffset, data, len);
	bufoffset += len;
    }
}
#endif

/* XXX */
extern int krb5int_debug_sendto_kdc;
extern void (*krb5int_sendtokdc_debug_handler)(const void*, size_t);

krb5_error_code
setup_network(const char *prog)
{
    struct socksetup setup_data;
    krb5_error_code retval;
    char *cp;
    int i, port;

    FD_ZERO(&sstate.rfds);
    FD_ZERO(&sstate.wfds);
    FD_ZERO(&sstate.xfds);
    sstate.max = 0;

/*    krb5int_debug_sendto_kdc = 1; */
    krb5int_sendtokdc_debug_handler = klog_handler;

    /* Handle each realm's ports */
    for (i=0; i<kdc_numrealms; i++) {
	cp = kdc_realmlist[i]->realm_ports;
	while (cp && *cp) {
	    if (*cp == ',' || isspace((int) *cp)) {
		cp++;
		continue;
	    }
	    port = strtol(cp, &cp, 10);
	    if (cp == 0)
		break;
	    retval = add_udp_port(port);
	    if (retval)
		return retval;
	}

	cp = kdc_realmlist[i]->realm_tcp_ports;
	while (cp && *cp) {
	    if (*cp == ',' || isspace((int) *cp)) {
		cp++;
		continue;
	    }
	    port = strtol(cp, &cp, 10);
	    if (cp == 0)
		break;
	    retval = add_tcp_port(port);
	    if (retval)
		return retval;
	}
    }

    setup_data.prog = prog;
    setup_data.retval = 0;
    krb5_klog_syslog (LOG_INFO, "setting up network...");
    /* To do: Use RFC 2292 interface (or follow-on) and IPV6_PKTINFO,
       so we might need only one UDP socket; fall back to binding
       sockets on each address only if IPV6_PKTINFO isn't
       supported.  */
    setup_data.udp_flags = UDP_DO_IPV4 | UDP_DO_IPV6;
    setup_udp_pktinfo_ports(&setup_data);
    if (setup_data.udp_flags) {
	if (foreach_localaddr (&setup_data, setup_udp_port, 0, 0)) {
	    return setup_data.retval;
	}
    }
    setup_tcp_listener_ports(&setup_data);
    krb5_klog_syslog (LOG_INFO, "set up %d sockets", n_sockets);
    if (n_sockets == 0) {
	com_err(prog, 0, "no sockets set up?");
	exit (1);
    }
#ifdef USE_THREADS
    retval = setup_notifier(&setup_data);
    if (retval) {
        return retval;
    }
#endif

    return 0;
}

static void init_addr(krb5_fulladdr *faddr, struct sockaddr *sa)
{
    switch (sa->sa_family) {
    case AF_INET:
	faddr->address->addrtype = ADDRTYPE_INET;
	faddr->address->length = 4;
	faddr->address->contents = (krb5_octet *) &sa2sin(sa)->sin_addr;
	faddr->port = ntohs(sa2sin(sa)->sin_port);
	break;
#ifdef KRB5_USE_INET6
    case AF_INET6:
        faddr->is_ipv6 = 1;
	if (IN6_IS_ADDR_V4MAPPED(&sa2sin6(sa)->sin6_addr)) {
	    faddr->address->addrtype = ADDRTYPE_INET;
	    faddr->address->length = 4;
	    faddr->address->contents = 12 + (krb5_octet *) &sa2sin6(sa)->sin6_addr;
	} else {
	    faddr->address->addrtype = ADDRTYPE_INET6;
	    faddr->address->length = 16;
	    faddr->address->contents = (krb5_octet *) &sa2sin6(sa)->sin6_addr;
	}
	faddr->port = ntohs(sa2sin6(sa)->sin6_port);
	break;
#endif
    default:
	faddr->address->addrtype = -1;
	faddr->address->length = 0;
	faddr->address->contents = 0;
	faddr->port = 0;
	break;
    }
}

static void fulladdr2sockaddr(struct sockaddr *sa, krb5_fulladdr *faddr)
{
    switch (faddr->address->addrtype) {
        case ADDRTYPE_INET:
            assert(faddr->address->length == 4);
#ifdef KRB5_USE_INET6
            if (faddr->is_ipv6) {
                sa->sa_family = AF_INET6;
                bzero((krb5_octet *) &sa2sin6(sa)->sin6_addr, 12);
                memcpy(12 + (krb5_octet *) &sa2sin6(sa)->sin6_addr,
                       faddr->address->contents, 4);
                sa2sin6(sa)->sin6_port = htons(faddr->port);
            } else {
#endif
                sa->sa_family = AF_INET;
                memcpy((krb5_octet *) &sa2sin(sa)->sin_addr,
                       faddr->address->contents, 4);
                sa2sin(sa)->sin_port = htons(faddr->port);
#ifdef KRB5_USE_INET6
            }
#endif
            break;
#ifdef KRB5_USE_INET6
        case ADDRTYPE_INET6:
            sa->sa_family = AF_INET6;
            memcpy((krb5_octet *) &sa2sin6(sa)->sin6_addr,
                   faddr->address->contents, 16);
            sa2sin6(sa)->sin6_port = htons(faddr->port);
            break;
#endif
        default:
            assert(0);
            break;
    }
}

#ifdef USE_THREADS
static krb5_error_code krb5_copy_fulladdr(krb5_context context,
                                          const krb5_fulladdr *in,
                                          krb5_fulladdr **out)
{
    krb5_error_code ret;

    *out = (krb5_fulladdr *)malloc(sizeof(krb5_fulladdr));
    if (*out == NULL) {
        return ENOMEM;
    }
    memset(*out, 0, sizeof(krb5_fulladdr));

    (*out)->port = in->port;

    ret = krb5_copy_addr(context, in->address, &((*out)->address));
    if (ret != 0) {
        free(*out);
        *out = NULL;
        return ret;
    }

    return 0;
}
#endif /* USE_THREADS */

static int
recv_from_to(int s, void *buf, size_t len, int flags,
	     struct sockaddr *from, socklen_t *fromlen,
	     struct sockaddr *to, socklen_t *tolen)
{
#if (!defined(IP_PKTINFO) && !defined(IPV6_PKTINFO)) || !defined(CMSG_SPACE)
    if (to && tolen)
	*tolen = 0;
    return recvfrom(s, buf, len, flags, from, fromlen);
#else
    int r;
    struct iovec iov;
    char cmsg[CMSG_SPACE(sizeof(union pktinfo))];
    struct cmsghdr *cmsgptr;
    struct msghdr msg;

    if (!to || !tolen)
	return recvfrom(s, buf, len, flags, from, fromlen);

    iov.iov_base = buf;
    iov.iov_len = len;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = from;
    msg.msg_namelen = *fromlen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg;
    msg.msg_controllen = sizeof(cmsg);

    r = recvmsg(s, &msg, flags);
    if (r < 0)
	return r;
    *fromlen = msg.msg_namelen;

    /* On Darwin (and presumably all *BSD with KAME stacks),
       CMSG_FIRSTHDR doesn't check for a non-zero controllen.  RFC
       3542 recommends making this check, even though the (new) spec
       for CMSG_FIRSTHDR says it's supposed to do the check.  */
    if (msg.msg_controllen) {
	cmsgptr = CMSG_FIRSTHDR(&msg);
	while (cmsgptr) {
#ifdef IP_PKTINFO
	    if (cmsgptr->cmsg_level == IPPROTO_IP
		&& cmsgptr->cmsg_type == IP_PKTINFO
		&& *tolen >= sizeof(struct sockaddr_in)) {
		struct in_pktinfo *pktinfo;
		memset(to, 0, sizeof(struct sockaddr_in));
		pktinfo = (struct in_pktinfo *)CMSG_DATA(cmsgptr);
		((struct sockaddr_in *)to)->sin_addr = pktinfo->ipi_addr;
		((struct sockaddr_in *)to)->sin_family = AF_INET;
		*tolen = sizeof(struct sockaddr_in);
		return r;
	    }
#endif
#if defined(KRB5_USE_INET6) && defined(IPV6_PKTINFO)
	    if (cmsgptr->cmsg_level == IPPROTO_IPV6
		&& cmsgptr->cmsg_type == IPV6_PKTINFO
		&& *tolen >= sizeof(struct sockaddr_in6)) {
		struct in6_pktinfo *pktinfo;
		memset(to, 0, sizeof(struct sockaddr_in6));
		pktinfo = (struct in6_pktinfo *)CMSG_DATA(cmsgptr);
		((struct sockaddr_in6 *)to)->sin6_addr = pktinfo->ipi6_addr;
		((struct sockaddr_in6 *)to)->sin6_family = AF_INET6;
		*tolen = sizeof(struct sockaddr_in6);
		return r;
	    }
#endif
	    cmsgptr = CMSG_NXTHDR(&msg, cmsgptr);
	}
    }
    /* No info about destination addr was available.  */
    *tolen = 0;
    return r;
#endif
}

static int
send_to_from(int s, void *buf, size_t len, int flags,
	     const struct sockaddr *to, socklen_t tolen,
	     const struct sockaddr *from, socklen_t fromlen)
{
#if (!defined(IP_PKTINFO) && !defined(IPV6_PKTINFO)) || !defined(CMSG_SPACE)
    return sendto(s, buf, len, flags, to, tolen);
#else
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsgptr;
    char cbuf[CMSG_SPACE(sizeof(union pktinfo))];

    if (from == 0 || fromlen == 0 || from->sa_family != to->sa_family) {
    use_sendto:
	return sendto(s, buf, len, flags, to, tolen);
    }

    iov.iov_base = buf;
    iov.iov_len = len;
    /* Truncation?  */
    if (iov.iov_len != len)
	return EINVAL;
    memset(cbuf, 0, sizeof(cbuf));
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *) to;
    msg.msg_namelen = tolen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    /* CMSG_FIRSTHDR needs a non-zero controllen, or it'll return NULL
       on Linux.  */
    msg.msg_controllen = sizeof(cbuf);
    cmsgptr = CMSG_FIRSTHDR(&msg);
    msg.msg_controllen = 0;

    switch (from->sa_family) {
#if defined(IP_PKTINFO)
    case AF_INET:
	if (fromlen != sizeof(struct sockaddr_in))
	    goto use_sendto;
	cmsgptr->cmsg_level = IPPROTO_IP;
	cmsgptr->cmsg_type = IP_PKTINFO;
	cmsgptr->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
	{
	    struct in_pktinfo *p = (struct in_pktinfo *)CMSG_DATA(cmsgptr);
	    const struct sockaddr_in *from4 = (const struct sockaddr_in *)from;
	    p->ipi_spec_dst = from4->sin_addr;
	}
	msg.msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));
	break;
#endif
#if defined(KRB5_USE_INET6) && defined(IPV6_PKTINFO)
    case AF_INET6:
	if (fromlen != sizeof(struct sockaddr_in6))
	    goto use_sendto;
	cmsgptr->cmsg_level = IPPROTO_IPV6;
	cmsgptr->cmsg_type = IPV6_PKTINFO;
	cmsgptr->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	{
	    struct in6_pktinfo *p = (struct in6_pktinfo *)CMSG_DATA(cmsgptr);
	    const struct sockaddr_in6 *from6 = (const struct sockaddr_in6 *)from;
	    p->ipi6_addr = from6->sin6_addr;
	}
	msg.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
	break;
#endif
    default:
	goto use_sendto;
    }
    return sendmsg(s, &msg, flags);
#endif
}

static void process_packet(struct connection *conn, const char *prog,
			   int selflags)
{
    int cc;
    socklen_t saddr_len, daddr_len;
    krb5_fulladdr faddr = {0};
    krb5_error_code retval;
    struct sockaddr_storage saddr, daddr;
    krb5_address addr;
    krb5_data request;
    krb5_data *response;
    char pktbuf[MAX_DGRAM_SIZE];
    int port_fd = conn->fd;
#ifdef USE_THREADS
    krb5_fulladdr *faddr_dup;
    krb5_fulladdr faddr_daddr;
    krb5_fulladdr *faddr_daddr_dup;
    krb5_address addr2;
#endif

    response = NULL;
    saddr_len = sizeof(saddr);
    daddr_len = sizeof(daddr);
    cc = recv_from_to(port_fd, pktbuf, sizeof(pktbuf), 0,
		      (struct sockaddr *)&saddr, &saddr_len,
		      (struct sockaddr *)&daddr, &daddr_len);
    if (cc == -1) {
	if (errno != EINTR
	    /* This is how Linux indicates that a previous
	       transmission was refused, e.g., if the client timed out
	       before getting the response packet.  */
	    && errno != ECONNREFUSED
	    )
	    com_err(prog, errno, "while receiving from network");
	return;
    }
    if (!cc)
	return;		/* zero-length packet? */

#if 0
    if (daddr_len > 0) {
	char addrbuf[100];
	if (getnameinfo(ss2sa(&daddr), daddr_len, addrbuf, sizeof(addrbuf),
			0, 0, NI_NUMERICHOST))
	    strcpy(addrbuf, "?");
	com_err(prog, 0, "pktinfo says local addr is %s", addrbuf);
    }
#endif

    request.length = cc;
#ifdef USE_THREADS
    request.data = (char *) malloc (request.length);
    if (request.data == NULL) {
        com_err(prog, ENOMEM, "while processing UDP request");
        return ;
    }
    memcpy (request.data, pktbuf, request.length);
#else
    request.data = pktbuf;
#endif
    faddr.address = &addr;
    init_addr(&faddr, ss2sa(&saddr));
    /* this address is in net order */
#ifdef USE_THREADS
    if ((retval = krb5_copy_fulladdr(def_kdc_context, &faddr, &faddr_dup))) {
        com_err(prog, ENOMEM, "while duplicating address");
        return;
    }

    faddr_daddr.address = &addr2;
    init_addr(&faddr_daddr, ss2sa(&daddr));

    if ((retval = krb5_copy_fulladdr(def_kdc_context, &faddr_daddr, &faddr_daddr_dup))) {
        com_err(prog, ENOMEM, "while duplicating address");
        return;
    }

    if ((retval = mt_dispatch(&request, faddr_dup, faddr_daddr_dup, port_fd, UDP))) {
        com_err(prog, retval, "while dispatching (udp)");
        return;
    }
#else
    if ((retval = dispatch(&request, &faddr, &response, 0))) {
	com_err(prog, retval, "while dispatching (udp)");
	return;
    }
#endif

#ifndef USE_THREADS
    cc = send_to_from(port_fd, response->data, (socklen_t) response->length, 0,
		      (struct sockaddr *)&saddr, saddr_len,
		      (struct sockaddr *)&daddr, daddr_len);
    if (cc == -1) {
	char addrbuf[46];
        krb5_free_data(def_kdc_context, response);
	if (inet_ntop(((struct sockaddr *)&saddr)->sa_family,
		      addr.contents, addrbuf, sizeof(addrbuf)) == 0) {
	    strcpy(addrbuf, "?");
	}
	com_err(prog, errno, "while sending reply to %s/%d",
		addrbuf, faddr.port);
	return;
    }
    if (cc != response->length) {
	krb5_free_data(def_kdc_context, response);
	com_err(prog, 0, "short reply write %d vs %d\n",
		response->length, cc);
	return;
    }
    krb5_free_data(def_kdc_context, response);
#endif

    return;
}

static int tcp_data_counter;
static int max_tcp_data_connections = 30;

static void kill_tcp_connection(struct connection *);

static void accept_tcp_connection(struct connection *conn, const char *prog,
				  int selflags)
{
    int s;
    struct sockaddr_storage addr_s;
    struct sockaddr *addr = (struct sockaddr *)&addr_s;
    socklen_t addrlen = sizeof(addr_s);
    struct socksetup sockdata;
    struct connection *newconn;
    char tmpbuf[10];

    s = accept(conn->fd, addr, &addrlen);
    if (s < 0)
	return;
    set_cloexec_fd(s);
    if (s > FD_SETSIZE) {
	close(s);
	return;
    }
    setnbio(s), setnolinger(s);

    sockdata.prog = prog;
    sockdata.retval = 0;

    newconn = add_tcp_data_fd(&sockdata, s);
    if (newconn == 0)
	return;

    if (getnameinfo((struct sockaddr *)&addr_s, addrlen,
		    newconn->u.tcp.addrbuf, sizeof(newconn->u.tcp.addrbuf),
		    tmpbuf, sizeof(tmpbuf),
		    NI_NUMERICHOST | NI_NUMERICSERV))
	strcpy(newconn->u.tcp.addrbuf, "???");
    else {
	char *p, *end;
	p = newconn->u.tcp.addrbuf;
	end = p + sizeof(newconn->u.tcp.addrbuf);
	p += strlen(p);
	if (end - p > 2 + strlen(tmpbuf)) {
	    *p++ = '.';
	    strcpy(p, tmpbuf);
	}
    }
#if 0
    krb5_klog_syslog(LOG_INFO, "accepted TCP connection on socket %d from %s",
		     s, newconn->u.tcp.addrbuf);
#endif

    newconn->u.tcp.addr_s = addr_s;
    newconn->u.tcp.addrlen = addrlen;
    newconn->u.tcp.bufsiz = 1024 * 1024;
    newconn->u.tcp.buffer = malloc(newconn->u.tcp.bufsiz);
    newconn->u.tcp.start_time = time(0);

    if (++tcp_data_counter > max_tcp_data_connections) {
	struct connection *oldest_tcp = NULL;
	struct connection *c;
	int i;

	krb5_klog_syslog(LOG_INFO, "too many connections");

	FOREACH_ELT (connections, i, c) {
	    if (c->type != CONN_TCP)
		continue;
	    if (c == newconn)
		continue;
#if 0
	    krb5_klog_syslog(LOG_INFO, "fd %d started at %ld", c->fd,
			     c->u.tcp.start_time);
#endif
	    if (oldest_tcp == NULL
		|| oldest_tcp->u.tcp.start_time > c->u.tcp.start_time)
		oldest_tcp = c;
	}
	if (oldest_tcp != NULL) {
	    krb5_klog_syslog(LOG_INFO, "dropping tcp fd %d from %s",
			     oldest_tcp->fd, oldest_tcp->u.tcp.addrbuf);
	    kill_tcp_connection(oldest_tcp);
	}
    }
    if (newconn->u.tcp.buffer == 0) {
	com_err(prog, errno, "allocating buffer for new TCP session from %s",
		newconn->u.tcp.addrbuf);
	delete_fd(newconn);
	close(s);
	tcp_data_counter--;
	return;
    }
    newconn->u.tcp.offset = 0;
    newconn->u.tcp.faddr.address = &newconn->u.tcp.kaddr;
    init_addr(&newconn->u.tcp.faddr, ss2sa(&newconn->u.tcp.addr_s));
    SG_SET(&newconn->u.tcp.sgbuf[0], newconn->u.tcp.lenbuf, 4);
    SG_SET(&newconn->u.tcp.sgbuf[1], 0, 0);

    FD_SET(s, &sstate.rfds);
    if (sstate.max <= s)
	sstate.max = s + 1;
}

static void
kill_tcp_connection(struct connection *conn)
{
    if (conn->u.tcp.response)
	krb5_free_data(def_kdc_context, conn->u.tcp.response);
    if (conn->u.tcp.buffer)
	free(conn->u.tcp.buffer);
    FD_CLR(conn->fd, &sstate.rfds);
    FD_CLR(conn->fd, &sstate.wfds);
    if (sstate.max == conn->fd + 1)
	while (sstate.max > 0
	       && ! FD_ISSET(sstate.max-1, &sstate.rfds)
	       && ! FD_ISSET(sstate.max-1, &sstate.wfds)
	       /* && ! FD_ISSET(sstate.max-1, &sstate.xfds) */
	    )
	    sstate.max--;
    close(conn->fd);
    conn->fd = -1;
    delete_fd(conn);
    tcp_data_counter--;
}

static krb5_error_code
make_toolong_error (krb5_data **out)
{
    krb5_error errpkt;
    krb5_error_code retval;
    krb5_data *scratch;

    retval = krb5_us_timeofday(def_kdc_context, &errpkt.stime, &errpkt.susec);
    if (retval)
	return retval;
    errpkt.error = KRB_ERR_FIELD_TOOLONG;
    /*
     * Using first realm's server name since we haven't actually decoded the
     * packet yet.  This should be NULL - but that would violate the RFC.
     */
    errpkt.server = kdc_realmlist[0]->realm_tgsprinc;
    errpkt.client = NULL;
    errpkt.cusec = 0;
    errpkt.ctime = 0;
    errpkt.text.length = 0;
    errpkt.text.data = 0;
    errpkt.e_data.length = 0;
    errpkt.e_data.data = 0;
    scratch = malloc(sizeof(*scratch));
    if (scratch == NULL)
	return ENOMEM;
    retval = krb5_mk_error(def_kdc_context, &errpkt, scratch);
    if (retval) {
	free(scratch);
	return retval;
    }

    *out = scratch;
    return 0;
}

static void
queue_tcp_outgoing_response(struct connection *conn)
{
    store_32_be(conn->u.tcp.response->length, conn->u.tcp.lenbuf);
    SG_SET(&conn->u.tcp.sgbuf[1], conn->u.tcp.response->data,
	   conn->u.tcp.response->length);
    conn->u.tcp.sgp = conn->u.tcp.sgbuf;
    conn->u.tcp.sgnum = 2;
    FD_SET(conn->fd, &sstate.wfds);
}

static void
process_tcp_connection(struct connection *conn, const char *prog, int selflags)
{
    if (selflags & SSF_WRITE) {
	ssize_t nwrote;
	SOCKET_WRITEV_TEMP tmp;

	nwrote = SOCKET_WRITEV(conn->fd, conn->u.tcp.sgp, conn->u.tcp.sgnum,
			       tmp);
	if (nwrote < 0) {
	    goto kill_tcp_connection;
	}
	if (nwrote == 0)
	    /* eof */
	    goto kill_tcp_connection;
	while (nwrote) {
	    sg_buf *sgp = conn->u.tcp.sgp;
	    if (nwrote < SG_LEN(sgp)) {
		SG_ADVANCE(sgp, nwrote);
		nwrote = 0;
	    } else {
		nwrote -= SG_LEN(sgp);
		conn->u.tcp.sgp++;
		conn->u.tcp.sgnum--;
		if (conn->u.tcp.sgnum == 0 && nwrote != 0)
		    abort();
	    }
	}
	if (conn->u.tcp.sgnum == 0) {
	    /* finished sending */
	    /* We should go back to reading, though if we sent a
	       FIELD_TOOLONG error in reply to a length with the high
	       bit set, RFC 4120 says we have to close the TCP
	       stream.  */
	    goto kill_tcp_connection;
	}
    } else if (selflags & SSF_READ) {
	/* Read message length and data into one big buffer, already
	   allocated at connect time.  If we have a complete message,
	   we stop reading, so we should only be here if there is no
	   data in the buffer, or only an incomplete message.  */
	size_t len;
	ssize_t nread;
	if (conn->u.tcp.offset < 4) {
	    /* msglen has not been computed */
	    /* XXX Doing at least two reads here, letting the kernel
	       worry about buffering.  It'll be faster when we add
	       code to manage the buffer here.  */
	    len = 4 - conn->u.tcp.offset;
	    nread = SOCKET_READ(conn->fd,
				conn->u.tcp.buffer + conn->u.tcp.offset, len);
	    if (nread < 0)
		/* error */
		goto kill_tcp_connection;
	    if (nread == 0)
		/* eof */
		goto kill_tcp_connection;
	    conn->u.tcp.offset += nread;
	    if (conn->u.tcp.offset == 4) {
		unsigned char *p = (unsigned char *)conn->u.tcp.buffer;
		conn->u.tcp.msglen = load_32_be(p);
		if (conn->u.tcp.msglen > conn->u.tcp.bufsiz - 4) {
		    krb5_error_code err;
		    /* message too big */
		    krb5_klog_syslog(LOG_ERR, "TCP client %s wants %lu bytes, cap is %lu",
				     conn->u.tcp.addrbuf, (unsigned long) conn->u.tcp.msglen,
				     (unsigned long) conn->u.tcp.bufsiz - 4);
		    /* XXX Should return an error.  */
		    err = make_toolong_error (&conn->u.tcp.response);
		    if (err) {
			krb5_klog_syslog(LOG_ERR,
					 "error constructing KRB_ERR_FIELD_TOOLONG error! %s",
					 error_message(err));
			goto kill_tcp_connection;
		    }
		    goto have_response;
		}
	    }
	} else {
	    /* msglen known */
	    krb5_data request;
	    krb5_error_code err;

	    len = conn->u.tcp.msglen - (conn->u.tcp.offset - 4);
	    nread = SOCKET_READ(conn->fd,
				conn->u.tcp.buffer + conn->u.tcp.offset, len);
	    if (nread < 0)
		/* error */
		goto kill_tcp_connection;
	    if (nread == 0)
		/* eof */
		goto kill_tcp_connection;
	    conn->u.tcp.offset += nread;
	    if (conn->u.tcp.offset < conn->u.tcp.msglen + 4)
		return;
	    /* have a complete message, and exactly one message */
	    request.length = conn->u.tcp.msglen;
	    request.data = conn->u.tcp.buffer + 4;
#ifdef USE_THREADS
            err = mt_dispatch(&request, &conn->u.tcp.faddr, NULL, conn->fd, TCP);
#else
	    err = dispatch(&request, &conn->u.tcp.faddr,
			   &conn->u.tcp.response, 0);
#endif
	    if (err) {
		com_err(prog, err, "while dispatching (tcp)");
		goto kill_tcp_connection;
	    }
#ifdef USE_THREADS
            return;
#endif
	have_response:
	    queue_tcp_outgoing_response(conn);
	    FD_CLR(conn->fd, &sstate.rfds);
	}
    } else
	abort();

    return;

kill_tcp_connection:
    kill_tcp_connection(conn);
}

static void service_conn(struct connection *conn, const char *prog,
			 int selflags)
{
    conn->service(conn, prog, selflags);
}

#ifdef USE_THREADS
static void *service_thread(void *);

struct thread_list {
    struct thread_list *next;
    pthread_t t;
};

static struct thread_list *thread_list_head;
#endif /* USE_THREADS */

krb5_error_code
listen_and_process(const char *prog)
{
    int			nfound;
    /* This struct contains 3 fd_set objects; on some platforms, they
       can be rather large.  Making this static avoids putting all
       that junk on the stack.  */
    static struct select_state sout;
    int			i, sret;
    krb5_error_code	err;

    if (conns == (struct connection **) NULL)
	return KDC5_NONET;

#ifdef USE_THREADS
    /* Spawn the threads */
    {
        sigset_t sigset;

        err = init_multithread(prog);
        if (err != 0)
            return err;

        lock_kdc();
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGINT);
        sigaddset(&sigset, SIGTERM);
        sigaddset(&sigset, SIGHUP);

        /* SIGINT, SIGTERM and SIGHUP must be received only by main thread */
        err = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
        if (err != 0) {
            com_err(prog, err, "while setting thread signal mask");
            unlock_kdc();
            return err;
        }

        thread_list_head = NULL;

        for (i = 0; i < thread_count; i++) {
            int *thread_num;
            struct thread_list *tl;

#ifdef DEBUG
            printf("Num threads = %d\n", thread_count);
#endif

            tl = (struct thread_list *)malloc(sizeof(struct thread_list));
            if (tl == NULL) {
                com_err(prog, ENOMEM, "while allocating thread list");
                unlock_kdc();
                return ENOMEM;
            }
            tl->next = thread_list_head;
            thread_list_head = tl;

            thread_num = (int *)malloc(sizeof(*thread_num));
            if (thread_num == NULL) {
                com_err(prog, ENOMEM, "while allocating thread counter");
                unlock_kdc();
                return ENOMEM;
            }

            *thread_num = i;

            err = pthread_create(&thread_list_head->t, NULL, service_thread, (void *) thread_num);
            if (err != 0) {
                com_err(prog, err, "while creating service thread");
                unlock_kdc();
                return err;
            }
        }

        err = pthread_sigmask(SIG_UNBLOCK, &sigset, NULL);
        if (err != 0) {
            com_err(prog, err, "while restoring thread signal mask");
            return err;
        }
    }
#endif /* USE_THREADS */

#ifdef USE_THREADS
    while (1)
#else
    while (!signal_requests_exit)
#endif
    {
	if (signal_requests_hup) {
	    krb5_klog_reopen(def_kdc_context);
	    signal_requests_hup = 0;
	}
	sstate.end_time.tv_sec = sstate.end_time.tv_usec = 0;
#ifdef USE_THREADS
        {
            struct select_state tmp;
            tmp = sstate;
            if (signal_requests_exit) {
                int active_tcp = 0;
                /* Don't accept new KDC requests */
                for (i = 0; i < n_sockets; i++) {
                    if (conns[i]->type == CONN_UDP || conns[i]->type == CONN_UDP_PKTINFO || conns[i]->type == CONN_TCP_LISTENER) {
                        FD_CLR(conns[i]->fd, &tmp.rfds);
                        FD_CLR(conns[i]->fd, &tmp.wfds);
                        if (tmp.max == conns[i]->fd + 1)
                            while (tmp.max > 0
                                    && ! FD_ISSET(tmp.max-1, &tmp.rfds)
                                    && ! FD_ISSET(tmp.max-1, &tmp.wfds)
                                  )
                                tmp.max--;
                    }
                }
                /* If there are no active TCP requests, exit */
                for (i = 0; i < n_sockets; i++) {
                    if (conns[i]->type == CONN_TCP) {
                        active_tcp = 1;
                        break;
                    }
                }
                if (active_tcp == 0) {
                    unlock_kdc();
                    break;
                }
            }
            unlock_kdc();
            err = krb5int_cm_call_select(&tmp, &sout, &sret);
            lock_kdc();
        }

        if (err == -1 && errno == EINTR)
            /* Some service thread has modified sstate */
            continue;
#else
	err = krb5int_cm_call_select(&sstate, &sout, &sret);
#endif
	if (err) {
	    com_err(prog, err, "while selecting for network input(1)");
	    continue;
	}
	if (sret == -1) {
	    if (errno != EINTR)
		com_err(prog, errno, "while selecting for network input(2)");
	    continue;
	}
	nfound = sret;
	for (i=0; i<n_sockets && nfound > 0; i++) {
	    int sflags = 0;
	    if (conns[i]->fd < 0)
		abort();
	    if (FD_ISSET(conns[i]->fd, &sout.rfds))
		sflags |= SSF_READ, nfound--;
	    if (FD_ISSET(conns[i]->fd, &sout.wfds))
		sflags |= SSF_WRITE, nfound--;
	    if (sflags)
		service_conn(conns[i], prog, sflags);
	}
    }

#ifdef USE_THREADS
    /* Stop the service threads */
    {
        kdc_request_t *req;
        struct thread_list *ptr = thread_list_head, *tmp;

        /* Notify the remaining service threads to exit */
        for (i = 0; i < thread_count; i++) {
            req = (kdc_request_t *)calloc(1, sizeof(*req));
            if (req == NULL)
                continue; /* XXX */

            req->request = NULL;

            add_to_req_queue (req);
        }

        while (ptr != NULL) {
            pthread_join (ptr->t, NULL);
            tmp = ptr;
            ptr = ptr->next;
            free (tmp);
        }
        thread_list_head = NULL;

        /* Cleanup the queue */
        destroy_queue();
    }
#endif /* USE_THREADS */

    return 0;
}

krb5_error_code
closedown_network(const char *prog)
{
    int i;
    struct connection *conn;

    if (conns == (struct connection **) NULL)
	return KDC5_NONET;

    FOREACH_ELT (connections, i, conn) {
	if (conn->fd >= 0)
	    (void) close(conn->fd);
	DEL (connections, i);
	/* There may also be per-connection data in the tcp structure
	   (tcp.buffer, tcp.response) that we're not freeing here.
	   That should only happen if we quit with a connection in
	   progress.  */
	free(conn);
    }
    FREE_SET_DATA(connections);
    FREE_SET_DATA(udp_port_data);
    FREE_SET_DATA(tcp_port_data);

    return 0;
}

#ifdef USE_THREADS
static krb5_error_code init_multithread(const char *prog)
{
    krb5_error_code ret;

    ret = k5_mutex_init (&kdc_lock);
    if (ret != 0) {
        com_err(prog, ret, "while initializing global mutex");
        return ret;
    }

    main_thread_id = pthread_self();

    if (pipe(notifier) != 0) {
        ret = errno;
        com_err(prog, ret, "while initializing notification pipe");
        return ret;
    }

    /* Initialize the request queue */
    return init_queue();
}

/*
 * Tell the main thread to re-look at the set of file descriptors it is
 * 'select'ing
 */
krb5_error_code notify_main_thread(void)
{
    krb5_error_code ret;

    /* Write a single byte to make main thread return from select */
    if (write(notifier[1], "*", 1) != 1) {
        ret = errno;
        com_err("krb5kdc", ret, "while writing to notification pipe");
        return errno;
    }

    return 0;
}

static krb5_error_code
mt_dispatch(krb5_data *pkt, krb5_fulladdr *from, krb5_fulladdr *to, int sockfd, int transport)
{
    kdc_request_t *req;
    krb5_error_code ret;

    req = (kdc_request_t *)malloc(sizeof(kdc_request_t));
    if (req == NULL)
        return ENOMEM;

    req->request = (krb5_data *)malloc(sizeof(krb5_data));
    if (req->request == NULL) {
        free(req);
        return ENOMEM;
    }
    *req->request = *pkt;
    req->response = NULL;
    req->from = from;
    req->transport = transport;
    req->sockfd = sockfd;

    if (transport == UDP) {
        req->to_addr = to;
    }

    if (transport == TCP) {
        FD_CLR(sockfd, &sstate.rfds);
        if (sstate.max == sockfd + 1)
            while (sstate.max > 0
                    && ! FD_ISSET(sstate.max-1, &sstate.rfds)
                    && ! FD_ISSET(sstate.max-1, &sstate.wfds)
                  )
                sstate.max--;
    }

    /* We don't want the main thread to hold the lock and wait on a full queue */
    unlock_kdc();
    ret = add_to_req_queue (req);
    lock_kdc();

    return ret;
}

static void *service_thread(void *_thread_num)
{
    int thread_num;
    const char *prog = "krb5kdc";
    krb5_error_code retval;

    thread_num = *((int *)_thread_num);

    while (1) {
        int cc;
        kdc_request_t *req;
        req = get_req_from_queue ();

        if (req->request == NULL)
            goto cleanup_and_continue;

        lock_kdc();
        retval = dispatch(req->request, req->from, &req->response, thread_num);
        if (retval != 0) {
            if (req->transport == TCP)
                com_err(prog, retval, "while dispatching (tcp)");
            else if (req->transport == UDP)
                com_err(prog, retval, "while dispatching (udp)");
            else
                abort();
        }

        if (req->transport == UDP) {
            struct sockaddr saddr;
            socklen_t saddr_len;
            struct sockaddr daddr;
            socklen_t daddr_len;

            fulladdr2sockaddr(&saddr, req->from);
            if (saddr.sa_family == AF_INET)
                saddr_len = sizeof(struct sockaddr_in);
            else
                saddr_len = sizeof(struct sockaddr_in6);

            fulladdr2sockaddr(&daddr, req->to_addr);
            if (daddr.sa_family == AF_INET)
                daddr_len = sizeof(struct sockaddr_in);
            else
                daddr_len = sizeof(struct sockaddr_in6);

            cc = send_to_from(req->sockfd, req->response->data, (socklen_t) req->response->length, 0,
                    &saddr, saddr_len, &daddr, daddr_len);
            if (cc == -1) {
                char addrbuf[46];
                if (inet_ntop(saddr.sa_family,
                            req->from->address->contents, addrbuf, sizeof(addrbuf)) == 0) {
                    strcpy(addrbuf, "?");
                }
                com_err(prog, errno, "while sending reply to %s/%d",
                        addrbuf, req->from->port);
                goto unlock_and_continue;
            }
            if (cc != req->response->length)
                com_err(prog, 0, "short reply write %d vs %d\n",
                        req->response->length, cc);


        } else if (req->transport == TCP) {
            struct connection *conn;

            conn = get_conn_from_fd (req->sockfd);
            if (conn == NULL)
                goto unlock_and_continue;

            conn->u.tcp.response = req->response;

            queue_tcp_outgoing_response(conn);
            FD_CLR(conn->fd, &sstate.rfds);

            if (conn->fd >= sstate.max)
                sstate.max = conn->fd + 1;

            req->request = req->response = NULL;
            req->from = NULL;
            /* Tell main thread to send the response */
            notify_main_thread();
        } else
            abort ();

unlock_and_continue:
        unlock_kdc();

cleanup_and_continue:
        if (req->request != NULL)
            krb5_free_data(def_kdc_context, req->request);
        if (req->response != NULL)
            krb5_free_data(def_kdc_context, req->response);
        if (req->transport == UDP) {
                if (req->from != NULL)
                        krb5_free_address(def_kdc_context, req->from->address);
                free (req->from);
        }
        free (req);

        if (signal_requests_exit)
            pthread_exit (NULL);
    }
}

#endif

#endif /* INET */
