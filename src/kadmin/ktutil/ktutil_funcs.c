/*
 * kadmin/ktutil/ktutil_funcs.c
 *
 *(C) Copyright 1995, 1996 by the Massachusetts Institute of Technology.
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
 * Utility functions for ktutil.
 */

#include "k5-int.h"
#include "ktutil.h"
#ifdef KRB5_KRB4_COMPAT
#include "kerberosIV/krb.h"
#include <stdio.h>
#endif
#include <string.h>

/*
 * Free a kt_list
 */
krb5_error_code ktutil_free_kt_list(context, list)
    krb5_context context;
    krb5_kt_list list;
{
    krb5_kt_list lp, prev;
    krb5_error_code retval = 0;

    for (lp = list; lp;) {
	retval = krb5_kt_free_entry(context, lp->entry);
	free((char *)lp->entry);
	if (retval)
	    break;
	prev = lp;
	lp = lp->next;
	free((char *)prev);
    }
    return retval;
}

/*
 * Delete a numbered entry in a kt_list.  Takes a pointer to a kt_list
 * in case head gets deleted.
 */
krb5_error_code ktutil_delete(context, list, index)
    krb5_context context;
    krb5_kt_list *list;
    int index;
{
    krb5_kt_list lp, prev;
    int i;

    for (lp = *list, i = 1; lp; prev = lp, lp = lp->next, i++) {
	if (i == index) {
	    if (i == 1)
		*list = lp->next;
	    else
		prev->next = lp->next;
	    lp->next = NULL;
	    return ktutil_free_kt_list(context, lp);
	}
    }
    return EINVAL;
}

/*
 * Read in a keytab and append it to list.  If list starts as NULL,
 * allocate a new one if necessary.
 */
krb5_error_code ktutil_read_keytab(context, name, list)
    krb5_context context;
    char *name;
    krb5_kt_list *list;
{
    krb5_kt_list lp = NULL, tail = NULL, back = NULL;
    krb5_keytab kt;
    krb5_keytab_entry *entry;
    krb5_kt_cursor cursor;
    krb5_error_code retval = 0;

    if (*list) {
	/* point lp at the tail of the list */
	for (lp = *list; lp->next; lp = lp->next);
	back = lp;
    }
    retval = krb5_kt_resolve(context, name, &kt);
    if (retval)
	return retval;
    retval = krb5_kt_start_seq_get(context, kt, &cursor);
    if (retval)
	goto close_kt;
    for (;;) {
	entry = (krb5_keytab_entry *)malloc(sizeof (krb5_keytab_entry));
	if (!entry) {
	    retval = ENOMEM;
	    break;
	}
	memset((char *)entry, 0, sizeof (*entry));
	retval = krb5_kt_next_entry(context, kt, entry, &cursor);
	if (retval)
	    break;

	if (!lp) {		/* if list is empty, start one */
	    lp = (krb5_kt_list)malloc(sizeof (*lp));
	    if (!lp) {
		retval = ENOMEM;
		break;
	    }
	} else {
	    lp->next = (krb5_kt_list)malloc(sizeof (*lp));
	    if (!lp->next) {
		retval = ENOMEM;
		break;
	    }
	    lp = lp->next;
	}
	if (!tail)
	    tail = lp;
	lp->next = NULL;
	lp->entry = entry;
    }
    if (entry)
	free((char *)entry);
    if (retval)
	if (retval == KRB5_KT_END)
	    retval = 0;
	else {
	    ktutil_free_kt_list(context, tail);
	    tail = NULL;
	    if (back)
		back->next = NULL;
	}
    if (!*list)
	*list = tail;
    krb5_kt_end_seq_get(context, kt, &cursor);
 close_kt:
    krb5_kt_close(context, kt);
    return retval;
}

/*
 * Takes a kt_list and writes it to the named keytab.
 */
krb5_error_code ktutil_write_keytab(context, list, name)
    krb5_context context;
    krb5_kt_list list;
    char *name;
{
    krb5_kt_list lp;
    krb5_keytab kt;
    char ktname[MAXPATHLEN+sizeof("WRFILE:")+1];
    krb5_error_code retval = 0;

    strcpy(ktname, "WRFILE:");
    strncat(ktname, name, MAXPATHLEN);
    retval = krb5_kt_resolve(context, ktname, &kt);
    if (retval)
	return retval;
    for (lp = list; lp; lp = lp->next) {
	retval = krb5_kt_add_entry(context, kt, lp->entry);
	if (retval)
	    break;
    }
    krb5_kt_close(context, kt);
    return retval;
}

#ifdef KRB5_KRB4_COMPAT
/*
 * getstr() takes a file pointer, a string and a count.  It reads from
 * the file until either it has read "count" characters, or until it
 * reads a null byte.  When finished, what has been read exists in the
 * given string "s".  If "count" characters were actually read, the
 * last is changed to a null, so the returned string is always null-
 * terminated.  getstr() returns the number of characters read,
 * including the null terminator.
 */

int getstr(fp, s, n)
    FILE *fp;
    register char *s;
    int n;
{
    register count = n;
    while (fread(s, 1, 1, fp) > 0 && --count)
        if (*s++ == '\0')
            return (n - count);
    *s = '\0';
    return (n - count);
}

/*
 * Read in a named krb4 srvtab and append to list.  Allocate new list
 * if needed.
 */
krb5_error_code ktutil_read_srvtab(context, name, list)
    krb5_context context;
    char *name;
    krb5_kt_list *list;
{
    krb5_kt_list lp = NULL, tail = NULL, back = NULL;
    krb5_keytab_entry *entry;
    krb5_error_code retval = 0;
    char sname[SNAME_SZ];	/* name of service */
    char sinst[INST_SZ];	/* instance of service */
    char srealm[REALM_SZ];	/* realm of service */
    unsigned char kvno;		/* key version number */
    des_cblock key;
    FILE *fp;

    if (*list) {
	/* point lp at the tail of the list */
	for (lp = *list; lp->next; lp = lp->next);
	back = lp;
    }
    fp = fopen(name, "r");
    if (!fp)
	return EIO;
    for (;;) {
	entry = (krb5_keytab_entry *)malloc(sizeof (krb5_keytab_entry));
	if (!entry) {
	    retval = ENOMEM;
	    break;
	}
	memset((char *)entry, 0, sizeof (*entry));
	memset(sname, 0, sizeof (sname));
	memset(sinst, 0, sizeof (sinst));
	memset(srealm, 0, sizeof (srealm));
	if (!(getstr(fp, sname, SNAME_SZ) > 0 &&
	      getstr(fp, sinst, INST_SZ) > 0 &&
	      getstr(fp, srealm, REALM_SZ) > 0 &&
	      fread(&kvno, 1, 1, fp) > 0 &&
	      fread((char *)key, sizeof (key), 1, fp) > 0))
	    break;
	entry->magic = KV5M_KEYTAB_ENTRY;
	entry->timestamp = 0;	/* XXX */
	entry->vno = kvno;
	retval = krb5_425_conv_principal(context,
					 sname, sinst, srealm,
					 &entry->principal);
	if (retval)
	    break;
	entry->key.magic = KV5M_KEYBLOCK;
	entry->key.enctype = ENCTYPE_DES_CBC_CRC;
	entry->key.length = sizeof (key);
	entry->key.contents = (krb5_octet *)malloc(sizeof (key));
	if (!entry->key.contents) {
	    retval = ENOMEM;
	    break;
	}
	memcpy((char *)entry->key.contents, (char *)key, sizeof (key));
	if (!lp) {		/* if list is empty, start one */
	    lp = (krb5_kt_list)malloc(sizeof (*lp));
	    if (!lp) {
		retval = ENOMEM;
		break;
	    }
	} else {
	    lp->next = (krb5_kt_list)malloc(sizeof (*lp));
	    if (!lp->next) {
		retval = ENOMEM;
		break;
	    }
	    lp = lp->next;
	}
	lp->next = NULL;
	lp->entry = entry;
	if (!tail)
	    tail = lp;
    }
    if (entry) {
	if (entry->magic == KV5M_KEYTAB_ENTRY)
	    krb5_kt_free_entry(context, entry);
	free((char *)entry);
    }
    if (retval) {
	ktutil_free_kt_list(context, tail);
	tail = NULL;
	if (back)
	    back->next = NULL;
    }
    if (!*list)
	*list = tail;
    fclose(fp);
    return retval;
}

/*
 * Writes a kt_list out to a krb4 srvtab file.  Note that it first
 * prunes the kt_list so that it won't contain any keys that are not
 * the most recent, and ignores keys that are not ENCTYPE_DES.
 */
krb5_error_code ktutil_write_srvtab(context, list, name)
    krb5_context context;
    krb5_kt_list list;
    char *name;
{
    krb5_kt_list lp, lp1, prev, pruned = NULL;
    krb5_error_code retval = 0;
    FILE *fp;
    char sname[SNAME_SZ];
    char sinst[INST_SZ];
    char srealm[REALM_SZ];

    /* First do heinous stuff to prune the list. */
    for (lp = list; lp; lp = lp->next) {
	if ((lp->entry->key.enctype != ENCTYPE_DES_CBC_CRC) &&
	    (lp->entry->key.enctype != ENCTYPE_DES_CBC_MD5) &&
	    (lp->entry->key.enctype != ENCTYPE_DES_CBC_MD4) &&
	    (lp->entry->key.enctype != ENCTYPE_DES_CBC_RAW))
	    continue;

	for (lp1 = pruned; lp1; prev = lp1, lp1 = lp1->next) {
	    /* Hunt for the current principal in the pruned list */
	    if (krb5_principal_compare(context,
				       lp->entry->principal,
				       lp1->entry->principal))
		    break;
	}
	if (!lp1) {		/* need to add entry to tail of pruned list */
	    if (!pruned) {
		pruned = (krb5_kt_list) malloc(sizeof (*pruned));
		if (!pruned)
		    return ENOMEM;
		memset((char *) pruned, 0, sizeof(*pruned));
		lp1 = pruned;
	    } else {
		prev->next
		    = (krb5_kt_list) malloc(sizeof (*pruned));
		if (!prev->next) {
		    retval = ENOMEM;
		    goto free_pruned;
		}
		memset((char *) prev->next, 0, sizeof(*pruned));
		lp1 = prev->next;
	    }
	    lp1->entry = lp->entry;
	} else if (lp1->entry->vno < lp->entry->vno)
	    /* Check if lp->entry is newer kvno; if so, update */
	    lp1->entry = lp->entry;
    }
    fp = fopen(name, "w");
    if (!fp) {
	retval = EIO;
	goto free_pruned;
    }
    for (lp = pruned; lp; lp = lp->next) {
	unsigned char  kvno;
	kvno = (unsigned char) lp->entry->vno;
	retval = krb5_524_conv_principal(context,
					 lp->entry->principal,
					 sname, sinst, srealm);
	if (retval)
	    break;
	fwrite(sname, strlen(sname) + 1, 1, fp);
	fwrite(sinst, strlen(sinst) + 1, 1, fp);
	fwrite(srealm, strlen(srealm) + 1, 1, fp);
	fwrite((char *)&kvno, 1, 1, fp);
	fwrite((char *)lp->entry->key.contents,
	       sizeof (des_cblock), 1, fp);
    }
    fclose(fp);
 free_pruned:
    /*
     * Loop over and free the pruned list; don't use free_kt_list
     * because that kills the entries.
     */
    for (lp = pruned; lp;) {
	prev = lp;
	lp = lp->next;
	free((char *)prev);
    }
    return retval;
}
#endif /* KRB5_KRB4_COMPAT */
