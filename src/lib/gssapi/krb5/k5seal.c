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
#include "rsa-md5.h"

static krb5_error_code
make_seal_token(context, enc_ed, seq_ed, seqnum, direction, text, token,
		encrypt, toktype, bigend)
     krb5_context context;
     krb5_gss_enc_desc *enc_ed;
     krb5_gss_enc_desc *seq_ed;
     krb5_int32 *seqnum;
     int direction;
     gss_buffer_t text;
     gss_buffer_t token;
     int encrypt;
     int toktype;
     int bigend;
{
   krb5_error_code code;
   krb5_cksumtype cksum_type;
   MD5_CTX md5;
   krb5_enctype enctype;
   krb5_checksum cksum;
   int cksum_size;
   int tmsglen, tlen;
   unsigned char *t, *ptr;

   /* create the token buffer */

   if ((toktype == KG_TOK_SEAL_MSG) || (toktype == KG_TOK_WRAP_MSG)) {
      if (bigend && !encrypt)
	 tmsglen = text->length;
      else
	 tmsglen = (kg_confounder_size(enc_ed)+text->length+8)&(~7);
   } else {
      tmsglen = 0;
   }

   enctype = krb5_eblock_enctype(context, &enc_ed->eblock);
   switch(enctype) {
   case ENCTYPE_DES_CBC_RAW:
       cksum_type = 0;
       cksum_size = 8;
       break;
   case ENCTYPE_DES3_CBC_RAW:
       cksum_type = 3;
       cksum_size = 16;
       break;
   default:
       return KRB5_PROG_ETYPE_NOSUPP;
   }

   tlen = g_token_size((gss_OID) gss_mech_krb5, 14+cksum_size+tmsglen);

   if ((t = (unsigned char *) xmalloc(tlen)) == NULL)
      return(ENOMEM);

   /*** fill in the token */

   ptr = t;

   g_make_token_header((gss_OID) gss_mech_krb5,
		       14+cksum_size+tmsglen, &ptr, toktype);

   /* 0..1 SIGN_ALG */

   ptr[0] = cksum_type;
   ptr[1] = 0;
   
   /* 2..3 SEAL_ALG or Filler */

   if (!encrypt ||
       ((toktype != KG_TOK_SEAL_MSG) && (toktype != KG_TOK_WRAP_MSG)))
   {
       /* No seal */
       ptr[2] = 0xff;
       ptr[3] = 0xff;
   } else {
       switch(enctype) {
       case ENCTYPE_DES_CBC_RAW:
	   ptr[2] = 0; ptr[3] = 0;
	   break;
       case ENCTYPE_DES3_CBC_RAW:
	   ptr[2] = 1; ptr[3] = 0;
	   break;
       default:
	   return KRB5_PROG_ETYPE_NOSUPP;
       }
   }

   /* 4..5 Filler */

   ptr[4] = 0xff;
   ptr[5] = 0xff;

   /* pad the plaintext, encrypt if needed, and stick it in the token */

   if ((toktype == KG_TOK_SEAL_MSG) || (toktype == KG_TOK_WRAP_MSG)) {
      unsigned char *plain;
      unsigned char pad;

      if ((plain = (unsigned char *) xmalloc(tmsglen)) == NULL) {
	 xfree(t);
	 return(ENOMEM);
      }

      if (code = kg_make_confounder(enc_ed, plain)) {
	 xfree(plain);
	 xfree(t);
	 return(code);
      }

      memcpy(plain+8, text->value, text->length);

      pad = 8-(text->length%8);

      memset(plain+8+text->length, pad, pad);

      if (encrypt) {
	 if (code = kg_encrypt(enc_ed, NULL, (krb5_pointer) plain,
			       (krb5_pointer) (ptr+14+cksum_size), tmsglen)) {
	    xfree(plain);
	    xfree(t);
	    return(code);
	 }
      } else {
	 if (bigend)
	    memcpy(ptr+14+cksum_size, text->value, text->length);
	 else
	    memcpy(ptr+14+cksum_size, plain, tmsglen);
      }

      xfree(plain);
   }

   switch(cksum_type) {
   case 0:
   case 3:
       MD5Init(&md5);
       MD5Update(&md5, (unsigned char *) ptr-2, 8);
       if (!bigend &&
	   ((toktype == KG_TOK_SEAL_MSG) || (toktype == KG_TOK_WRAP_MSG)))
	   MD5Update(&md5, ptr+14+cksum_size, tmsglen);
       else
	   MD5Update(&md5, text->value, text->length);
       MD5Final(&md5);

#if 0
       code = krb5_calculate_checksum(context, CKSUMTYPE_DESCBC,
				      md5.digest, 16,
				      seq_ed->key->contents, 
				      seq_ed->key->length, &cksum);
#endif
       code = kg_encrypt(seq_ed, NULL, md5.digest, md5.digest, 16);

       if (cksum_type == 0)
	   cksum.length = 8;
       else
	   cksum.length = 16;
       cksum.contents = (krb5_pointer)md5.digest + 16 - cksum.length;

       if (code) {
	   xfree(t);
	   return code;
       }
       
       break;
   }

   if (cksum.length != cksum_size) {
       xfree(t);
       return GSS_S_FAILURE;
   }
       
   memcpy(ptr+14, cksum.contents, cksum.length);

   /* create the seq_num */

   if (code = kg_make_seq_num(seq_ed, direction?0:0xff, *seqnum,
			      ptr+14, ptr+6)) {
      xfree(t);
      return(code);
   }

   /* that's it.  return the token */

   (*seqnum)++;

   token->length = tlen;
   token->value = (void *) t;

   return(0);
}

/* if signonly is true, ignore conf_req, conf_state, 
   and do not encode the ENC_TYPE, MSG_LENGTH, or MSG_TEXT fields */

OM_uint32
kg_seal(context, minor_status, context_handle, conf_req_flag, qop_req, 
	input_message_buffer, conf_state, output_message_buffer, toktype)
     krb5_context context;
     OM_uint32 *minor_status;
     gss_ctx_id_t context_handle;
     int conf_req_flag;
     int qop_req;
     gss_buffer_t input_message_buffer;
     int *conf_state;
     gss_buffer_t output_message_buffer;
     int toktype;
{
   krb5_gss_ctx_id_rec *ctx;
   krb5_error_code code;
   krb5_timestamp now;

   output_message_buffer->length = 0;
   output_message_buffer->value = NULL;

   /* only default qop is allowed */
   if (qop_req != GSS_C_QOP_DEFAULT) {
      *minor_status = (OM_uint32) G_UNKNOWN_QOP;
      return(GSS_S_FAILURE);
   }

   /* validate the context handle */
   if (! kg_validate_ctx_id(context_handle)) {
      *minor_status = (OM_uint32) G_VALIDATE_FAILED;
      return(GSS_S_NO_CONTEXT);
   }

   ctx = (krb5_gss_ctx_id_rec *) context_handle;

   if (! ctx->established) {
      *minor_status = KG_CTX_INCOMPLETE;
      return(GSS_S_NO_CONTEXT);
   }

   if (code = krb5_timeofday(context, &now)) {
      *minor_status = code;
      return(GSS_S_FAILURE);
   }

   if (code = make_seal_token(context, &ctx->enc, &ctx->seq,
			      &ctx->seq_send, ctx->initiate,
			      input_message_buffer, output_message_buffer,
			      conf_req_flag, toktype, ctx->big_endian)) {
      *minor_status = code;
      return(GSS_S_FAILURE);
   }

   if (((toktype == KG_TOK_SEAL_MSG) ||
	(toktype == KG_TOK_WRAP_MSG)) && conf_state) {
      *conf_state = conf_req_flag;
   }

   *minor_status = 0;
   return((ctx->endtime < now)?GSS_S_CONTEXT_EXPIRED:GSS_S_COMPLETE);
}

OM_uint32
kg_seal_size(context, minor_status, context_handle, conf_req_flag, qop_req, 
	     output_size, input_size)
    krb5_context        context;
    OM_uint32		*minor_status;
    gss_ctx_id_t	context_handle;
    int			conf_req_flag;
    gss_qop_t		qop_req;
    OM_uint32		output_size;
    OM_uint32		*input_size;
{
    krb5_gss_ctx_id_rec	*ctx;
    OM_uint32		cfsize;
    OM_uint32		ohlen;

    /* only default qop is allowed */
    if (qop_req != GSS_C_QOP_DEFAULT) {
	*minor_status = (OM_uint32) G_UNKNOWN_QOP;
	return(GSS_S_FAILURE);
    }
    
    /* validate the context handle */
    if (! kg_validate_ctx_id(context_handle)) {
	*minor_status = (OM_uint32) G_VALIDATE_FAILED;
	return(GSS_S_NO_CONTEXT);
    }
    
    ctx = (krb5_gss_ctx_id_rec *) context_handle;
    if (! ctx->established) {
	*minor_status = KG_CTX_INCOMPLETE;
	return(GSS_S_NO_CONTEXT);
    }

    /* Calculate the token size and subtract that from the output size */
    cfsize = (conf_req_flag) ? kg_confounder_size(&ctx->enc) : 0;
    ohlen = g_token_size((gss_OID) gss_mech_krb5,
			 (unsigned int) cfsize + 14 + kg_cksum_len(&ctx->enc));

    /* Cannot have trailer length that will cause us to pad over our length */
    *input_size = (output_size - ohlen) & (~7);
    *minor_status = 0;
    return(GSS_S_COMPLETE);
}
