/*
 * Copyright 1993 by OpenVision Technologies, Inc.
 * 
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 * 
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "gssapiP_krb5.h"
#include <memory.h>
#include "k5-int.h"

static krb5_error_code
make_ap_req(ctx, auth_context, cred, server, endtime, chan_bindings, 
	    do_mutual, flags, token)
    void *ctx;
    krb5_auth_context * auth_context;
    krb5_gss_cred_id_t cred;
    krb5_principal server;
    krb5_timestamp *endtime;
    gss_channel_bindings_t chan_bindings;
    OM_uint32 do_mutual;
    krb5_flags *flags;
    gss_buffer_t token;
{
    krb5_context context = ctx;
    krb5_flags mk_req_flags = 0;
    krb5_error_code code;
    krb5_data checksum_data;
    krb5_checksum md5;
    krb5_creds in_creds, * out_creds = 0;
    krb5_data ap_req;
    unsigned char *ptr;
    krb5_data credmsg;
    unsigned char ckbuf[24];		/* see the token formats doc */
    unsigned char *t;
    int tlen;
    krb5_int32 con_flags;

    ap_req.data = 0;
    checksum_data.data = 0;

    /* build the checksum buffer */
 
    /* compute the hash of the channel bindings */

    if ((code = kg_checksum_channel_bindings(chan_bindings, &md5, 0)))
        return(code);

    /* get an auth_context structure and fill in checksum type */

    if ((code = krb5_auth_con_init(context, auth_context)))
	return(code);

    krb5_auth_con_setcksumtype(context, *auth_context, CKSUMTYPE_KG_CB);

    /* build the checksum field */

    if(*flags && GSS_C_DELEG_FLAG) {

	/* first get KRB_CRED message, so we know its length */

	/* clear the time check flag that was set in krb5_auth_con_init() */
	krb5_auth_con_getflags(context, *auth_context, &con_flags);
	krb5_auth_con_setflags(context, *auth_context,
			       con_flags & ~KRB5_AUTH_CONTEXT_DO_TIME);

	if ((code = krb5_fwd_tgt_creds(context, *auth_context, 0,
				  cred->princ, server, cred->ccache, 1,
				  &credmsg)))
	    return(code);

	/* turn KRB5_AUTH_CONTEXT_DO_TIME back on */
	krb5_auth_con_setflags(context, *auth_context, con_flags);

	if(credmsg.length+28 > KRB5_INT16_MAX) {
            krb5_xfree(credmsg.data);
	    return(KRB5KRB_ERR_FIELD_TOOLONG);
	}

	/* now allocate a buffer to hold the checksum data and KRB_CRED msg
	   and write it */

	if ((ptr = (unsigned char *) xmalloc(credmsg.length+28)) == NULL) {
	    krb5_xfree(credmsg.data);
	    return(ENOMEM);
	}

	checksum_data.data = (char *) ptr;
	checksum_data.length = credmsg.length+28;

        TWRITE_INT(ptr, md5.length, 0);
        TWRITE_STR(ptr, (unsigned char *) md5.contents, md5.length);
        TWRITE_INT(ptr, do_mutual?GSS_C_MUTUAL_FLAG | GSS_C_DELEG_FLAG
				 :GSS_C_DELEG_FLAG, 0);

        /* done with this, free it */
        xfree(md5.contents);

	TWRITE_INT16(ptr, KRB5_GSS_FOR_CREDS_OPTION, 0);
	TWRITE_INT16(ptr, credmsg.length, 0);
	TWRITE_STR(ptr, (unsigned char *) credmsg.data, credmsg.length);

	/* free credmsg data */

	krb5_xfree(credmsg.data);

    } else {

	ptr = ckbuf;

	TWRITE_INT(ptr, md5.length, 0);
	TWRITE_STR(ptr, (unsigned char *) md5.contents, md5.length);
	TWRITE_INT(ptr, do_mutual?GSS_C_MUTUAL_FLAG:0, 0);

	/* done with this, free it */
	xfree(md5.contents);

	checksum_data.data = (char *) ckbuf;
	checksum_data.length = sizeof(ckbuf);
    }

    /* fill in the necessary fields in creds */
    memset((char *) &in_creds, 0, sizeof(krb5_creds));
    if ((code = krb5_copy_principal(context, cred->princ, &in_creds.client)))
	goto cleanup;
    if ((code = krb5_copy_principal(context, server, &in_creds.server)))
	goto cleanup;
    
    in_creds.times.endtime = *endtime;
    
    /*
     * Get the credential..., I don't know in 0 is a good value for the
     * kdcoptions
     */
    if ((code = krb5_get_credentials(context, 0, cred->ccache, 
				     &in_creds, &out_creds)))
	goto cleanup;

    /* call mk_req.  subkey and ap_req need to be used or destroyed */

    mk_req_flags = AP_OPTS_USE_SUBKEY;

    if (do_mutual)
	mk_req_flags |= AP_OPTS_MUTUAL_REQUIRED;

    if ((code = krb5_mk_req_extended(context, auth_context, mk_req_flags,
				     &checksum_data, out_creds, &ap_req)))
	goto cleanup;

   /* store the interesting stuff from creds and authent */
   *endtime = out_creds->times.endtime;
   *flags = out_creds->ticket_flags;

   /* build up the token */

   /* allocate space for the token */
   tlen = g_token_size((gss_OID) gss_mech_krb5, ap_req.length);

   if ((t = (unsigned char *) xmalloc(tlen)) == NULL) {
      code = ENOMEM;
      goto cleanup;
   }

   /* fill in the buffer */

   ptr = t;

   g_make_token_header((gss_OID) gss_mech_krb5, ap_req.length,
		       &ptr, KG_TOK_CTX_AP_REQ);

   TWRITE_STR(ptr, (unsigned char *) ap_req.data, ap_req.length);

   /* pass it back */
   token->length = tlen;
   token->value = (void *) t;

   code = 0;
    
cleanup:
    if (checksum_data.data && checksum_data.data != (char *) ckbuf)
	free(checksum_data.data);
    krb5_free_cred_contents(context, &in_creds);
    if (out_creds)
	krb5_free_creds(context, out_creds);
    if (ap_req.data)
	xfree(ap_req.data);
    if (code)
	krb5_auth_con_free(context, *auth_context);

    return (code);
}

OM_uint32
krb5_gss_init_sec_context(ct, minor_status, claimant_cred_handle,
			context_handle, target_name, mech_type,
			req_flags, time_req, input_chan_bindings,
			input_token, actual_mech_type, output_token,
			ret_flags, time_rec)
    void *ct;
    OM_uint32 *minor_status;
    gss_cred_id_t claimant_cred_handle;
    gss_ctx_id_t *context_handle;
    gss_name_t target_name;
    gss_OID mech_type;
    OM_uint32 req_flags;
    OM_uint32 time_req;
    gss_channel_bindings_t input_chan_bindings;
    gss_buffer_t input_token;
    gss_OID *actual_mech_type;
    gss_buffer_t output_token;
    OM_uint32 *ret_flags;
    OM_uint32 *time_rec;
{
    krb5_context context = ct;
    krb5_gss_cred_id_t 	  cred;
    krb5_error_code 	  code; 
    krb5_gss_ctx_id_rec *ctx;
    krb5_enctype enctype;
    krb5_timestamp now;
    gss_buffer_desc token;
    int i;

   /* set up return values so they can be "freed" successfully */

   output_token->length = 0;
   output_token->value = NULL;
   if (actual_mech_type)
      *actual_mech_type = (gss_OID) gss_mech_krb5;

   /* verify the mech_type */

   if ((mech_type != GSS_C_NULL_OID) &&
       (! g_OID_equal(mech_type, gss_mech_krb5))) {
      *minor_status = 0;
      return(GSS_S_BAD_MECH);
   }

   /* verify the credential, or use the default */
   /*SUPPRESS 29*/
   if (claimant_cred_handle == GSS_C_NO_CREDENTIAL) {
      OM_uint32 major;

      if ((major = kg_get_defcred(minor_status, &claimant_cred_handle)) &&
	  GSS_ERROR(major)) {
	 return(major);
      }
   } else {
      if (! kg_validate_cred_id(claimant_cred_handle)) {
	 *minor_status = (OM_uint32) G_VALIDATE_FAILED;
	 return(GSS_S_CALL_BAD_STRUCTURE|GSS_S_DEFECTIVE_CREDENTIAL);
      }
   }

   cred = (krb5_gss_cred_id_t) claimant_cred_handle;

   /* verify that the target_name is valid and usable */

   if (! kg_validate_name(target_name)) {
      *minor_status = (OM_uint32) G_VALIDATE_FAILED;
      return(GSS_S_CALL_BAD_STRUCTURE|GSS_S_BAD_NAME);
   }

   /* is this a new connection or not? */

   /*SUPPRESS 29*/
   if (*context_handle == GSS_C_NO_CONTEXT) {
      /* make sure the cred is usable for init */

      if ((cred->usage != GSS_C_INITIATE) &&
	  (cred->usage != GSS_C_BOTH)) {
	 *minor_status = 0;
	 return(GSS_S_NO_CRED);
      }

      /* complain if the input token is nonnull */

      if (input_token != GSS_C_NO_BUFFER) {
	 *minor_status = 0;
	 return(GSS_S_DEFECTIVE_TOKEN);
      }

      /* create the ctx */

      if ((ctx = (krb5_gss_ctx_id_rec *) xmalloc(sizeof(krb5_gss_ctx_id_rec)))
	  == NULL) {
	 *minor_status = ENOMEM;
	 return(GSS_S_FAILURE);
      }

      /* fill in the ctx */
      memset(ctx, 0, sizeof(krb5_gss_ctx_id_rec));
      ctx->auth_context = NULL;
      ctx->initiate = 1;
      ctx->mutual = req_flags & GSS_C_MUTUAL_FLAG;
      ctx->flags = req_flags & GSS_C_DELEG_FLAG;
      ctx->seed_init = 0;
      ctx->big_endian = 0;  /* all initiators do little-endian, as per spec */

      if (time_req == 0 || time_req == GSS_C_INDEFINITE) {
	 ctx->endtime = 0;
      } else {
	 if ((code = krb5_timeofday(context, &now))) {
	    free(ctx);
	    *minor_status = code;
	    return(GSS_S_FAILURE);
	 }
	 ctx->endtime = now + time_req;
      }

      if ((code = krb5_copy_principal(context, cred->princ, &ctx->here))) {
	 xfree(ctx);
	 *minor_status = code;
	 return(GSS_S_FAILURE);
      }
      
      if ((code = krb5_copy_principal(context, (krb5_principal) target_name,
				      &ctx->there))) {
	 krb5_free_principal(context, ctx->here);
	 xfree(ctx);
	 *minor_status = code;
	 return(GSS_S_FAILURE);
      }

      if ((code = make_ap_req(context, &(ctx->auth_context), cred, 
			      ctx->there, &ctx->endtime, input_chan_bindings, 
			      ctx->mutual, &ctx->flags, &token))) {
	 krb5_free_principal(context, ctx->here);
	 krb5_free_principal(context, ctx->there);
	 xfree(ctx);
	 *minor_status = code;
	 if (code == KRB5KRB_AP_ERR_TKT_EXPIRED)
		 return GSS_S_CREDENTIALS_EXPIRED;
	 return(GSS_S_FAILURE);
      }

      krb5_auth_con_getlocalseqnumber(context, ctx->auth_context, &ctx->seq_send);
      krb5_auth_con_getlocalsubkey(context, ctx->auth_context, &ctx->subkey);

      /* fill in the encryption descriptors */

      switch(ctx->subkey->enctype) {
      case ENCTYPE_DES_CBC_MD5:
      case ENCTYPE_DES_CBC_CRC:
	  enctype = ENCTYPE_DES_CBC_RAW;
	  break;
      case ENCTYPE_DES3_CBC_MD5:
	  enctype = ENCTYPE_DES3_CBC_RAW;
	  break;
      default:
	  return GSS_S_FAILURE;
      }

      /* the encryption key is the session key XOR 0xf0f0f0f0f0f0f0f0 */

      krb5_use_enctype(context, &ctx->enc.eblock, enctype);
      ctx->enc.processed = 0;
      if ((code = krb5_copy_keyblock(context, ctx->subkey, &ctx->enc.key)))
	 return(code); 
      for (i=0; i<ctx->enc.key->length; i++)
	 /*SUPPRESS 113*/
	 ctx->enc.key->contents[i] ^= 0xf0;
#if 0
      if ((code = krb5_process_key(context, &ctx->enc.eblock, ctx->enc.key)))
	  return(code);
      ctx->enc.processed = 1;
#endif

      krb5_use_enctype(context, &ctx->seq.eblock, enctype);
      ctx->seq.processed = 0;
      if ((code = krb5_copy_keyblock(context, ctx->subkey, &ctx->seq.key)))
	  return(code);
#if 0
      if ((code = krb5_process_key(context, &ctx->seq.eblock, ctx->seq.key)))
	  return(code);
      ctx->seq.processed = 1;
#endif

      /* at this point, the context is constructed and valid,
	 hence, releaseable */

      /* intern the context handle */

      if (! kg_save_ctx_id((gss_ctx_id_t) ctx)) {
	 xfree(token.value);
	 krb5_free_keyblock(context, ctx->subkey);
	 krb5_free_principal(context, ctx->here);
	 krb5_free_principal(context, ctx->there);
	 xfree(ctx);

	 *minor_status = (OM_uint32) G_VALIDATE_FAILED;
	 return(GSS_S_FAILURE);
      }

      /* compute time_rec */

      if (time_rec) {
	 if ((code = krb5_timeofday(context, &now))) {
	    xfree(token.value);
	    (void)krb5_gss_delete_sec_context(context, minor_status, 
					      (gss_ctx_id_t) ctx, NULL);
	    *minor_status = code;
	    return(GSS_S_FAILURE);
	 }
	 *time_rec = ctx->endtime - now;
      }

      /* set the other returns */

      *context_handle = (gss_ctx_id_t) ctx;

      *output_token = token;

      if (ret_flags)
	 *ret_flags = ((req_flags & (GSS_C_MUTUAL_FLAG | GSS_C_DELEG_FLAG)) | 
		       GSS_C_CONF_FLAG | GSS_C_INTEG_FLAG);

      /* return successfully */

      *minor_status = 0;
      if (ctx->mutual) {
	 ctx->established = 0;
	 return(GSS_S_CONTINUE_NEEDED);
      } else {
	 ctx->seq_recv = ctx->seq_send;
	 ctx->established = 1;
	 return(GSS_S_COMPLETE);
      }
   } else {
      unsigned char *ptr;
      char *sptr;
      krb5_data ap_rep;
      krb5_ap_rep_enc_part *ap_rep_data;

      /* validate the context handle */
      /*SUPPRESS 29*/
      if (! kg_validate_ctx_id(*context_handle)) {
	 *minor_status = (OM_uint32) G_VALIDATE_FAILED;
	 return(GSS_S_NO_CONTEXT);
      }

      ctx = (gss_ctx_id_t) *context_handle;

      /* make sure the context is non-established, and that certain
	 arguments are unchanged */

      if ((ctx->established) ||
	  (((gss_cred_id_t) cred) != claimant_cred_handle) ||
	  ((req_flags & GSS_C_MUTUAL_FLAG) == 0)) {
	 (void)krb5_gss_delete_sec_context(context, minor_status, 
					   context_handle, NULL);
	 *minor_status = KG_CONTEXT_ESTABLISHED;
	 return(GSS_S_FAILURE);
      }

      if (! krb5_principal_compare(context, ctx->there, 
				   (krb5_principal) target_name)) {
	 (void)krb5_gss_delete_sec_context(context, minor_status, 
					   context_handle, NULL);
	 *minor_status = 0;
	 return(GSS_S_BAD_NAME);
      }

      /* verify the token and leave the AP_REP message in ap_rep */

      if (input_token == GSS_C_NO_BUFFER) {
	 (void)krb5_gss_delete_sec_context(context, minor_status, 
					   context_handle, NULL);
	 *minor_status = 0;
	 return(GSS_S_DEFECTIVE_TOKEN);
      }

      ptr = (unsigned char *) input_token->value;

      if (! g_verify_token_header((gss_OID) gss_mech_krb5, &(ap_rep.length),
				  &ptr, KG_TOK_CTX_AP_REP,
				  input_token->length)) {
	 *minor_status = 0;
	 return(GSS_S_DEFECTIVE_TOKEN);
      }

      sptr = (char *) ptr;                      /* PC compiler bug */
      TREAD_STR(sptr, ap_rep.data, ap_rep.length);

      	/* decode the ap_rep */
      	if ((code = krb5_rd_rep(context,ctx->auth_context,&ap_rep,
				&ap_rep_data))) {
	    /*
	     * XXX A hack for backwards compatiblity.
	     * To be removed in 1999 -- proven 
	     */
	    krb5_auth_con_setuseruserkey(context,ctx->auth_context,ctx->subkey);
	    if ((code = krb5_rd_rep(context, ctx->auth_context, &ap_rep,
				    &ap_rep_data))) {
	 	(void)krb5_gss_delete_sec_context(context, minor_status, 
					          context_handle, NULL);
		*minor_status = code;
	 	return(GSS_S_FAILURE);
	    }
      	}

      /* store away the sequence number */
      ctx->seq_recv = ap_rep_data->seq_number;

      /* free the ap_rep_data */
      krb5_free_ap_rep_enc_part(context, ap_rep_data);

      /* set established */
      ctx->established = 1;

      /* set returns */

      if (time_rec) {
	 if ((code = krb5_timeofday(context, &now))) {
	    (void)krb5_gss_delete_sec_context(context, minor_status, 
					      (gss_ctx_id_t) ctx, NULL);
	    *minor_status = code;
	    return(GSS_S_FAILURE);

	 }
      }

      if (ret_flags)
	 *ret_flags = GSS_C_CONF_FLAG | GSS_C_INTEG_FLAG | GSS_C_MUTUAL_FLAG;

      /* success */

      *minor_status = 0;
      return(GSS_S_COMPLETE);
   }
}
