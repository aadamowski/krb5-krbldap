/*
 * rd_svc_key.c
 *
 * Copyright 1985, 1986, 1987, 1988 by the Massachusetts Institute
 * of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 */

#include "mit-copyright.h"
#include "krb.h"
#include <stdio.h>
#include <string.h>

#include "k5-int.h"

extern char *krb__get_srvtabname();

/*
 * The private keys for servers on a given host are stored in a
 * "srvtab" file (typically "/etc/srvtab").  This routine extracts
 * a given server's key from the file.
 *
 * read_service_key() takes the server's name ("service"), "instance",
 * and "realm" and a key version number "kvno", and looks in the given
 * "file" for the corresponding entry, and if found, returns the entry's
 * key field in "key".
 * 
 * If "instance" contains the string "*", then it will match
 * any instance, and the chosen instance will be copied to that
 * string.  For this reason it is important that the there is enough
 * space beyond the "*" to receive the entry.
 *
 * If "kvno" is 0, it is treated as a wild card and the first
 * matching entry regardless of the "vno" field is returned.
 *
 * This routine returns KSUCCESS on success, otherwise KFAILURE.
 *
 * The format of each "srvtab" entry is as follows:
 *
 * Size			Variable		Field in file
 * ----			--------		-------------
 * string		serv			server name
 * string		inst			server instance
 * string		realm			server realm
 * 1 byte		vno			server key version #
 * 8 bytes		key			server's key
 * ...			...			...
 */

#ifdef __i960__
/* special hack to use a global srvtab variable... */
#define open vxworks_srvtab_open
#define close vxworks_srvtab_close
#define getst vxworks_srvtab_getst
#define read vxworks_srvtab_read

extern char *vxworks_srvtab_base;
char *vxworks_srvtab_ptr;
int vxworks_srvtab_getchar(s)
     char *s;
{
  int tmp1;
  if(vxworks_srvtab_ptr >= (vxworks_srvtab_base + strlen(vxworks_srvtab_base)))
    return 0;

  sscanf(vxworks_srvtab_ptr, "%2x", &tmp1);

  *s = tmp1;
  vxworks_srvtab_ptr+=2;
  return 1;
}

int vxworks_srvtab_getst(fd,s,n)
    int fd;
    register char *s;
    int n;
{
    register count = n;
    while (vxworks_srvtab_getchar(s) && --count)
        if (*s++ == '\0')
            return (n - count);
    *s = '\0';
    return (n - count);
}

int vxworks_srvtab_open(s, n, m)
     char *s;
     int n, m;
{
  vxworks_srvtab_ptr = vxworks_srvtab_base;
  return 1;
}

int vxworks_srvtab_close(fd)
     int fd;
{
  vxworks_srvtab_ptr = 0;
  return 0;
}

int vxworks_srvtab_read(fd, s, n)
     int fd;
     char *s;
     int n;
{
  int count = n;
  /* we want to get exactly n chars. */
  while(vxworks_srvtab_getchar(s) && --count)
    s++;
  return (n-count);
}
#endif

KRB5_DLLIMP int KRB5_CALLCONV
read_service_key(service,instance,realm,kvno,file,key)
    char FAR *service;		/* Service Name */
    char FAR *instance;		/* Instance name or "*" */
    char FAR *realm;		/* Realm */
    int kvno;			/* Key version number */
    char FAR *file;		/* Filename */
    char FAR *key;		/* Pointer to key to be filled in */
{
    int kret;
    
#ifdef KRB4_USE_KEYTAB
    krb5_error_code retval;
    krb5_context context;
    krb5_principal princ;
    krb5_keytab kt_id;
    krb5_keytab_entry kt_entry;
    char sname[ANAME_SZ+1];
    char sinst[INST_SZ+1];
    char srealm[REALM_SZ+1];
    char keytabname[MAX_KEYTAB_NAME_LEN + 1];	/* + 1 for NULL termination */
#endif

    kret = get_service_key(service,instance,realm,&kvno,file,key);

#ifdef KRB4_USE_KEYTAB
    if (! kret)
	return KSUCCESS;

    krb5_init_context(&context);

    if (!strcmp(instance, "*")) {
	retval = krb5_sname_to_principal(context, NULL, NULL, KRB5_NT_SRV_HST,
					 &princ);
	if (!retval) {
	    retval = krb5_524_conv_principal(context, princ,
					     sname, sinst, srealm);
	    krb5_free_principal(context, princ);
	}
	if (!retval)
	    instance = sinst;
    }
    
    retval = krb5_425_conv_principal(context,
				     service,
				     instance,
				     realm,
				     &princ);
    if (!retval)
	retval = krb5_kt_default_name(context, (char *)keytabname,
				      sizeof(keytabname)-1);
    if (!retval) {
	retval = krb5_kt_resolve(context, (char *)keytabname, &kt_id);
	if (!retval) {
	    retval = krb5_kt_get_entry(context, kt_id, princ, kvno,
				       ENCTYPE_DES_CBC_CRC, &kt_entry);
	    krb5_kt_close(context, kt_id);
	}
	krb5_free_principal(context, princ);
    }
    if (!retval) {
	if (kt_entry.key.length == sizeof(C_Block)) {
	    (void) memcpy(key, kt_entry.key.contents, sizeof(C_Block));
	} else {
	    retval = KRB5_BAD_KEYSIZE;
	}
	krb5_kt_free_entry(context, &kt_entry);
    }
    krb5_free_context(context);

    if (! retval)
	return KSUCCESS;
#endif
    
    return kret;
}

/* kvno is passed by reference, so that if it is zero, and we find a match,
   the match gets written back into *kvno so the caller can find it.
 */
KRB5_DLLIMP int KRB5_CALLCONV
get_service_key(service,instance,realm,kvno,file,key)
    char FAR *service;              /* Service Name */
    char FAR *instance;             /* Instance name or "*" */
    char FAR *realm;                /* Realm */
    int  FAR *kvno;                 /* Key version number */
    char FAR *file;                 /* Filename */
    char FAR *key;                  /* Pointer to key to be filled in */
{
    char serv[SNAME_SZ];
    char inst[INST_SZ];
    char rlm[REALM_SZ];
    unsigned char vno;          /* Key version number */
    int wcard;
    char krb_realm[REALM_SZ];

    int stab;

    if (!file)
	file = KEYFILE;

    if ((stab = open(file, 0, 0)) < 0)
        return(KFAILURE);

    wcard = (instance[0] == '*') && (instance[1] == '\0');
    /* get current realm if not passed in */
    if (!realm) {
	int rem;

	rem = krb_get_lrealm(krb_realm,1);
	if (rem != KSUCCESS)
	    return(rem);
	realm = krb_realm;
    }

    while(getst(stab,serv,SNAME_SZ) > 0) { /* Read sname */
        (void) getst(stab,inst,INST_SZ); /* Instance */
        (void) getst(stab,rlm,REALM_SZ); /* Realm */
        /* Vers number */
        if (read(stab,(char *)&vno,1) != 1) {
	    close(stab);
            return(KFAILURE);
	}
        /* Key */
        if (read(stab,key,8) != 8) {
	    close(stab);
            return(KFAILURE);
	}
        /* Is this the right service */
        if (strcmp(serv,service))
            continue;
        /* How about instance */
        if (!wcard && strcmp(inst,instance))
            continue;
        if (wcard)
            (void) strncpy(instance,inst,INST_SZ);
        /* Is this the right realm */
#if defined(ATHENA_COMPAT) || defined(ATHENA_OLD_SRVTAB)
	/* XXX For backward compatibility:  if keyfile says "Athena"
	   and caller wants "ATHENA.MIT.EDU", call it a match */
        if (strcmp(rlm,realm) &&
	    (strcmp(rlm,"Athena") ||
	     strcmp(realm,"ATHENA.MIT.EDU")))
	    continue;
#else /* ! ATHENA_COMPAT */
        if (strcmp(rlm,realm)) 
	    continue;
#endif /* ATHENA_COMPAT */

        /* How about the key version number */
        if (*kvno && *kvno != (int) vno)
            continue;

        (void) close(stab);
	*kvno = vno;
        return(KSUCCESS);
    }

    /* Can't find the requested service */
    (void) close(stab);
    return(KFAILURE);
}
