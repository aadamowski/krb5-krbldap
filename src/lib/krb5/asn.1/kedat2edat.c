/*
 * $Source$
 * $Author$
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <krb5/copyright.h>.
 *
 * Glue between Kerberos version and ISODE 6.0 version of structures.
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_kedat2edat_c[] =
"$Id$";
#endif	/* lint || saber */

#include <krb5/copyright.h>
#include <krb5/krb5.h>

/*#include <time.h> */
#include <isode/psap.h>
#include <krb5/asn1.h>
#include "asn1glue.h"

#include <krb5/ext-proto.h>

/* ISODE defines max(a,b) */

struct type_KRB5_EncryptedData *
krb5_enc_data2KRB5_EncryptedData(val, error)
const register krb5_enc_data *val;
register int *error;
{
    register struct type_KRB5_EncryptedData *retval;
 
    retval = (struct type_KRB5_EncryptedData *)xmalloc(sizeof(*retval));
    if (!retval) {
	*error = ENOMEM;
	return(0);
    }
    xbzero(retval, sizeof(*retval));

    retval->etype = val->etype;
    retval->cipher = krb5_data2qbuf(&(val->ciphertext));
    if (!retval->cipher) {
	xfree(retval);
	*error = ENOMEM;
	return(0);
    }
    if (val->kvno) {
	retval->optionals = opt_KRB5_EncryptedData_kvno;
	retval->kvno = val->kvno;
    }
    return(retval);
}
