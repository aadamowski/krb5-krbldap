/*
 * lib/krb5/krb/srv_rcache.c
 *
 * Copyright 1991 by the Massachusetts Institute of Technology.
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
 *
 * Allocate & prepare a default replay cache for a server.
 */

#include "k5-int.h"
#include <ctype.h>
#include <stdio.h>

krb5_error_code
krb5_get_server_rcache(context, piece, rcptr)
    krb5_context context;
    const krb5_data *piece;
    krb5_rcache *rcptr;
{
    krb5_rcache rcache = 0;
    char *cachename = 0;
    char tmp[4];
    extern krb5_deltat krb5_clockskew;
    krb5_error_code retval;
    int len, p, i;
    
    rcache = (krb5_rcache) malloc(sizeof(*rcache));
    if (!rcache)
	return ENOMEM;
    
    retval = krb5_rc_resolve_type(context, &rcache, "dfl");
    if (retval) goto cleanup;

    len = piece->length + 3 + 1;
    for (i = 0; i < piece->length; i++) {
	if (piece->data[i] == '\\')
	    len++;
	else if (!isgraph(piece->data[i]))
	    len += 3;
    }
    cachename = malloc(len);
    if (!cachename) {
	retval = ENOMEM;
	goto cleanup;
    }
    strcpy(cachename, "rc_");
    p = 3;
    for (i = 0; i < piece->length; i++) {
	if (piece->data[i] == '\\') {
	    cachename[p++] = '\\';
	    cachename[p++] = '\\';
	    continue;
	}
	if (!isgraph(piece->data[i])) {
	    sprintf(tmp, "%03o", piece->data[i]);
	    cachename[p++] = '\\';
	    cachename[p++] = tmp[0];
	    cachename[p++] = tmp[1];
	    cachename[p++] = tmp[2];
	    continue;
	}
	cachename[p++] = piece->data[i];
    }
    cachename[p++] = '\0';

    if ((retval = krb5_rc_resolve(context, rcache, cachename)))
	goto cleanup;
    
    /*
     * First try to recover the replay cache; if that doesn't work,
     * initialize it.
     */
    if (krb5_rc_recover(context, rcache)) {
	if ((retval = krb5_rc_initialize(context, rcache, krb5_clockskew))) {
	    krb5_rc_close(context, rcache);
	    rcache = 0;
	    goto cleanup;
	}
    }

    *rcptr = rcache;
    return 0;

cleanup:
    if (rcache)
	krb5_xfree(rcache);
    if (cachename)
	krb5_xfree(cachename);
    return retval;
}
