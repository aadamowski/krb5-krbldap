/*
 * $Source$
 * $Author$
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <krb5/copyright.h>.
 *
 * krb5_mk_req_extended()
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_mk_req_ext_c[] =
"$Id$";
#endif	/* !lint & !SABER */

#include <krb5/copyright.h>
#include <krb5/krb5.h>
#include <krb5/asn1.h>

#include <krb5/libos.h>
#include <krb5/libos-proto.h>

#include <krb5/ext-proto.h>

/*
 Formats a KRB_AP_REQ message into outbuf, with more complete options than
 krb_mk_req.

 outbuf, ap_req_options, checksum, and ccache are used in the
 same fashion as for krb5_mk_req.

 creds is used to supply the credentials (ticket and session key) needed
 to form the request.

 if creds->ticket has no data (length == 0), then a ticket is obtained
 from either the cache or the TGS, passing creds to krb5_get_credentials().
 kdc_options specifies the options requested for the ticket to be used.
 If a ticket with appropriate flags is not found in the cache, then these
 options are passed on in a request to an appropriate KDC.

 ap_req_options specifies the KRB_AP_REQ options desired.

 if ap_req_options specifies AP_OPTS_USE_SESSION_KEY, then creds->ticket
 must contain the appropriate ENC-TKT-IN-SKEY ticket.

 checksum specifies the checksum to be used in the authenticator.

 The outbuf buffer storage is allocated, and should be freed by the
 caller when finished.

 returns system errors
*/
static krb5_error_code generate_authenticator PROTOTYPE((krb5_authenticator *,
							 const krb5_creds *,
							 const krb5_checksum *));

krb5_error_code
krb5_mk_req_extended(ap_req_options, checksum, times, kdc_options, ccache,
		     creds, outbuf)
const krb5_flags ap_req_options;
const krb5_checksum *checksum;
const krb5_ticket_times *times;
const krb5_flags kdc_options;
krb5_ccache ccache;
krb5_creds *creds;
krb5_data *outbuf;
{
    krb5_error_code retval;
    krb5_ap_req request;
    krb5_authenticator authent;
    krb5_data *scratch;
    krb5_enctype etype;
    krb5_encrypt_block eblock;
    krb5_data *toutbuf;

    if ((ap_req_options & AP_OPTS_USE_SESSION_KEY) &&
	!creds->ticket.length)
	return(KRB5_NO_TKT_SUPPLIED);

    if (!creds->ticket.length) {
	/* go get creds */
	creds->times = *times;		/* XXX do we need times? */
	if (retval = krb5_get_credentials(kdc_options,
					  ccache,
					  creds))
	    return(retval);
    }
    /* verify a valid etype is available */
    if (!valid_keytype(creds->keyblock.keytype))
	return KRB5_PROG_KEYTYPE_NOSUPP;

    etype = krb5_keytype_array[creds->keyblock.keytype]->system->proto_enctype;

    if (!valid_etype(etype))
	return KRB5_PROG_ETYPE_NOSUPP;

    request.ap_options = ap_req_options;
    /* we need a native ticket */
    if (retval = krb5_decode_ticket(&creds->ticket, &request.ticket))
	return(retval);			/* XXX who cleans up creds? */

#define cleanup_ticket() krb5_free_ticket(request.ticket)
    if (retval = generate_authenticator(&authent, creds, checksum)) {
	cleanup_ticket();
	return retval;
    }
    /* encode it before encrypting */
    retval = encode_krb5_authenticator(&authent, &scratch);
    if (retval) {
	cleanup_ticket();
	return(retval);
    }

#define cleanup_scratch() { (void) bzero(scratch->data, scratch->length); krb5_free_data(scratch); }

    /* put together an eblock for this encryption */

    eblock.crypto_entry = krb5_csarray[etype]->system;
    request.authenticator.length = krb5_encrypt_size(scratch->length,
						     eblock.crypto_entry);
    /* add padding area, and zero it */
    if (!(scratch->data = realloc(scratch->data, request.authenticator.length))) {
	/* may destroy scratch->data */
	xfree(scratch);
	retval = ENOMEM;
	goto clean_ticket;
    }
    bzero(scratch->data + scratch->length,
	  request.authenticator.length - scratch->length);
    if (!(request.authenticator.data = malloc(request.authenticator.length))) {
	retval = ENOMEM;
	goto clean_scratch;
    }

#define cleanup_encpart() {(void) bzero(request.authenticator.data, request.authenticator.length); free(request.authenticator.data); request.authenticator.length = 0; request.authenticator.data = 0;}

    /* do any necessary key pre-processing */
    if (retval = krb5_process_key(&eblock, &creds->keyblock)) {
	goto clean_encpart;
    }

#define cleanup_prockey() {(void) krb5_finish_key(&eblock);}

    /* call the encryption routine */
    if (retval = krb5_encrypt((krb5_pointer) scratch->data,
			      (krb5_pointer) request.authenticator.data,
			      scratch->length, &eblock, 0)) {
	goto clean_prockey;
    }

    /* authenticator now assembled-- do some cleanup */
    cleanup_scratch();

    if (retval = krb5_finish_key(&eblock)) {
	cleanup_encpart();
	return retval;
    }

    if (!(retval = encode_krb5_ap_req(&request, &toutbuf))) {
	*outbuf = *toutbuf;
	xfree(toutbuf);
    }
    cleanup_ticket();
    cleanup_encpart();
    return retval;

 clean_prockey:
    cleanup_prockey();
 clean_encpart:
    cleanup_encpart();
 clean_scratch:
    cleanup_scratch();
 clean_ticket:
    cleanup_ticket();

    return retval;
}

static krb5_error_code
generate_authenticator(authent, creds, cksum)
krb5_authenticator *authent;
const krb5_creds *creds;
const krb5_checksum *cksum;
{
    authent->client = creds->client;
    authent->checksum = (krb5_checksum *)cksum;

    /* cmsec is unsigned, time is signed, hence the cast */
    return(krb5_ms_timeofday(&authent->ctime, 
			     (krb5_int16 *)&authent->cmsec));
}
