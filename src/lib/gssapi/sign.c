/*
 * sign.c --- sign message
 * 
 * $Source$
 * $Author$
 * $Header$
 * 
 * Copyright 1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * For copying and distribution information, please see the file
 * <krb5/copyright.h>.
 *
 */

#include <gssapi.h>
#include <krb5/asn1.h>

OM_uint32 gss_sign(minor_status, context, qop_req, 
		   input_message_buffer, output_message_buffer)
	OM_uint32	*minor_status;
	gss_ctx_id_t	context;
	int		qop_req;
	gss_buffer_t	input_message_buffer;
	gss_buffer_t	output_message_buffer;
{
	krb5_data	inbuf, outbuf, *scratch;
	int	safe_flags = 0;
	krb5_safe *message;
	
	*minor_status = 0;

	inbuf.length = input_message_buffer->length;
	inbuf.data = input_message_buffer->value;


	if (context->flags & GSS_C_SEQUENCE_FLAG)
		safe_flags = KRB5_SAFE_DOSEQUENCE|KRB5_SAFE_NOTIME;
	if (*minor_status = krb5_mk_safe(&inbuf,
					 CKSUMTYPE_RSA_MD4_DES,
					 context->session_key,
					 &context->my_address,
					 &context->his_address,
					 context->my_seq_num,
					 safe_flags,
					 0, /* no rcache */
					 &outbuf))
		return(GSS_S_FAILURE);
	if (*minor_status = decode_krb5_safe(&outbuf, &message))
		return(GSS_S_FAILURE);
	message->user_data.length = 1;
	xfree(outbuf.data);
	if (*minor_status = encode_krb5_safe(&message, &scratch)) {
		krb5_free_safe(message);
		return(GSS_S_FAILURE);
	}
	krb5_free_safe(message);
	if (*minor_status = gss_make_token(minor_status,
					   GSS_API_KRB5_TYPE,
					   GSS_API_KRB5_SIGN,
					   scratch->length,
					   scratch->data,
					   output_message_buffer)) {
		krb5_free_data(scratch);
		return(GSS_S_FAILURE);
	}
	krb5_free_data(scratch);
	if (context->flags & GSS_C_SEQUENCE_FLAG)
		context->my_seq_num++;
	return(GSS_S_COMPLETE);
}
	
