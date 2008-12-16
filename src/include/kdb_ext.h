/*
 * include/krb5/kdb_ext.h
 *
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

#ifndef KRB5_KDB5_EXT__
#define KRB5_KDB5_EXT__

/* Can be delegated as in TicketFlags */
#define KRB5_KDB_OK_AS_DELEGATE		0x00100000
/* Allowed to use protocol transition */
#define KRB5_KDB_OK_TO_AUTH_AS_DELEGATE	0x00200000
/* Service does not require authorization data */
#define KRB5_KDB_NO_AUTH_DATA_REQUIRED	0x00400000
/* Used to indicate non-Windows client behaviour */
#define KRB5_KDB_NON_MS_PRINCIPAL	0x00800000
/* Private flag used to indicate principal is local TGS */
#define KRB5_KDB_TICKET_GRANTING_SERVICE	0x01000000
/* Private flag used to indicate trust is non-transitive */
#define KRB5_KDB_TRUST_NON_TRANSITIVE		0x02000000

/* Entry get flags */
/* Name canonicalization requested */
#define KRB5_KDB_FLAG_CANONICALIZE		0x00000010
/* Include authorization data generated by backend */
#define KRB5_KDB_FLAG_INCLUDE_PAC		0x00000020
/* Is AS-REQ (client referrals only) */
#define KRB5_KDB_FLAG_CLIENT_REFERRALS_ONLY	0x00000040
/* Map cross-realm principals */
#define KRB5_KDB_FLAG_MAP_PRINCIPALS		0x00000080
/* Protocol transition */
#define KRB5_KDB_FLAG_PROTOCOL_TRANSITION	0x00000100
/* Constrained delegation */
#define KRB5_KDB_FLAG_CONSTRAINED_DELEGATION	0x00000200
/* PKINIT */
#define KRB5_KDB_FLAG_PKINIT			0x00000400
/* User-to-user */
#define KRB5_KDB_FLAG_USER_TO_USER		0x00000800
/* Cross-realm */
#define KRB5_KDB_FLAG_CROSS_REALM		0x00001000

#define KRB5_KDB_FLAGS_S4U			( KRB5_KDB_FLAG_PROTOCOL_TRANSITION | \
						  KRB5_KDB_FLAG_CONSTRAINED_DELEGATION )

#define KRB5_TL_PAC_LOGON_INFO		0x0100 /* NDR encoded validation info */
#define KRB5_TL_SERVER_REFERRAL		0x0200 /* ASN.1 encoded ServerReferralInfo */
#define KRB5_TL_SVR_REFERRAL_DATA	0x0300 /* ASN.1 encoded PA-SVR-REFERRAL-DATA */
#define KRB5_TL_CONSTRAINED_DELEGATION_ACL 0x0400 /* Each entry is a permitted SPN */
#define KRB5_TL_LM_KEY			0x0500 /* LM OWF */
#define KRB5_TL_X509_SUBJECT_ISSUER_NAME 0x0600 /* <I>IssuerDN<S>SubjectDN */

krb5_error_code krb5_db_get_principal_ext ( krb5_context kcontext,
					    krb5_const_principal search_for,
					    unsigned int flags,
					    krb5_db_entry *entries,
					    int *nentries,
					    krb5_boolean *more );

krb5_error_code krb5_db_invoke ( krb5_context kcontext,
				 unsigned int method,
				 const krb5_data *req,
				 krb5_data *rep );

/* db_invoke methods */
#define KRB5_KDB_METHOD_SIGN_AUTH_DATA			0x00000010
#define KRB5_KDB_METHOD_CHECK_TRANSITED_REALMS		0x00000020
#define KRB5_KDB_METHOD_CHECK_POLICY_AS			0x00000030
#define KRB5_KDB_METHOD_CHECK_POLICY_TGS		0x00000040
#define KRB5_KDB_METHOD_AUDIT_AS			0x00000050
#define KRB5_KDB_METHOD_AUDIT_TGS			0x00000060
#define KRB5_KDB_METHOD_REFRESH_POLICY			0x00000070
#define KRB5_KDB_METHOD_GET_PAC_PRINC			0x00000080

typedef struct _kdb_sign_auth_data_req {
    krb5_magic magic;
    unsigned int flags;			/* KRB5_KDB flags */
    krb5_const_principal client_princ;	/* Client name used in ticket */
    krb5_db_entry *client;		/* DB entry for client principal */
    krb5_db_entry *server;		/* DB entry for server principal */
    krb5_db_entry *krbtgt;		/* DB entry for ticket granting service principal */
    krb5_keyblock *client_key;		/* Reply key, valid for AS-REQ only */
    krb5_keyblock *server_key;		/* Key used to generate server signature */
    krb5_timestamp authtime;		/* Authtime of TGT */
    krb5_authdata **auth_data;		/* Authorization data from TGT */
} kdb_sign_auth_data_req;

typedef struct _kdb_sign_auth_data_rep {
    krb5_magic magic;
    krb5_authdata **auth_data;
    krb5_db_entry *entry;
    int nprincs;
} kdb_sign_auth_data_rep;

typedef struct _kdb_check_transited_realms_req {
    krb5_magic magic;
    const krb5_data *tr_contents;
    const krb5_data *client_realm;
    const krb5_data *server_realm;
} kdb_check_transited_realms_req;

typedef struct _kdb_check_policy_as_req {
    krb5_magic magic;
    krb5_kdc_req *request;
    krb5_db_entry *client;
    krb5_db_entry *server;
    krb5_timestamp kdc_time;
} kdb_check_policy_as_req;

typedef struct _kdb_check_policy_as_rep {
    krb5_magic magic;
    const char *status;
} kdb_check_policy_as_rep;

typedef struct _kdb_check_policy_tgs_req {
    krb5_magic magic;
    krb5_kdc_req *request;
    krb5_db_entry *server;
    krb5_ticket *ticket;
} kdb_check_policy_tgs_req;

typedef struct _kdb_check_policy_tgs_rep {
    krb5_magic magic;
    const char *status;
} kdb_check_policy_tgs_rep;

typedef struct _kdb_audit_as_req {
    krb5_magic magic;
    krb5_kdc_req *request;
    krb5_db_entry *client;
    krb5_db_entry *server;
    krb5_timestamp authtime;
    krb5_error_code error_code;
} kdb_audit_as_req;

typedef struct _kdb_audit_tgs_req {
    krb5_magic magic;
    krb5_kdc_req *request;
    krb5_const_principal client;
    krb5_db_entry *server;
    krb5_timestamp authtime;
    krb5_error_code error_code;
} kdb_audit_tgs_req;

#endif /* KRB5_KDB5_EXT__ */
