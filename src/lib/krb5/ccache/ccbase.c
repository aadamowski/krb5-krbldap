/*
 * lib/krb5/ccache/ccbase.c
 *
 * Copyright 1990,2004 by the Massachusetts Institute of Technology.
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
 * Registration functions for ccache.
 */

#include "k5-int.h"
#include "k5-thread.h"

#include "fcc.h"
#include "cc-int.h"

struct krb5_cc_typelist {
    const krb5_cc_ops *ops;
    struct krb5_cc_typelist *next;
};

struct krb5_cc_typecursor {
    struct krb5_cc_typelist *tptr;
};
/* typedef krb5_cc_typecursor in k5-int.h */

extern const krb5_cc_ops krb5_mcc_ops;
#ifdef USE_KEYRING_CCACHE
extern const krb5_cc_ops krb5_krcc_ops;
#endif

#ifdef _WIN32
extern const krb5_cc_ops krb5_lcc_ops;
static struct krb5_cc_typelist cc_lcc_entry = { &krb5_lcc_ops, NULL };
static struct krb5_cc_typelist cc_mcc_entry = { &krb5_mcc_ops, &cc_lcc_entry };
#else

#ifdef USE_CCAPI_V3
extern const krb5_cc_ops krb5_cc_stdcc_ops;
static struct krb5_cc_typelist cc_stdcc_entry = { &krb5_cc_stdcc_ops, NULL };
static struct krb5_cc_typelist cc_mcc_entry = { &krb5_mcc_ops, &cc_stdcc_entry };
#else

static struct krb5_cc_typelist cc_mcc_entry = { &krb5_mcc_ops, NULL };
#endif /* USE_CCAPI_V3 */

#ifdef USE_KEYRING_CCACHE
static struct krb5_cc_typelist cc_file_entry = { &krb5_cc_file_ops,
						 &cc_mcc_entry };
static struct krb5_cc_typelist cc_krcc_entry = { &krb5_krcc_ops,
						 &cc_file_entry };
#endif /* USE_KEYRING_CCACHE */
#endif

static struct krb5_cc_typelist cc_fcc_entry = { &krb5_cc_file_ops,
						&cc_mcc_entry };
#ifdef USE_KEYRING_CCACHE
#define INITIAL_TYPEHEAD (&cc_krcc_entry)
#else
#define INITIAL_TYPEHEAD (&cc_fcc_entry)
#endif
static struct krb5_cc_typelist *cc_typehead = INITIAL_TYPEHEAD;
static k5_mutex_t cc_typelist_lock = K5_MUTEX_PARTIAL_INITIALIZER;

int
krb5int_cc_initialize(void)
{
    int err;

    err = k5_mutex_finish_init(&krb5int_mcc_mutex);
    if (err)
	return err;
    err = k5_mutex_finish_init(&cc_typelist_lock);
    if (err)
	return err;
    err = k5_mutex_finish_init(&krb5int_cc_file_mutex);
    if (err)
	return err;
#ifdef USE_KEYRING_CCACHE
    err = k5_mutex_finish_init(&krb5int_krcc_mutex);
    if (err)
	return err;
#endif
    return 0;
}

void
krb5int_cc_finalize(void)
{
    struct krb5_cc_typelist *t, *t_next;
    k5_mutex_destroy(&cc_typelist_lock);
    k5_mutex_destroy(&krb5int_cc_file_mutex);
    k5_mutex_destroy(&krb5int_mcc_mutex);
#ifdef USE_KEYRING_CCACHE
    k5_mutex_destroy(&krb5int_krcc_mutex);
#endif
    for (t = cc_typehead; t != INITIAL_TYPEHEAD; t = t_next) {
	t_next = t->next;
	free(t);
    }
}


/*
 * Register a new credentials cache type
 * If override is set, replace any existing ccache with that type tag
 */

krb5_error_code KRB5_CALLCONV
krb5_cc_register(krb5_context context, krb5_cc_ops *ops, krb5_boolean override)
{
    struct krb5_cc_typelist *t;
    krb5_error_code err;

    err = k5_mutex_lock(&cc_typelist_lock);
    if (err)
	return err;
    for (t = cc_typehead;t && strcmp(t->ops->prefix,ops->prefix);t = t->next)
	;
    if (t) {
	if (override) {
	    t->ops = ops;
	    k5_mutex_unlock(&cc_typelist_lock);
	    return 0;
	} else {
	    k5_mutex_unlock(&cc_typelist_lock);
	    return KRB5_CC_TYPE_EXISTS;
	}
    }
    if (!(t = (struct krb5_cc_typelist *) malloc(sizeof(*t)))) {
	k5_mutex_unlock(&cc_typelist_lock);
	return ENOMEM;
    }
    t->next = cc_typehead;
    t->ops = ops;
    cc_typehead = t;
    k5_mutex_unlock(&cc_typelist_lock);
    return 0;
}

/*
 * Resolve a credential cache name into a cred. cache object.
 *
 * The name is currently constrained to be of the form "type:residual";
 *
 * The "type" portion corresponds to one of the predefined credential
 * cache types, while the "residual" portion is specific to the
 * particular cache type.
 */

#include <ctype.h>
krb5_error_code KRB5_CALLCONV
krb5_cc_resolve (krb5_context context, const char *name, krb5_ccache *cache)
{
    struct krb5_cc_typelist *tlist;
    char *pfx, *cp;
    const char *resid;
    unsigned int pfxlen;
    krb5_error_code err;
    
    cp = strchr (name, ':');
    if (!cp) {
	if (krb5_cc_dfl_ops)
	    return (*krb5_cc_dfl_ops->resolve)(context, cache, name);
	else
	    return KRB5_CC_BADNAME;
    }

    pfxlen = cp - name;

    if ( pfxlen == 1 && isalpha((unsigned char) name[0]) ) {
        /* We found a drive letter not a prefix - use FILE */
        pfx = strdup("FILE");
        if (!pfx)
            return ENOMEM;

        resid = name;
    } else {
        resid = name + pfxlen + 1;

        pfx = malloc (pfxlen+1);
        if (!pfx)
            return ENOMEM;

        memcpy (pfx, name, pfxlen);
        pfx[pfxlen] = '\0';
    }

    *cache = (krb5_ccache) 0;

    err = k5_mutex_lock(&cc_typelist_lock);
    if (err) {
	free(pfx);
	return err;
    }
    for (tlist = cc_typehead; tlist; tlist = tlist->next) {
	if (strcmp (tlist->ops->prefix, pfx) == 0) {
	    krb5_error_code (KRB5_CALLCONV *ccresolver)() = tlist->ops->resolve;
	    k5_mutex_unlock(&cc_typelist_lock);
	    free(pfx);
	    return (*ccresolver)(context, cache, resid);
	}
    }
    k5_mutex_unlock(&cc_typelist_lock);
    if (krb5_cc_dfl_ops && !strcmp (pfx, krb5_cc_dfl_ops->prefix)) {
	free (pfx);
	return (*krb5_cc_dfl_ops->resolve)(context, cache, resid);
    }
    free(pfx);
    return KRB5_CC_UNKNOWN_TYPE;
}

/*
 * cc_typecursor
 *
 * Note: to avoid copying the typelist at cursor creation time, among
 * other things, we assume that the only additions ever occur to the
 * typelist.
 */
krb5_error_code
krb5int_cc_typecursor_new(krb5_context context, krb5_cc_typecursor *t)
{
    krb5_error_code err = 0;
    krb5_cc_typecursor n = NULL;

    *t = NULL;
    n = malloc(sizeof(*n));
    if (n == NULL)
	return ENOMEM;

    err = k5_mutex_lock(&cc_typelist_lock);
    if (err)
	goto errout;
    n->tptr = cc_typehead;
    err = k5_mutex_unlock(&cc_typelist_lock);
    if (err)
	goto errout;

    *t = n;
errout:
    if (err)
	free(n);
    return err;
}

krb5_error_code
krb5int_cc_typecursor_next(
    krb5_context context,
    krb5_cc_typecursor t,
    const krb5_cc_ops **ops)
{
    krb5_error_code err = 0;

    *ops = NULL;
    if (t->tptr == NULL)
	return 0;

    err = k5_mutex_lock(&cc_typelist_lock);
    if (err)
	goto errout;
    *ops = t->tptr->ops;
    t->tptr = t->tptr->next;
    err = k5_mutex_unlock(&cc_typelist_lock);
    if (err)
	goto errout;

errout:
    return err;
}

krb5_error_code
krb5int_cc_typecursor_free(krb5_context context, krb5_cc_typecursor *t)
{
    free(*t);
    *t = NULL;
    return 0;
}
