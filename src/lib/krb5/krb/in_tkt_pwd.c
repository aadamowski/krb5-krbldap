/*
 * $Source$
 * $Author$
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <krb5/mit-copyright.h>.
 *
 * krb5_get_in_tkt_with_password()
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_in_tkt_pwd_c[] =
"$Id$";
#endif	/* !lint & !SABER */

#include <krb5/copyright.h>
#include <krb5/krb5.h>
#include <krb5/krb5_err.h>
#include <errno.h>
#include <krb5/ext-proto.h>

extern krb5_cryptosystem_entry *string_to_keyarray[]; /* XXX */

struct pwd_keyproc_arg {
    krb5_principal who;
    krb5_data password;
};

/* 
 * key-producing procedure for use by krb5_get_in_tkt_with_password.
 */

static krb5_error_code
pwd_keyproc(type, key, keyseed)
krb5_keytype type;
krb5_keyblock **key;
krb5_pointer keyseed;
{
    krb5_error_code retval;
    struct pwd_keyproc_arg *arg;

    if (!valid_keytype(type))
	return KRB5KDC_ERR_ETYPE_NOSUPP; /* XXX */
    *key = (krb5_keyblock *)malloc(sizeof(**key));
    if (!*key)
	return ENOMEM;
    
    arg = (struct pwd_keyproc_arg *)keyseed;
    if (retval = (*string_to_keyarray[type]->string_to_key)(type,
							    *key,
							    &arg->password,
							    arg->who)) {
	free((char *) *key);
	return(retval);
    }
    return 0;
}

/*
 Attempts to get an initial ticket for creds->client to use server
 creds->server, (realm is taken from creds->client), with options
 options, requesting encryption type etype, and using
 creds->times.starttime,  creds->times.endtime,  creds->times.renew_till
 as from, till, and rtime.  creds->times.renew_till is ignored unless
 the RENEWABLE option is requested.

 If addrs is non-NULL, it is used for the addresses requested.  If it is
 null, the system standard addresses are used.

 If password is non-NULL, it is converted using the cryptosystem entry
 point for a string conversion routine, seeded with the client's name.
 If password is passed as NULL, the password is read from the terminal,
 and then converted into a key.

 A succesful call will place the ticket in the credentials cache ccache.

 returns system errors, encryption errors
 */
krb5_error_code
krb5_get_in_tkt_with_password(options, addrs, etype, keytype, password,
			      ccache, creds)
krb5_flags options;
krb5_address **addrs;
krb5_enctype etype;
krb5_keytype keytype;
char *password;
krb5_ccache ccache;
krb5_creds *creds;
{
    krb5_error_code retval;
    struct pwd_keyproc_arg keyseed;


    keyseed.password.data = password;
    keyseed.password.length = strlen(password);
    keyseed.who = creds->client;

    retval = krb5_get_in_tkt(options, addrs, etype, keytype, pwd_keyproc,
			     (krb5_pointer) &keyseed,
			     krb5_kdc_rep_decrypt_proc, 0,
			     creds);
    /* XXX need to play with creds & store them ? */
    return retval;
}

