/*
 * Copyright 1993 OpenVision Technologies, Inc., All Rights Reserved
 *
 * $Header$
 * 
 * $Log$
 * Revision 1.7.4.1  1996/07/18 03:08:07  marc
 * merged in changes from OV_9510_BP to OV_9510_FINAL1
 *
 * Revision 1.7.2.1  1996/06/20  02:16:25  marc
 * File added to the repository on a branch
 *
 * Revision 1.7  1996/05/12  06:21:57  marc
 * don't use <absolute paths> for "internal header files"
 *
 * Revision 1.6  1993/12/13  21:15:56  shanzer
 * fixed memory leak
 * .,
 *
 * Revision 1.5  1993/12/06  22:20:37  marc
 * fixup free functions to use xdr to free the underlying struct
 *
 * Revision 1.4  1993/11/15  00:29:46  shanzer
 * check to make sure pointers are somewhat vaid before freeing.
 *
 * Revision 1.3  1993/11/09  04:02:24  shanzer
 * added some includefiles
 * changed bzero to memset
 *
 * Revision 1.2  1993/11/04  01:54:24  shanzer
 * added rcs header ..
 *
 */

#if !defined(lint) && !defined(__CODECENTER__)
static char *rcsid = "$Header$";
#endif

#include	"adb.h"
#include	<memory.h>
#include	<malloc.h>

void
osa_free_princ_ent(osa_princ_ent_t val)
{
    XDR xdrs;

    xdrmem_create(&xdrs, NULL, 0, XDR_FREE);

    xdr_osa_princ_ent_rec(&xdrs, val);
    free(val);
}

void
osa_free_policy_ent(osa_policy_ent_t val)
{
    XDR xdrs;

    xdrmem_create(&xdrs, NULL, 0, XDR_FREE);

    xdr_osa_policy_ent_rec(&xdrs, val);
    free(val);
}

