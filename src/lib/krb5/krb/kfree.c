/*
 * lib/krb5/free/f_addr.c
 *
 * Copyright 1990-1998 by the Massachusetts Institute of Technology.
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
 * krb5_free_address()
 */
/*
 * Copyright (c) 2006-2008, Novell, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *   * The copyright holder's name is not used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "k5-int.h"

void KRB5_CALLCONV
krb5_free_address(krb5_context context, krb5_address *val)
{
    if (val->contents)
	free(val->contents);
    free(val);
}

void KRB5_CALLCONV
krb5_free_addresses(krb5_context context, krb5_address **val)
{
    register krb5_address **temp;

    for (temp = val; *temp; temp++) {
	if ((*temp)->contents)
	    free((*temp)->contents);
	free(*temp);
    }
    free(val);
}


void KRB5_CALLCONV
krb5_free_ap_rep(krb5_context context, register krb5_ap_rep *val)
{
    if (val->enc_part.ciphertext.data)
	free(val->enc_part.ciphertext.data);
    free(val);
}

void KRB5_CALLCONV
krb5_free_ap_req(krb5_context context, register krb5_ap_req *val)
{
    if (val->ticket)
	krb5_free_ticket(context, val->ticket);
    if (val->authenticator.ciphertext.data)
	free(val->authenticator.ciphertext.data);
    free(val);
}

void KRB5_CALLCONV
krb5_free_ap_rep_enc_part(krb5_context context, krb5_ap_rep_enc_part *val)
{
    if (val->subkey)
	krb5_free_keyblock(context, val->subkey);
    free(val);
}

void KRB5_CALLCONV
krb5_free_authenticator_contents(krb5_context context, krb5_authenticator *val)
{
    if (val->checksum) {
	krb5_free_checksum(context, val->checksum);
	val->checksum = 0;
    }
    if (val->client) {
	krb5_free_principal(context, val->client);
	val->client = 0;
    }
    if (val->subkey) {
	krb5_free_keyblock(context, val->subkey);
	val->subkey = 0;
    }
    if (val->authorization_data) {
	krb5_free_authdata(context, val->authorization_data);
	val->authorization_data = 0;
    }
}

void KRB5_CALLCONV
krb5_free_authdata(krb5_context context, krb5_authdata **val)
{
    register krb5_authdata **temp;

    for (temp = val; *temp; temp++) {
	if ((*temp)->contents)
	    free((*temp)->contents);
	free(*temp);
    }
    free(val);
}

void KRB5_CALLCONV
krb5_free_authenticator(krb5_context context, krb5_authenticator *val)
{
    krb5_free_authenticator_contents(context, val);
    free(val);
}

void KRB5_CALLCONV
krb5_free_checksum(krb5_context context, register krb5_checksum *val)
{
    krb5_free_checksum_contents(context, val);
    free(val);
}

void KRB5_CALLCONV
krb5_free_checksum_contents(krb5_context context, register krb5_checksum *val)
{
    if (val->contents) {
	free(val->contents);
	val->contents = 0;
    }
}

void KRB5_CALLCONV
krb5_free_cred(krb5_context context, register krb5_cred *val)
{
    if (val->tickets)
        krb5_free_tickets(context, val->tickets);
    if (val->enc_part.ciphertext.data)
	free(val->enc_part.ciphertext.data);
    free(val);
}

/*
 * krb5_free_cred_contents zeros out the session key, and then frees
 * the credentials structures 
 */

void KRB5_CALLCONV
krb5_free_cred_contents(krb5_context context, krb5_creds *val)
{
    if (val->client) {
	krb5_free_principal(context, val->client);
	val->client = 0;
    }
    if (val->server) {
	krb5_free_principal(context, val->server);
	val->server = 0;
    }
    if (val->keyblock.contents) {
	memset((char *)val->keyblock.contents, 0, val->keyblock.length);
	free(val->keyblock.contents);
	val->keyblock.contents = 0;
    }
    if (val->ticket.data) {
	free(val->ticket.data);
	val->ticket.data = 0;
    }
    if (val->second_ticket.data) {
	free(val->second_ticket.data);
	val->second_ticket.data = 0;
    }
    if (val->addresses) {
	krb5_free_addresses(context, val->addresses);
	val->addresses = 0;
    }
    if (val->authdata) {
	krb5_free_authdata(context, val->authdata);
	val->authdata = 0;
    }
}

void KRB5_CALLCONV 
krb5_free_cred_enc_part(krb5_context context, register krb5_cred_enc_part *val)
{
    register krb5_cred_info **temp;
    
    if (val->r_address) {
	krb5_free_address(context, val->r_address);
	val->r_address = 0;
    }
    if (val->s_address) {
	krb5_free_address(context, val->s_address);
	val->s_address = 0;
    }

    if (val->ticket_info) {
	for (temp = val->ticket_info; *temp; temp++) {
	    if ((*temp)->session)
		krb5_free_keyblock(context, (*temp)->session);
	    if ((*temp)->client)
		krb5_free_principal(context, (*temp)->client);
	    if ((*temp)->server)
		krb5_free_principal(context, (*temp)->server);
	    if ((*temp)->caddrs)
		krb5_free_addresses(context, (*temp)->caddrs);
	    free((*temp));
	}
	free(val->ticket_info);
	val->ticket_info = 0;
    }
}


void KRB5_CALLCONV
krb5_free_creds(krb5_context context, krb5_creds *val)
{
    krb5_free_cred_contents(context, val);
    free(val);
}


void KRB5_CALLCONV
krb5_free_data(krb5_context context, krb5_data *val)
{
    if (val->data)
	free(val->data);
    free(val);
}

void KRB5_CALLCONV
krb5_free_data_contents(krb5_context context, krb5_data *val)
{
    if (val->data) {
	free(val->data);
	val->data = 0;
    }
}

void krb5_free_etype_info(krb5_context context, krb5_etype_info info)
{
  int i;

  for(i=0; info[i] != NULL; i++) {
      if (info[i]->salt)
	  free(info[i]->salt);
      krb5_free_data_contents( context, &info[i]->s2kparams);
      free(info[i]);
  }
  free(info);
}
    

void KRB5_CALLCONV
krb5_free_enc_kdc_rep_part(krb5_context context, register krb5_enc_kdc_rep_part *val)
{
    if (val->session)
	krb5_free_keyblock(context, val->session);
    if (val->last_req)
	krb5_free_last_req(context, val->last_req);
    if (val->server)
	krb5_free_principal(context, val->server);
    if (val->caddrs)
	krb5_free_addresses(context, val->caddrs);
    free(val);
}

void KRB5_CALLCONV
krb5_free_enc_tkt_part(krb5_context context, krb5_enc_tkt_part *val)
{
    if (val->session)
	krb5_free_keyblock(context, val->session);
    if (val->client)
	krb5_free_principal(context, val->client);
    if (val->transited.tr_contents.data)
	free(val->transited.tr_contents.data);
    if (val->caddrs)
	krb5_free_addresses(context, val->caddrs);
    if (val->authorization_data)
	krb5_free_authdata(context, val->authorization_data);
    free(val);
}


void KRB5_CALLCONV
krb5_free_error(krb5_context context, register krb5_error *val)
{
    if (val->client)
	krb5_free_principal(context, val->client);
    if (val->server)
	krb5_free_principal(context, val->server);
    if (val->text.data)
	free(val->text.data);
    if (val->e_data.data)
	free(val->e_data.data);
    free(val);
}

void KRB5_CALLCONV
krb5_free_kdc_rep(krb5_context context, krb5_kdc_rep *val)
{
    if (val->padata)
	krb5_free_pa_data(context, val->padata);
    if (val->client)
	krb5_free_principal(context, val->client);
    if (val->ticket)
	krb5_free_ticket(context, val->ticket);
    if (val->enc_part.ciphertext.data)
	free(val->enc_part.ciphertext.data);
    if (val->enc_part2)
	krb5_free_enc_kdc_rep_part(context, val->enc_part2);
    free(val);
}


void KRB5_CALLCONV
krb5_free_kdc_req(krb5_context context, krb5_kdc_req *val)
{
    if (val->padata)
	krb5_free_pa_data(context, val->padata);
    if (val->client)
	krb5_free_principal(context, val->client);
    if (val->server)
	krb5_free_principal(context, val->server);
    if (val->ktype)
	free(val->ktype);
    if (val->addresses)
	krb5_free_addresses(context, val->addresses);
    if (val->authorization_data.ciphertext.data)
	free(val->authorization_data.ciphertext.data);
    if (val->unenc_authdata)
	krb5_free_authdata(context, val->unenc_authdata);
    if (val->second_ticket)
	krb5_free_tickets(context, val->second_ticket);
    free(val);
}

void KRB5_CALLCONV
krb5_free_keyblock_contents(krb5_context context, register krb5_keyblock *key)
{
    krb5int_c_free_keyblock_contents (context, key);
}

void KRB5_CALLCONV
krb5_free_keyblock(krb5_context context, register krb5_keyblock *val)
{
    krb5int_c_free_keyblock (context, val);
}



void KRB5_CALLCONV
krb5_free_last_req(krb5_context context, krb5_last_req_entry **val)
{
    register krb5_last_req_entry **temp;

    for (temp = val; *temp; temp++)
	free(*temp);
    free(val);
}

void KRB5_CALLCONV
krb5_free_pa_data(krb5_context context, krb5_pa_data **val)
{
    register krb5_pa_data **temp;

    for (temp = val; *temp; temp++) {
	if ((*temp)->contents)
	    free((*temp)->contents);
	free(*temp);
    }
    free(val);
}

void KRB5_CALLCONV
krb5_free_principal(krb5_context context, krb5_principal val)
{
    register krb5_int32 i;

    if (!val)
	return;
    
    if (val->data) {
	i = krb5_princ_size(context, val);
	while(--i >= 0)
	    free(krb5_princ_component(context, val, i)->data);
	free(val->data);
    }
    if (val->realm.data)
	free(val->realm.data);
    free(val);
}

void KRB5_CALLCONV
krb5_free_priv(krb5_context context, register krb5_priv *val)
{
    if (val->enc_part.ciphertext.data)
	free(val->enc_part.ciphertext.data);
    free(val);
}

void KRB5_CALLCONV
krb5_free_priv_enc_part(krb5_context context, register krb5_priv_enc_part *val)
{
    if (val->user_data.data)
	free(val->user_data.data);
    if (val->r_address)
	krb5_free_address(context, val->r_address);
    if (val->s_address)
	krb5_free_address(context, val->s_address);
    free(val);
}

void KRB5_CALLCONV
krb5_free_pwd_data(krb5_context context, krb5_pwd_data *val)
{
    if (val->element)
	krb5_free_pwd_sequences(context, val->element);
    free(val);
}


void KRB5_CALLCONV
krb5_free_pwd_sequences(krb5_context context, passwd_phrase_element **val)
{
    register passwd_phrase_element **temp;

    for (temp = val; *temp; temp++) {
	if ((*temp)->passwd) {
	   krb5_free_data(context, (*temp)->passwd);
	   (*temp)->passwd = 0;
	}
	if ((*temp)->phrase) {
	   krb5_free_data(context, (*temp)->phrase);
	   (*temp)->phrase = 0;
	}
	free(*temp);
    }
    free(val);
}


void KRB5_CALLCONV
krb5_free_safe(krb5_context context, register krb5_safe *val)
{
    if (val->user_data.data)
	free(val->user_data.data);
    if (val->r_address)
	krb5_free_address(context, val->r_address);
    if (val->s_address)
	krb5_free_address(context, val->s_address);
    if (val->checksum)
	krb5_free_checksum(context, val->checksum);
    free(val);
}


void KRB5_CALLCONV
krb5_free_ticket(krb5_context context, krb5_ticket *val)
{
    if (val->server)
	krb5_free_principal(context, val->server);
    if (val->enc_part.ciphertext.data)
	free(val->enc_part.ciphertext.data);
    if (val->enc_part2)
	krb5_free_enc_tkt_part(context, val->enc_part2);
    free(val);
}

void KRB5_CALLCONV
krb5_free_tickets(krb5_context context, krb5_ticket **val)
{
    register krb5_ticket **temp;

    for (temp = val; *temp; temp++)
        krb5_free_ticket(context, *temp);
    free(val);
}


void KRB5_CALLCONV
krb5_free_tgt_creds(krb5_context context, krb5_creds **tgts)
{
    register krb5_creds **tgtpp;
    for (tgtpp = tgts; *tgtpp; tgtpp++)
	krb5_free_creds(context, *tgtpp);
    free(tgts);
}

void KRB5_CALLCONV
krb5_free_tkt_authent(krb5_context context, krb5_tkt_authent *val)
{
    if (val->ticket)
	    krb5_free_ticket(context, val->ticket);
    if (val->authenticator)
	    krb5_free_authenticator(context, val->authenticator);
    free(val);
}

void KRB5_CALLCONV
krb5_free_unparsed_name(krb5_context context, char *val)
{
    if (val)
	free(val);
}

void KRB5_CALLCONV
krb5_free_sam_challenge(krb5_context ctx, krb5_sam_challenge *sc)
{
    if (!sc)
	return;
    krb5_free_sam_challenge_contents(ctx, sc);
    free(sc);
}

void KRB5_CALLCONV
krb5_free_sam_challenge_2(krb5_context ctx, krb5_sam_challenge_2 *sc2)
{
    if (!sc2)
	return;
    krb5_free_sam_challenge_2_contents(ctx, sc2);
    free(sc2);
}

void KRB5_CALLCONV
krb5_free_sam_challenge_contents(krb5_context ctx, krb5_sam_challenge *sc)
{
    if (!sc)
	return;
    if (sc->sam_type_name.data)
	krb5_free_data_contents(ctx, &sc->sam_type_name);
    if (sc->sam_track_id.data)
	krb5_free_data_contents(ctx, &sc->sam_track_id);
    if (sc->sam_challenge_label.data)
	krb5_free_data_contents(ctx, &sc->sam_challenge_label);
    if (sc->sam_challenge.data)
	krb5_free_data_contents(ctx, &sc->sam_challenge);
    if (sc->sam_response_prompt.data)
	krb5_free_data_contents(ctx, &sc->sam_response_prompt);
    if (sc->sam_pk_for_sad.data)
	krb5_free_data_contents(ctx, &sc->sam_pk_for_sad);
    if (sc->sam_cksum.contents) {
	free(sc->sam_cksum.contents);
	sc->sam_cksum.contents = 0;
    }
}

void KRB5_CALLCONV
krb5_free_sam_challenge_2_contents(krb5_context ctx,
				   krb5_sam_challenge_2 *sc2)
{
    krb5_checksum **cksump;

    if (!sc2)
	return;
    if (sc2->sam_challenge_2_body.data)
	krb5_free_data_contents(ctx, &sc2->sam_challenge_2_body);
    if (sc2->sam_cksum) {
	cksump = sc2->sam_cksum;
	while (*cksump) {
	    krb5_free_checksum(ctx, *cksump);
	    cksump++;
	}
	free(sc2->sam_cksum);
	sc2->sam_cksum = 0;
    }
}

void KRB5_CALLCONV
krb5_free_sam_challenge_2_body(krb5_context ctx,
			       krb5_sam_challenge_2_body *sc2)
{
    if (!sc2)
	return;
    krb5_free_sam_challenge_2_body_contents(ctx, sc2);
    free(sc2);
}

void KRB5_CALLCONV
krb5_free_sam_challenge_2_body_contents(krb5_context ctx,
					krb5_sam_challenge_2_body *sc2)
{
    if (!sc2)
	return;
    if (sc2->sam_type_name.data) 
	krb5_free_data_contents(ctx, &sc2->sam_type_name);
    if (sc2->sam_track_id.data)
	krb5_free_data_contents(ctx, &sc2->sam_track_id);
    if (sc2->sam_challenge_label.data)
	krb5_free_data_contents(ctx, &sc2->sam_challenge_label);
    if (sc2->sam_challenge.data)
	krb5_free_data_contents(ctx, &sc2->sam_challenge);
    if (sc2->sam_response_prompt.data)
	krb5_free_data_contents(ctx, &sc2->sam_response_prompt);
    if (sc2->sam_pk_for_sad.data)
	krb5_free_data_contents(ctx, &sc2->sam_pk_for_sad);
}

void KRB5_CALLCONV
krb5_free_sam_response(krb5_context ctx, krb5_sam_response *sr)
{
    if (!sr)
	return;
    krb5_free_sam_response_contents(ctx, sr);
    free(sr);
}

void KRB5_CALLCONV
krb5_free_sam_response_2(krb5_context ctx, krb5_sam_response_2 *sr2)
{
    if (!sr2)
	return;
    krb5_free_sam_response_2_contents(ctx, sr2);
    free(sr2);
}

void KRB5_CALLCONV
krb5_free_sam_response_contents(krb5_context ctx, krb5_sam_response *sr)
{
    if (!sr)
	return;
    if (sr->sam_track_id.data)
	krb5_free_data_contents(ctx, &sr->sam_track_id);
    if (sr->sam_enc_key.ciphertext.data)
	krb5_free_data_contents(ctx, &sr->sam_enc_key.ciphertext);
    if (sr->sam_enc_nonce_or_ts.ciphertext.data)
	krb5_free_data_contents(ctx, &sr->sam_enc_nonce_or_ts.ciphertext);
}

void KRB5_CALLCONV
krb5_free_sam_response_2_contents(krb5_context ctx, krb5_sam_response_2 *sr2)
{
    if (!sr2)
	return;
    if (sr2->sam_track_id.data)
	krb5_free_data_contents(ctx, &sr2->sam_track_id);
    if (sr2->sam_enc_nonce_or_sad.ciphertext.data)
	krb5_free_data_contents(ctx, &sr2->sam_enc_nonce_or_sad.ciphertext);
}

void KRB5_CALLCONV
krb5_free_predicted_sam_response(krb5_context ctx,
				 krb5_predicted_sam_response *psr)
{
    if (!psr)
	return;
    krb5_free_predicted_sam_response_contents(ctx, psr);
    free(psr);
}

void KRB5_CALLCONV
krb5_free_predicted_sam_response_contents(krb5_context ctx,
				 krb5_predicted_sam_response *psr)
{
    if (!psr)
	return;
    if (psr->sam_key.contents)
	krb5_free_keyblock_contents(ctx, &psr->sam_key);
    if (psr->client) {
	krb5_free_principal(ctx, psr->client);
	psr->client = 0;
    }
    if (psr->msd.data)
	krb5_free_data_contents(ctx, &psr->msd);
}

void KRB5_CALLCONV
krb5_free_enc_sam_response_enc(krb5_context ctx,
			       krb5_enc_sam_response_enc *esre)
{
    if (!esre)
	return;
    krb5_free_enc_sam_response_enc_contents(ctx, esre);
    free(esre);
}

void KRB5_CALLCONV 
krb5_free_enc_sam_response_enc_2(krb5_context ctx,
				 krb5_enc_sam_response_enc_2 *esre2)
{
    if (!esre2)
	return;
    krb5_free_enc_sam_response_enc_2_contents(ctx, esre2);
    free(esre2);
}

void KRB5_CALLCONV
krb5_free_enc_sam_response_enc_contents(krb5_context ctx,
			       krb5_enc_sam_response_enc *esre)
{
    if (!esre)
	return;
    if (esre->sam_sad.data)
	krb5_free_data_contents(ctx, &esre->sam_sad);
}

void KRB5_CALLCONV
krb5_free_enc_sam_response_enc_2_contents(krb5_context ctx,
					  krb5_enc_sam_response_enc_2 *esre2)
{
    if (!esre2)
	return;
    if (esre2->sam_sad.data)
	krb5_free_data_contents(ctx, &esre2->sam_sad);
}

void KRB5_CALLCONV
krb5_free_pa_enc_ts(krb5_context ctx, krb5_pa_enc_ts *pa_enc_ts)
{
    if (!pa_enc_ts)
	return;
    free(pa_enc_ts);
}

void KRB5_CALLCONV
krb5_free_pa_for_user(krb5_context context, krb5_pa_for_user *req)
{
    if (req == NULL)
	return;
    if (req->user != NULL) {
	krb5_free_principal(context, req->user);
	req->user = NULL;
    }
    krb5_free_checksum_contents(context, &req->cksum);
    krb5_free_data_contents(context, &req->auth_package);
    free(req);
}

void KRB5_CALLCONV
krb5_free_pa_server_referral_data(krb5_context context,
				  krb5_pa_server_referral_data *ref)
{
    if (ref == NULL)
	return;
    if (ref->referred_realm) {
	krb5_free_data(context, ref->referred_realm);
	ref->referred_realm = NULL;
    }
    if (ref->true_principal_name != NULL) {
	krb5_free_principal(context, ref->true_principal_name);
	ref->true_principal_name = NULL;
    } 
    if (ref->requested_principal_name != NULL) {
	krb5_free_principal(context, ref->requested_principal_name);
	ref->requested_principal_name = NULL;
    }
    krb5_free_checksum_contents(context, &ref->rep_cksum); 
    free(ref);
}

void KRB5_CALLCONV
krb5_free_pa_svr_referral_data(krb5_context context,
			       krb5_pa_svr_referral_data *ref)
{
    if (ref == NULL)
	return;
    if (ref->principal != NULL) {
	krb5_free_principal(context, ref->principal);
	ref->principal = NULL;
    } 
    free(ref);
}

void KRB5_CALLCONV
krb5_free_pa_pac_req(krb5_context context,
		     krb5_pa_pac_req *req)
{
    if (req == NULL)
	return;
    free(req);
}

void KRB5_CALLCONV
krb5_free_etype_list(krb5_context context,
		     krb5_etype_list *etypes)
{
    if (etypes != NULL) {
	if (etypes->etypes != NULL)
	    free(etypes->etypes);
	free(etypes);
    }
}
