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

/*
 * $Id$
 */

#include "gssapiP_krb5.h"

/** exported constants defined in gssapi_krb5{,_nx}.h **/

/* these are bogus, but will compile */

/*
 * The OID of the draft krb5 mechanism, assigned by IETF, is:
 * 	iso(1) org(3) dod(5) internet(1) security(5)
 *	kerberosv5(2) = 1.3.5.1.5.2
 * The OID of the krb5_name type is:
 * 	iso(1) member-body(2) US(840) mit(113554) infosys(1) gssapi(2)
 * 	krb5(2) krb5_name(1) = 1.2.840.113554.1.2.2.1
 * The OID of the krb5_principal type is:
 * 	iso(1) member-body(2) US(840) mit(113554) infosys(1) gssapi(2)
 * 	krb5(2) krb5_principal(2) = 1.2.840.113554.1.2.2.2
 * The OID of the proposed standard krb5 mechanism is:
 * 	iso(1) member-body(2) US(840) mit(113554) infosys(1) gssapi(2)
 * 	krb5(2) = 1.2.840.113554.1.2.2
 *	
 */

/*
 * Encoding rules: The first two values are encoded in one byte as 40
 * * value1 + value2.  Subsequent values are encoded base 128, most
 * significant digit first, with the high bit set on all octets except
 * the last in each value's encoding.
 */

static const gss_OID_desc oids[] = {
   /* XXXX this OID is from Ted.  It's not official yet, but it's close. */
   {5, "\053\005\001\005\002"},
   {10, "\052\206\110\206\367\022\001\002\002\001"},
   {10, "\052\206\110\206\367\022\001\002\002\002"},
   {9, "\052\206\110\206\367\022\001\002\002"},
};

const gss_OID_desc * const gss_mech_krb5 = oids+0;
const gss_OID_desc * const gss_nt_krb5_name = oids+1;
const gss_OID_desc * const gss_nt_krb5_principal = oids+2;
const gss_OID_desc * const gss_new_mech_krb5 = oids+3;

static const gss_OID_set_desc oidsets[] = {
   {1, (gss_OID) oids},
};

const gss_OID_set_desc * const gss_mech_set_krb5 = oidsets+0;

krb5_context kg_context;

void *kg_vdb = NULL;

/** default credential support */

/* default credentials */

static gss_cred_id_t defcred = GSS_C_NO_CREDENTIAL;

/* XXX what happens when the default credentials expire or are invalidated? */

OM_uint32
kg_get_defcred(minor_status, cred)
     OM_uint32 *minor_status;
     gss_cred_id_t *cred;
{
   if (defcred == GSS_C_NO_CREDENTIAL) {
      OM_uint32 major;

      if (!kg_context && kg_get_context())
	      return GSS_S_FAILURE;

      if ((major = krb5_gss_acquire_cred(kg_context, minor_status, 
					 (gss_name_t) NULL, GSS_C_INDEFINITE, 
					 GSS_C_NULL_OID_SET, GSS_C_INITIATE, 
					 &defcred, NULL, NULL)) &&
	  GSS_ERROR(major)) {
	 defcred = GSS_C_NO_CREDENTIAL;
	 return(major);
      }
   }

   *cred = defcred;
   *minor_status = 0;
   return(GSS_S_COMPLETE);
}

OM_uint32
kg_release_defcred(minor_status)
     OM_uint32 *minor_status;
{
   if (defcred == GSS_C_NO_CREDENTIAL) {
      *minor_status = 0;
      return(GSS_S_COMPLETE);
   }

   if (!kg_context && kg_get_context())
	   return GSS_S_FAILURE;
   
   return(krb5_gss_release_cred(kg_context, minor_status, &defcred));
}

OM_uint32
kg_get_context()
{
	if (kg_context)
		return GSS_S_COMPLETE;
	if (krb5_init_context(&kg_context))
		return GSS_S_FAILURE;
	krb5_init_ets(kg_context);
	return GSS_S_COMPLETE;
}
