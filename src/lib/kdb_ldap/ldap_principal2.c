/*
 * lib/kdb/kdb_ldap/ldap_principal2.c
 *
 * Copyright (c) 2004-2005, Novell, Inc.
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

#include <time.h>
#include "ldap_main.h"
#include "kdb_ldap.h"
#include "ldap_principal.h"
#include "princ_xdr.h"
#include "ldap_tkt_policy.h"
#include "ldap_pwd_policy.h"
#include "ldap_err.h"

extern char* principal_attributes[];
extern char* max_pwd_life_attr[];
#if !defined( LDAP_OPT_RESULT_CODE) && defined(LDAP_OPT_ERROR_NUMBER)
#define LDAP_OPT_RESULT_CODE LDAP_OPT_ERROR_NUMBER
#endif

static krb5_error_code
krb5_decode_krbsecretkey(krb5_context, krb5_db_entry *, struct berval **, krb5_tl_data *);

static krb5_error_code
krb5_read_tkt_policyreference(krb5_context, krb5_ldap_context *, krb5_db_entry *, char *);

static char *
getstringtime(krb5_timestamp );

/*
 * look up a principal in the directory.
 */

krb5_error_code
krb5_ldap_get_principal(context, searchfor, entries, nentries, more)
    krb5_context context;
    krb5_const_principal searchfor;
    krb5_db_entry *entries;	/* filled in */
    int *nentries;		/* how much room/how many found */
    krb5_boolean *more;		/* are there more? */
{
    char                        *user=NULL, *DN=NULL, *filter=NULL, *subtree[2]={NULL};
    unsigned int                tree=0, ntrees=1, mask=0, princlen=0;
    krb5_error_code	        tempst=0, st=0;
    char                        **values=NULL, *policydn=NULL, *pwdpolicydn=NULL, *modname=NULL;
    krb5_tl_data                userinfo_tl_data={0};
    krb5_timestamp              modtime=0;
    struct berval               **bvalues=NULL;
    LDAP	                *ld=NULL;
    LDAPMessage	                *result=NULL, *ent=NULL;
    krb5_ldap_context           *ldap_context=NULL;
    kdb5_dal_handle             *dal_handle=NULL;
    krb5_ldap_server_handle     *ldap_server_handle=NULL;
    krb5_principal              parsed_mod_name=NULL;
    krb5_boolean                attr_present=FALSE;

    /* Clear the global error string */
    krb5_clear_error_message(context);

    /* set initial values */
    *nentries = 0;
    *more = 0; 
    memset(entries, 0, sizeof(*entries));
    
    if (searchfor == NULL) 
	return EINVAL;

    dal_handle = (kdb5_dal_handle *) context->db_context;
    ldap_context = (krb5_ldap_context *) dal_handle->db_context;

    CHECK_LDAP_HANDLE(ldap_context);

    if (is_principal_in_realm(ldap_context, searchfor) != 0) {
	*more = 0;
	krb5_set_error_message (context, st, "Principal does not belong to realm");
	goto cleanup;
    }    
    
    if ((st=krb5_unparse_name(context, searchfor, &user)) != 0)
	goto cleanup; 

    if ((st=krb5_ldap_unparse_principal_name(user)) != 0)
	goto cleanup; 
   
    princlen = strlen(FILTER) + strlen(user) + 2 + 1;      /* 2 for closing brackets */
    if ((filter=malloc(princlen)) == NULL) {
	st = ENOMEM;
	goto cleanup;
    }
    snprintf(filter, princlen, FILTER"%s))", user);

    if ((st = krb5_get_subtree_info(ldap_context, subtree, &ntrees)) != 0)
	goto cleanup;

    GET_HANDLE();
    for (tree=0; tree<ntrees && *nentries==0; ++tree) {

	LDAP_SEARCH(subtree[tree], ldap_context->lrparams->search_scope, filter, principal_attributes);
	for (ent=ldap_first_entry(ld, result); ent != NULL && *nentries == 0; ent=ldap_next_entry(ld, ent)) {
	    
	    /* get the associated directory user information */
	    if ((values=ldap_get_values(ld, ent, "krbprincipalname")) != NULL) {
		int i=0, pcount=0, ptype=KDB_USER_PRINCIPAL;
		
		/* a wild-card in a principal name can return a list of kerberos principals. 
		 * Make sure that the correct principal is returned.
		 * NOTE: a principalname k* in ldap server will return all the principals starting with a k
		 */
		for (i=0; values[i] != NULL; ++i) {
		    if (strcasecmp(values[i], user) == 0) {
			*nentries = 1;
			pcount = ldap_count_values(values);
			break;
		    }
		}
		ldap_value_free(values);
		
		if (*nentries == 0) /* no matching principal found */
		    continue;

		if ((DN = ldap_get_dn(ld, ent)) == NULL) {
                    ldap_get_option (ld, LDAP_OPT_RESULT_CODE, &st);
		    st = set_ldap_error (context, st, 0);
		    goto cleanup;
		}
		
		if ((values=ldap_get_values(ld, ent, "objectclass")) != NULL) {	    
		    for(i=0; values[i] != NULL; ++i)
			if (strcasecmp(values[i], "krbprincipal") == 0) {
			    ptype = KDB_SERVICE_PRINCIPAL;
			    break;
			}
		    ldap_value_free(values);
		}
		  
		/* add principalcount, DN and principaltype user information to tl_data */
		if (((st=store_tl_data(&userinfo_tl_data, KDB_TL_PRINCCOUNT, &pcount)) != 0) ||
		    ((st=store_tl_data(&userinfo_tl_data, KDB_TL_USERDN, DN)) != 0) ||
		    ((st=store_tl_data(&userinfo_tl_data, KDB_TL_PRINCTYPE, &ptype)) != 0))
		    goto cleanup;
	    }
   
	    /* populate entries->princ with searchfor value */
	    if ((st=krb5_copy_principal(context, searchfor, &(entries->princ))) != 0)
		goto cleanup;

	    /* read all the kerberos attributes */

	    /* KRBMAXTICKETLIFE */
	    if (krb5_ldap_get_value(ld, ent, "krbmaxticketlife", &(entries->max_life)) == 0)
		mask |= KDB_MAX_LIFE_ATTR;
	    
	    /* KRBMAXRENEWABLEAGE */
	    if (krb5_ldap_get_value(ld, ent, "krbmaxrenewableage", &(entries->max_renewable_life)) == 0)
		mask |= KDB_MAX_RLIFE_ATTR;

	    /* KRBTICKETFLAGS */
	    if (krb5_ldap_get_value(ld, ent, "krbticketflags", &(entries->attributes)) == 0)
		mask |= KDB_TKT_FLAGS_ATTR;

	    /* PRINCIPAL EXPIRATION TIME */
	    if ((st=krb5_ldap_get_time(ld, ent, "krbprincipalexpiration", &(entries->expiration), 
				       &attr_present)) != 0)
		goto cleanup;
	    if (attr_present == TRUE)
		mask |= KDB_PRINC_EXPIRE_TIME_ATTR;
	    
	    /* PASSWORD EXPIRATION TIME */
	    if ((st=krb5_ldap_get_time(ld, ent, "krbpasswordexpiration", &(entries->pw_expiration), 
				       &attr_present)) != 0)
		goto cleanup;
	    if (attr_present == TRUE)
		mask |= KDB_PWD_EXPIRE_TIME_ATTR;

	    /* KRBPOLICYREFERENCE */
	    if ((st=krb5_ldap_get_string(ld, ent, "krbpolicyreference", &policydn, &attr_present)) != 0)
		goto cleanup;
	    if (attr_present == TRUE)
		mask |= KDB_POL_REF_ATTR;

	    /* KRBPWDPOLICYREFERENCE */
	    if ((st=krb5_ldap_get_string(ld, ent, "krbpwdpolicyreference", &pwdpolicydn, &attr_present)) != 0)
		goto cleanup;
	    if (attr_present == TRUE) {
		krb5_tl_data  kadm_tl_data;

		mask |= KDB_PWD_POL_REF_ATTR;
		if((st = krb5_update_tl_kadm_data(pwdpolicydn, &kadm_tl_data)) != 0){
		    goto cleanup;
		}
		krb5_dbe_update_tl_data(context, entries, &kadm_tl_data);
	    }

	    /* KRBSECRETKEY */
	    if((bvalues=ldap_get_values_len(ld, ent, "krbsecretkey")) != NULL) {
		mask |= KDB_SECRET_KEY;
		if ((st=krb5_decode_krbsecretkey(context, entries, bvalues, &userinfo_tl_data)) != 0)
		    goto cleanup;
	    }
	    
	    /* MODIFY TIMESTAMP */
	    if ((st=krb5_ldap_get_time(ld, ent, "modifytimestamp", &modtime, &attr_present)) != 0)
		goto cleanup;

	    /* MODIFIER'S NAME */
	    if ((st=krb5_ldap_get_string(ld, ent, "modifiersname", &modname, &attr_present)) != 0)
		goto cleanup;
	    if (attr_present == TRUE) {
		if ((st=krb5_parse_name(context, modname, &parsed_mod_name)) != 0)
		    goto cleanup;

		if ((st=krb5_dbe_update_mod_princ_data(context, entries, modtime, parsed_mod_name)) != 0)
		    goto cleanup;
	    }

	    /* update the mask of attributes present on the directory object to the tl_data */
	    if ((st=store_tl_data(&userinfo_tl_data, KDB_TL_MASK, &mask)) != 0)
		goto cleanup;
	    if ((st=krb5_dbe_update_tl_data(context, entries, &userinfo_tl_data)) != 0)
		goto cleanup;
	    
#ifdef HAVE_EDIRECTORY
	    {
		krb5_timestamp              expiretime=0;
		char                        *is_login_disabled=NULL;

		/* LOGIN EXPIRATION TIME */
		if ((st=krb5_ldap_get_time(ld, ent, "loginexpirationtime", &expiretime, 
					   &attr_present)) != 0)
		    goto cleanup;
		
		if (attr_present == TRUE) {
		    if ((mask & KDB_PRINC_EXPIRE_TIME_ATTR) == 1) {
			if (expiretime < entries->expiration)
			    entries->expiration = expiretime;
		    } else {
			entries->expiration = expiretime;
		    }
		}
	    
		/* LOGIN DISABLED */	    
		if ((st=krb5_ldap_get_string(ld, ent, "logindisabled", &is_login_disabled, &attr_present)) != 0)
		    goto cleanup;
		if (attr_present == TRUE) {
		    if (strcasecmp(is_login_disabled,"TRUE")== 0)
			entries->attributes |= KRB5_KDB_DISALLOW_ALL_TIX;	
		    free (is_login_disabled);
		}
	    }
#endif
	}
	ldap_msgfree(result);
	result = NULL;
    } /* for(tree=0 ... */
    
    /* once done, put back the ldap handle */
    krb5_ldap_put_handle_to_pool(ldap_context, ldap_server_handle);
    ldap_server_handle = NULL;
    
    /* if principal not found */
    if (*nentries == 0)
	goto cleanup;
    
    if ((st=krb5_read_tkt_policyreference(context, ldap_context, entries, policydn)) !=0)
	goto cleanup;
    
    if (pwdpolicydn) {
	osa_policy_ent_t   pwdpol;
	int                cnt=0;
	krb5_timestamp     last_pw_changed;
	krb5_ui_4          pw_max_life; 

	memset(&pwdpol, 0, sizeof(pwdpol));
	
	if ((st=krb5_ldap_get_password_policy(context, pwdpolicydn, &pwdpol, &cnt)) != 0)
	    goto cleanup;
	pw_max_life = pwdpol->pw_max_life;
	free (pwdpol);

	if (pw_max_life > 0) {
	    if ((st=krb5_dbe_lookup_last_pwd_change(context, entries, &last_pw_changed)) != 0)
		goto cleanup;

	    if ((mask & KDB_PWD_EXPIRE_TIME_ATTR) == 1) { 
		if ((last_pw_changed + pw_max_life) < entries->pw_expiration)
		    entries->pw_expiration = last_pw_changed + pw_max_life;
	    } else
		entries->pw_expiration = last_pw_changed + pw_max_life;
	}
    }
    
 cleanup:
    ldap_msgfree(result);
    
    if (*nentries == 0 || st != 0)
	krb5_dbe_free_contents(context, entries);

    if (filter) 
	free (filter);

    if (DN)
	ldap_memfree (DN);

    for (; ntrees; --ntrees)
	if (subtree[ntrees-1]) 
	    free (subtree[ntrees-1]);

    if (userinfo_tl_data.tl_data_contents)
	free(userinfo_tl_data.tl_data_contents);
    
    if (ldap_server_handle)    
    	krb5_ldap_put_handle_to_pool(ldap_context, ldap_server_handle);

    if (user)
	free(user);
    
    if (modname)
	free(modname);
    
    if (parsed_mod_name)
	krb5_free_principal(context, parsed_mod_name);

    if (pwdpolicydn)
	free(pwdpolicydn);

    if (policydn)
	free(policydn);

    return st;
}

typedef struct _xargs_t {
    int            ptype;
    char           *dn;
    krb5_boolean   dn_from_kbd;
    char           *containerdn;
    char           *tktpolicydn;
}xargs_t;

static void 
free_xargs(xargs)
    xargs_t xargs;
{
    if (xargs.dn)
	free (xargs.dn);
    if (xargs.containerdn)
	free (xargs.containerdn);
    if (xargs.tktpolicydn)
	free (xargs.tktpolicydn);
}

static krb5_error_code 
process_db_args(context, db_args, xargs)
    krb5_context   context;
    char           **db_args;
    xargs_t        *xargs;
{
    int                   i=0;
    krb5_error_code       st=0;
    char                  errbuf[1024];
    char *arg=NULL,       *arg_val=NULL;
    unsigned int          arg_val_len=0;
    krb5_boolean          uflag=FALSE, cflag=FALSE;
    
    xargs->ptype = KDB_SERVICE_PRINCIPAL;
    if (db_args)
    {
	for (i=0; db_args[i]; ++i) {
	    arg = strtok_r(db_args[i], "=", &arg_val);
	    if(strcmp(arg, USERDN_ARG) == 0) {
		if (cflag == TRUE) {
		    st = EINVAL;
                    krb5_set_error_message(context, st, "'containerdn' and 'userdn' can not both "
                            "be specified");
		    goto cleanup;
		}
		if (xargs->dn != NULL || xargs->containerdn != NULL) {
		    st = EINVAL;
                    snprintf(errbuf, sizeof(errbuf), "%s option not supported", arg);
                    krb5_set_error_message(context, st, "%s", errbuf);
		    goto cleanup;
		}
		if (strcmp(arg_val, "") == 0 || arg_val == NULL) {
		    st = EINVAL;
		    snprintf(errbuf, sizeof(errbuf), "%s option value missing", arg);
		    krb5_set_error_message(context, st, "%s", errbuf);
		    goto cleanup;
		}
		arg_val_len = strlen(arg_val) + 1;
		xargs->dn = calloc (1, arg_val_len);
		if (xargs->dn == NULL) {
		    st = ENOMEM;
		    goto cleanup;
		}
		uflag = TRUE;
		xargs->ptype = KDB_USER_PRINCIPAL;
		xargs->dn_from_kbd = TRUE;
		memcpy(xargs->dn, arg_val, arg_val_len);
	    } else if (strcmp(arg, CONTAINERDN_ARG) == 0) {
		if (uflag == TRUE) {
		    st = EINVAL;
                    krb5_set_error_message(context, st, "'containerdn' and 'userdn' can not both "
                            "be specified");
		    goto cleanup;
		}
		if (xargs->dn != NULL || xargs->containerdn != NULL) {
		    st = EINVAL;
                    snprintf(errbuf, sizeof(errbuf), "%s option not supported", arg);
                    krb5_set_error_message(context, st, "%s", errbuf);
		    goto cleanup;
		}
		if (strcmp(arg_val, "") == 0 || arg_val == NULL) {
		    st = EINVAL;
		    snprintf(errbuf, sizeof(errbuf), "%s option value missing", arg);
		    krb5_set_error_message(context, st, "%s", errbuf);
		    goto cleanup;
		}
		arg_val_len = strlen(arg_val) + 1;
		xargs->containerdn = calloc (1, arg_val_len);
		if (xargs->containerdn == NULL) {
		    st = ENOMEM;
		    goto cleanup;
		}
		cflag = TRUE;
		xargs->ptype = KDB_SERVICE_PRINCIPAL;
		xargs->dn_from_kbd = TRUE;
		memcpy(xargs->containerdn, arg_val, arg_val_len);
	    } else if (strcmp(arg, TKTPOLICYDN_ARG) == 0) {
		if (arg_val == NULL) {
		    st = EINVAL;
		    snprintf(errbuf, sizeof(errbuf), "%s option value missing", arg);
		    krb5_set_error_message(context, st, "%s", errbuf);
		    goto cleanup;
		}
		arg_val_len = strlen(arg_val) + 1;
		xargs->tktpolicydn = calloc (1, arg_val_len);
		if (xargs->tktpolicydn == NULL) {
		    st = ENOMEM;
		    goto cleanup;
		}
		memcpy(xargs->tktpolicydn, arg_val, arg_val_len);
		
	    } else {
		st = EINVAL;
		snprintf(errbuf, sizeof(errbuf), "unknown option: %s", arg);
		krb5_set_error_message(context, st, "%s", errbuf);
		goto cleanup;
	    }
	}
    }
 cleanup:
    return st;
}

krb5_error_code
krb5_ldap_put_principal(context, entries, nentries, db_args)
    krb5_context               context;
    krb5_db_entry              *entries;
    register int               *nentries;         /* number of entry structs to update */
    char                       **db_args;
{
    int 		        i=0, l=0, plen=0;
    krb5_error_code 	        st=0, tempst=0;
    LDAP  		        *ld=NULL;
    LDAPMessage                 *result=NULL, *ent=NULL;
    char                        *user=NULL, *subtree=NULL;
    char                        **values=NULL, *strval[10]={NULL}, errbuf[1024];
    struct berval	        **bersecretkey=NULL;
    LDAPMod 		        **mods=NULL;
    krb5_boolean                dnfound=TRUE, tktpolicy_set=FALSE;
    krb5_tl_data                *tl_data=NULL;
    krb5_key_data               **keys=NULL;
    KEY                         *oldkeys=NULL;
    kdb5_dal_handle             *dal_handle=NULL;
    krb5_ldap_context           *ldap_context=NULL;
    krb5_ldap_server_handle     *ldap_server_handle=NULL;    
    osa_princ_ent_rec 	        princ_ent;
    xargs_t                     xargs={0};

    /* Clear the global error string */
    krb5_clear_error_message(context);

    SETUP_CONTEXT();
    if (ldap_context->lrparams == NULL || ldap_context->krbcontainer == NULL)
	return EINVAL;
    
    /* get ldap handle */
    GET_HANDLE();
    
    for (i=0; i < *nentries; ++i, ++entries) {
	if (is_principal_in_realm(ldap_context, entries->princ) != 0) {
            st = EINVAL;
            krb5_set_error_message(context, st, "Principal does not belong to the default realm");
	    goto cleanup;
	}
    
	/* get the principal information to act on */
	if (entries->princ) {
	    if (((st=krb5_unparse_name(context,entries->princ, &user)) !=0) ||
		((st=krb5_ldap_unparse_principal_name(user)) != 0))
		goto cleanup;
	    plen = strlen(user);
	}

	if (((st=krb5_get_princ_type(context, entries, &(xargs.ptype))) != 0) ||
	    ((st=krb5_get_userdn(context, entries, &(xargs.dn))) != 0) ||
	    ((st=krb5_get_secretkeys(context, entries, &oldkeys)) != 0)) 
	    goto cleanup;
    
	if ((st=process_db_args(context, db_args, &xargs)) != 0)
	    goto cleanup;

	if (xargs.dn == NULL) { /* creation of service principal */
	    if (xargs.ptype == KDB_USER_PRINCIPAL) {
		st = EINVAL;
		krb5_set_error_message(context, st, "User DN is missing");
		goto cleanup;
	    }
	
	    /* get the subtree information */ 
	    if (entries->princ->length == 2 && entries->princ->data[0].length == strlen("krbtgt") && 
		strncmp(entries->princ->data[0].data, "krbtgt", entries->princ->data[0].length) == 0) {
		/* if the principal is a inter-realm principal, always created in the realm container */
		subtree = strdup(ldap_context->lrparams->realmdn);
	    } else if (xargs.containerdn) {
		if ((st=checkattributevalue(ld, xargs.containerdn, NULL, NULL, NULL)) != 0) {
                    if (st == KRB5_KDB_NOENTRY || st == KRB5_KDB_CONSTRAINT_VIOLATION) {
			int ost = st;
                        st = EINVAL;
                        sprintf(errbuf, "'%s' not found: ", xargs.containerdn);
                        prepend_err_str(context, errbuf, st, ost);
                    }
		    goto cleanup;
		}
		subtree = strdup(xargs.containerdn);
	    } else if (ldap_context->lrparams->subtree && strlen(ldap_context->lrparams->subtree) != 0) {
		subtree = strdup(ldap_context->lrparams->subtree);
	    } else {
		subtree = strdup(ldap_context->lrparams->realmdn);
	    }
	    CHECK_NULL(subtree);

	    xargs.dn = malloc(strlen("krbprincipalname=") + strlen(user) + strlen(",") + 
			      strlen(subtree) + 1);
	    CHECK_NULL(xargs.dn);
	    sprintf(xargs.dn, "krbprincipalname=%s,%s", user, subtree);

	}
    
	if (xargs.dn_from_kbd == TRUE) {
	    /* make sure the DN falls in the subtree */
	    int              tre=0, dnlen=0, subtreelen=0, ntrees=0;
	    char             *subtreelist[2]={NULL};
	    krb5_boolean     outofsubtree=TRUE;
	    
	    /* get the current subtree list */
	    if ((st = krb5_get_subtree_info(ldap_context, subtreelist, &ntrees)) != 0)
		goto cleanup;
	    
	    for( tre=0; tre<ntrees; ++tre ) {
		if( subtreelist[tre] == NULL || strlen(subtreelist[tre]) == 0 ) {  
		    outofsubtree = FALSE;
		    break;
		} else {
		    dnlen = strlen (xargs.dn);
		    subtreelen = strlen(subtreelist[tre]);
		    if (strcasecmp((xargs.dn + dnlen - subtreelen), subtreelist[tre]) == 0) {
			outofsubtree = FALSE;
			break;
		    }
		}
	    }
		
	    for( tre=0; tre < ntrees; ++tre ) {	
		free( subtreelist[tre] );
	    }
		
	    if( outofsubtree == TRUE ) {
		st = EINVAL;
		krb5_set_error_message(context, st, "DN is out of the realm subtree");
		goto cleanup;
	    }
	}
    
	/* check if the DN exists */
	{
	    char  *attributes[]={"krbpolicyreference", NULL};
	
	    LDAP_SEARCH_1(xargs.dn, LDAP_SCOPE_BASE, 0, attributes,IGNORE_STATUS);
	    if (st == LDAP_NO_SUCH_OBJECT) {
		dnfound = FALSE;
		st = LDAP_SUCCESS;
	    } else if (st == LDAP_SUCCESS) { 
		ent = ldap_first_entry(ld, result);
		if (ent != NULL) {
		    if ((values=ldap_get_values(ld, ent, "krbpolicyreference")) != NULL) {
			tktpolicy_set = TRUE;
			ldap_value_free(values);
		    }
		}
		ldap_msgfree(result);
	    } else {
		st = set_ldap_error(context, st, OP_SEARCH);
		goto cleanup;
	    }
	}
    
	if (dnfound == FALSE) { 	    /* create a new object */
	    if (xargs.ptype == KDB_USER_PRINCIPAL) {
		memset(strval, 0, sizeof(strval));
		strval[0] = "inetorgperson";
		strval[1] = "Person";
		strval[2] = "krbprincipalaux";
		strval[3] = "krbpolicyaux";
		strval[4] = "krbpwdpolicyrefaux";
		if ((st=krb5_add_str_mem_ldap_mod(&mods, "objectclass", LDAP_MOD_ADD, strval)) != 0)
		    goto cleanup;
		values = ldap_explode_dn(xargs.dn, 1);
		if (values == NULL) {
                    st = EINVAL;
                    krb5_set_error_message(context, st, "Invalid DN");
		    goto cleanup;
		}
		memset(strval, 0, sizeof(strval));
		strval[0] = values[0];
		if ((st=krb5_add_str_mem_ldap_mod(&mods, "cn", LDAP_MOD_ADD, strval)) != 0) {
		    ldap_value_free(values);
		    goto cleanup;
		}
		/* surname is set same as the cn */
		if ((st=krb5_add_str_mem_ldap_mod(&mods, "Surname", LDAP_MOD_ADD, strval)) != 0) {
		    ldap_value_free(values);
		    goto cleanup;
		}
		ldap_value_free(values);
	    } else { 
		memset(strval, 0, sizeof(strval));
		strval[0] = "krbprincipal";
		strval[1] = "krbprincipalaux";
		strval[2] = "krbpolicyaux";
		strval[3] = "krbpwdpolicyrefaux";
		if ((st=krb5_add_str_mem_ldap_mod(&mods, "objectclass", LDAP_MOD_ADD, strval)) != 0)
		    goto cleanup;
	    }
	} else { /* update the objectclass attribute if any of these is missing */
	    char *attrvalues[] = {"krbprincipalaux", "krbpolicyaux", "krbpwdpolicyrefaux", NULL};
	    int p, q, r=0, amask=0;
	
	    if ((st=checkattributevalue(ld, xargs.dn, "objectclass", attrvalues, &amask)) != 0) {
                st = KRB5_KDB_UK_RERROR;
		goto cleanup;
	    }
	    memset(strval, 0, sizeof(strval));
	    for(p=1, q=0; p<=4; p<<=1, ++q) {
		if ((p & amask) == 0)
		    strval[r++] = attrvalues[q];
	    }
	    if (r != 0) {
		if ((st=krb5_add_str_mem_ldap_mod(&mods, "objectclass", LDAP_MOD_ADD, strval)) != 0)
		    goto cleanup;
	    }
	}
    
	if (entries->mask & KDB_MAX_LIFE) {    
	    if ((st=krb5_add_int_mem_ldap_mod(&mods, "krbmaxticketlife", LDAP_MOD_REPLACE, entries->max_life)) != 0)
		goto cleanup;
	}
    
	if (entries->mask & KDB_MAX_RLIFE) {
	    if ((st=krb5_add_int_mem_ldap_mod(&mods, "krbmaxrenewableage", LDAP_MOD_REPLACE,
					      entries->max_renewable_life)) != 0)
		goto cleanup;
	}
    
	if (entries->mask & KDB_ATTRIBUTES) {
	    if ((st=krb5_add_int_mem_ldap_mod(&mods, "krbticketflags", LDAP_MOD_REPLACE,
					      entries->attributes)) != 0)
		goto cleanup;
	}
    
	if (entries->mask & KDB_PRINCIPAL) {
	    memset(strval, 0, sizeof(strval));
	    strval[0] = user;
	    if ((st=krb5_add_str_mem_ldap_mod(&mods, "krbprincipalname", LDAP_MOD_ADD, strval)) != 0)
		goto cleanup;
	}

	if (entries->mask & KDB_PRINC_EXPIRE_TIME) {
	    memset(strval, 0, sizeof(strval));
	    if ((strval[0]=getstringtime(entries->expiration)) == NULL)
		goto cleanup;
	    if ((st=krb5_add_str_mem_ldap_mod(&mods, "krbprincipalexpiration", LDAP_MOD_REPLACE, strval)) != 0) {
		free (strval[0]);
		goto cleanup;
	    }
	    free (strval[0]);
	}
    
	if (entries->mask & KDB_PW_EXPIRATION) {
#ifdef HAVE_EDIRECTORY 	/* applies only to the kerberos user principal */ 
	    if (xargs.ptype == KDB_SERVICE_PRINCIPAL) {
		st = EINVAL;
		goto cleanup;
	    }
#endif
	    memset(strval, 0, sizeof(strval));
	    if ((strval[0]=getstringtime(entries->pw_expiration)) == NULL)
		goto cleanup;
	    if ((st=krb5_add_str_mem_ldap_mod(&mods, 
#ifdef HAVE_EDIRECTORY
					      "passwordexpirationtime",
#else
					      "krbpasswordexpiration",
#endif
					      LDAP_MOD_REPLACE, strval)) != 0) {
		free (strval[0]);
		goto cleanup;
	    }
	    free (strval[0]);
	}
    
	if (entries->mask & KDB_POLICY) {
	    for(tl_data=entries->tl_data; tl_data; tl_data=tl_data->tl_data_next) {
		if (tl_data->tl_data_type == KRB5_TL_KADM_DATA) {
		    memset(&princ_ent, 0, sizeof(princ_ent));
		    /* FIX ME: I guess the princ_ent should be freed after this call */
		    if((st = krb5_lookup_tl_kadm_data(tl_data, &princ_ent)) != 0) {
			goto cleanup;
		    }
		}
	    }
	
	    if(princ_ent.aux_attributes & KDB_POLICY) { 
		memset(strval, 0, sizeof(strval));
		strval[0] = princ_ent.policy;
		if ((st=krb5_add_str_mem_ldap_mod(&mods, "krbpwdpolicyreference", LDAP_MOD_REPLACE, strval)) != 0)
		    goto cleanup;
	    } else {
		st = EINVAL;
		krb5_set_error_message(context, st, "Password policy value null");
		goto cleanup;
	    }
	}
    
	if (entries->mask & KDB_POLICY_CLR) {
	    memset(strval, 0, sizeof(strval));
	    if ((st=krb5_add_str_mem_ldap_mod(&mods, "krbpwdpolicyreference", LDAP_MOD_DELETE, strval)) != 0)
		goto cleanup;
	}
    
	if (entries->mask & KDB_KEY_DATA || entries->mask & KDB_KVNO) {
	    int kcount=0, zero=0, salttype=0, totalkeys=0;
	    char *currpos=NULL, *krbsecretkey=NULL;
	
	    /* delete the old keys */
	    if (oldkeys) {
		if ((st=krb5_add_ber_mem_ldap_mod(&mods, "krbsecretkey", LDAP_MOD_DELETE | LDAP_MOD_BVALUES, 
						  oldkeys->keys)) != 0)
		    goto cleanup;
	    }

	    bersecretkey = malloc (sizeof(struct berval *) * (entries->n_key_data + 1));
	    CHECK_NULL(bersecretkey);
	    memset(bersecretkey, 0, sizeof(struct berval *) * (entries->n_key_data + 1));

	    keys = malloc (sizeof (krb5_key_data *) * entries->n_key_data + 1);
	    CHECK_NULL(keys);
	    memset(keys, 0, (sizeof (krb5_key_data *) * entries->n_key_data + 1));
	    for (kcount=0; kcount < entries->n_key_data; ++kcount)
		keys[kcount] = entries->key_data+kcount;
	    totalkeys = entries->n_key_data;
      
	    kcount = 0;
	    while (totalkeys) {
		int                    noofkeys=0, currkvno=0, currkvno_org=0, rlen=0;
		krb5_timestamp         last_pw_changed=0;

		krbsecretkey = malloc (MAX_KEY_LENGTH);
		CHECK_NULL(krbsecretkey);
		memset(krbsecretkey, 0, MAX_KEY_LENGTH);
		currpos = krbsecretkey;
		rlen = MAX_KEY_LENGTH;
	
		STORE16_INT(currpos, plen); /* principal len */
		currpos +=2;
		rlen -=2;
	
		for (l=0; l < entries->n_key_data; ++l)
		    if (keys[l] != NULL) {
			currkvno = keys[l]->key_data_kvno;
			break;
		    }

		currkvno_org = currkvno;
		STORE16_INT(currpos, currkvno); /* principal key version */
		currpos +=2;
		rlen -=2;
	
		memset(currpos, 0, 2); /* master key version */
		currpos +=2;
		rlen -=2;
	
		if ((st=krb5_dbe_lookup_last_pwd_change(context, entries, &last_pw_changed)) != 0)
		    goto cleanup;
		STORE32_INT(currpos, last_pw_changed); /* last pwd change */
		currpos += 4;
		rlen -=4;
	    
		for (noofkeys=0; l < entries->n_key_data; ++l)
		    if (keys[l] && keys[l]->key_data_kvno == currkvno_org)
			++noofkeys;
	
		STORE16_INT(currpos, noofkeys); /* number of keys */
		currpos +=2;
		rlen -=2;
	
		/* key type, key length, salt type and salt type */
		for (l=0; l<entries->n_key_data; ++l) {
		    if ( keys[l] && keys[l]->key_data_kvno == currkvno_org) {
			STORE16_INT(currpos, keys[l]->key_data_type[0]);
			currpos +=2;
		        rlen -=2;
			STORE16_INT(currpos, keys[l]->key_data_length[0]);
			currpos +=2;
		        rlen -=2;
	    
			STORE16_INT(currpos, keys[l]->key_data_type[1]); 
			currpos +=2;
		        rlen -=2;
			salttype = keys[l]->key_data_type[1];
			if (salttype==KRB5_KDB_SALTTYPE_NOREALM || salttype==KRB5_KDB_SALTTYPE_ONLYREALM) {
			    STORE16_INT(currpos, zero);	      
			} else {
			    STORE16_INT(currpos, keys[l]->key_data_length[1]);
			}
			currpos +=2;
		        rlen -=2;
		    } 
		}
                if (plen > rlen) {
                    st = EINVAL;
                    snprintf(errbuf, sizeof(errbuf), "Insufficient buffer while storing the key of principal %s", user);
                    krb5_set_error_message(context, st, "%s", errbuf);
                    goto cleanup;
                }
		memcpy(currpos, user, (unsigned int)plen);   /* principal name */
		currpos +=plen;
		rlen -=plen;
	
		/* key value, salt value */
		for(l=0; l<entries->n_key_data; ++l) {
		    if (keys[l] && keys[l]->key_data_kvno == currkvno_org) {
			if (keys[l]->key_data_length[0]) {
                            if (keys[l]->key_data_length[0] > rlen) {
                                st = EINVAL;
                                snprintf(errbuf, sizeof(errbuf), "Insufficient buffer while storing the key of principal %s", user);
                                krb5_set_error_message(context, st, "%s", errbuf);
                                goto cleanup;
                            }
			    memcpy(currpos, keys[l]->key_data_contents[0], keys[l]->key_data_length[0]);
			    currpos += keys[l]->key_data_length[0];
                            rlen -= keys[l]->key_data_length[0];
          		}
	    
			salttype = keys[l]->key_data_type[1];
			if (keys[l]->key_data_length[1] && (!(salttype==KRB5_KDB_SALTTYPE_NOREALM 
							      || salttype==KRB5_KDB_SALTTYPE_ONLYREALM))) {
                            if (keys[l]->key_data_length[1] > rlen) {
                                st = EINVAL;
                                snprintf(errbuf, sizeof(errbuf), "Insufficient buffer while storing the key of principal %s", user);
                                krb5_set_error_message(context, st, "%s", errbuf);
                                goto cleanup;
                            }
			    memcpy(currpos, keys[l]->key_data_contents[1], keys[l]->key_data_length[1]);
			    currpos += keys[l]->key_data_length[1];
                            rlen -= keys[l]->key_data_length[1];
			}
			keys[l] = NULL;
		    } 
		}
		bersecretkey[kcount] = malloc (sizeof (struct berval));
		CHECK_NULL(bersecretkey[kcount]);
		bersecretkey[kcount]->bv_len = currpos - krbsecretkey;
		bersecretkey[kcount++]->bv_val = krbsecretkey;
		totalkeys = totalkeys - noofkeys;
	    }
	    if ((st=krb5_add_ber_mem_ldap_mod(&mods, "krbsecretkey", 
					      LDAP_MOD_ADD | LDAP_MOD_BVALUES, bersecretkey)) != 0)
		goto cleanup;
	} /* Modify Key data ends here */
	
	/* Directory specific attribute */
	if (xargs.tktpolicydn != NULL) {
	    int tmask=0;
	    if (strlen(xargs.tktpolicydn) != 0) {
		st = checkattributevalue(ld, xargs.tktpolicydn, "objectclass", policyclass, &tmask);
		CHECK_CLASS_VALIDITY(st, tmask, "ticket policy object value: ");

		memset(strval, 0, sizeof(strval));
		strval[0] = xargs.tktpolicydn;
		if ((st=krb5_add_str_mem_ldap_mod(&mods, "krbpolicyreference", LDAP_MOD_REPLACE, strval)) != 0)
		    goto cleanup;
	    } else {
		/* if xargs.tktpolicydn is a empty string, then delete already existing krbpolicyreference attr */
		if (tktpolicy_set == FALSE) {      /* if the attribute is not present then abort */
		    st = EINVAL;
                    krb5_set_error_message(context, st, "'ticketpolicydn' empty");
		    goto cleanup;
		} else { 
		    memset(strval, 0, sizeof(strval));
		    if ((st=krb5_add_str_mem_ldap_mod(&mods, "krbpolicyreference", LDAP_MOD_DELETE, strval)) != 0)
			goto cleanup;
		}
	    }
	}

	if (dnfound == TRUE) {
	    st=ldap_modify_s(ld, xargs.dn, mods);
            if (st != LDAP_SUCCESS) {
                sprintf(errbuf, "User modification failed: %s", ldap_err2string(st));
                st = translate_ldap_error (st, OP_MOD);
                krb5_set_error_message(context, st, "%s", errbuf);
                goto cleanup;
            }
        }
	else {
	    st=ldap_add_s(ld, xargs.dn, mods);	
	    if (st != LDAP_SUCCESS) {
	        sprintf(errbuf, "Principal add failed: %s", ldap_err2string(st));
                st = translate_ldap_error (st, OP_ADD);
                krb5_set_error_message(context, st, "%s", errbuf);
	        goto cleanup;
	    }
        }
    }
    
 cleanup:
    if (user) 
	free(user);
    
    free_xargs(xargs);

    if (subtree)
	free (subtree);
    
    if (bersecretkey) {
	for (l=0; bersecretkey[l]; ++l) {
	    if (bersecretkey[l]->bv_val)
		free (bersecretkey[l]->bv_val);
	    free (bersecretkey[l]);
	}
	free (bersecretkey);
    }
    
    if (keys)
	free (keys);
    
    if (oldkeys) {
	for (l=0; l < oldkeys->nkey; ++l) {
	    if (oldkeys->keys[l]->bv_val)
		free (oldkeys->keys[l]->bv_val);
	    free (oldkeys->keys[l]);
	}
	free (oldkeys->keys);
	free (oldkeys);
    }
    ldap_mods_free(mods, 1);
    krb5_ldap_put_handle_to_pool(ldap_context, ldap_server_handle);
    *nentries = i;
    return(st);
}

static krb5_error_code
krb5_read_tkt_policyreference(context, ldap_context, entries, policydn)
    krb5_context                context;
    krb5_ldap_context           *ldap_context;
    krb5_db_entry               *entries;
    char                        *policydn;
{
    krb5_error_code             st=0;
    unsigned int                mask=0, omask=0;
    int                         tkt_mask=(KDB_MAX_LIFE_ATTR | KDB_MAX_RLIFE_ATTR | KDB_TKT_FLAGS_ATTR);
    krb5_ldap_policy_params     *tktpoldnparam=NULL;    

    if ((st=krb5_get_attributes_mask(context, entries, &mask)) != 0)
	goto cleanup;

    if ((mask & tkt_mask) != tkt_mask) {
	if (policydn != NULL) {
	    st = krb5_ldap_read_policy(context, policydn, &tktpoldnparam, &omask);
	    if (st && st != KRB5_KDB_NOENTRY) {
                prepend_err_str(context, "Error reading ticket policy. ", st, st);
		goto cleanup;
            }
	    
	    st = 0; /* reset the return status */
	}
	
	if ((mask & KDB_MAX_LIFE_ATTR) == 0) {
	    if ((omask & KDB_MAX_LIFE_ATTR) ==  KDB_MAX_LIFE_ATTR)
		entries->max_life = tktpoldnparam->maxtktlife;
	    else if (ldap_context->lrparams->max_life)
		entries->max_life = ldap_context->lrparams->max_life;
	    else if(ldap_context->krbcontainer->max_life)
		entries->max_life = ldap_context->krbcontainer->max_life;
	    else
		entries->max_life = KRB5_KDB_MAX_LIFE; 
	}
	
	if ((mask & KDB_MAX_RLIFE_ATTR) == 0) {
	    if ((omask & KDB_MAX_RLIFE_ATTR) == KDB_MAX_RLIFE_ATTR)
		entries->max_renewable_life = tktpoldnparam->maxrenewlife;
	    else if (ldap_context->lrparams->max_renewable_life)
		entries->max_renewable_life = ldap_context->lrparams->max_renewable_life;
	    else if(ldap_context->krbcontainer->max_renewable_life)
		entries->max_renewable_life = ldap_context->krbcontainer->max_renewable_life;
	    else
		entries->max_renewable_life = KRB5_KDB_MAX_RLIFE;
	}
	
	if ((mask & KDB_TKT_FLAGS_ATTR) == 0) {
	    if ((omask & KDB_TKT_FLAGS_ATTR) == KDB_TKT_FLAGS_ATTR)
		entries->attributes = tktpoldnparam->tktflags;
	    else if (ldap_context->lrparams->tktflags)
		entries->attributes |= ldap_context->lrparams->tktflags;
	    else if(ldap_context->krbcontainer->tktflags)
		entries->attributes |= ldap_context->krbcontainer->tktflags;
	}
	krb5_ldap_free_policy(context, tktpoldnparam);
    }
    
 cleanup:
    return st;
}

static krb5_error_code
krb5_decode_krbsecretkey(context, entries, bvalues, userinfo_tl_data)
    krb5_context                context;
    krb5_db_entry               *entries;
    struct berval               **bvalues;
    krb5_tl_data                *userinfo_tl_data;
{
    char                        *user=NULL, *ptr=NULL, *pname=NULL, *currentkey=NULL, *currentsalt=NULL;
    void                        *reallocptr=NULL;
    int                         i=0, j=0, k=0, plen=0, noofkeys=0, ist_pkeyver=0, pkeyver=0, mkeyver=0, keylen=0;
    krb5_key_data               *key_data=NULL;
    krb5_error_code             st=0;
    krb5_timestamp              last_pw_changed=0;
    
    if ((st=krb5_unparse_name(context, entries->princ, &user)) != 0)
        goto cleanup;
    
    for(i=0; bvalues[i] != NULL; ++i) {

	ptr = (char *) bvalues[i]->bv_val;	      
		    
	/* check the consistency of the key */
			
	if(bvalues[i]->bv_len < KEYHEADER)  /* key smaller than the header size */
	    continue;	      
	
	plen = PRINCIPALLEN(ptr);
	if (NOOFKEYS(ptr) == 0)
	    continue;
	
	keylen = KEYHEADER + (8 * NOOFKEYS(ptr));
	if (bvalues[i]->bv_len < keylen) /* key or salt header info corrupted*/ 
	    continue;
	
	keylen += plen;
	if (bvalues[i]->bv_len < keylen) /* principal info corrupted */ 
	    continue;
		    
	for(k=0; k<NOOFKEYS(ptr); ++k)
	    keylen += KEYLENGTH(ptr, k) + SALTLENGTH(ptr, k);
		    
	if (bvalues[i]->bv_len < keylen) /* key or salt values corrupted */
	    continue;

	pname = PRINCIPALNAME(ptr);   /* set pname to principalName field */
	
	/* key doesn't belong to the principal */
	if (strncmp(user, pname, (unsigned) plen) != 0)
	    continue;

	/* Number of Principal Keys */
	noofkeys += NOOFKEYS(ptr);
		    
	if ((st=store_tl_data(userinfo_tl_data, KDB_TL_KEYINFO, bvalues[i])) != 0)
	    goto cleanup;
		    
	pkeyver = PKEYVER(ptr); 	    /* Principal Key Version */
	mkeyver = MKEYVER(ptr);		    /* Master Key Version */

	if (ist_pkeyver == 0 || pkeyver >= ist_pkeyver) {
	    ist_pkeyver = pkeyver;
	    /* last password changed */      
	    last_pw_changed = 0;
	    last_pw_changed += (ptr[6] & 0xFF) << 24;
	    last_pw_changed += (ptr[7] & 0xFF) << 16;
	    last_pw_changed += (ptr[8] & 0xFF) << 8;
	    last_pw_changed += (ptr[9] & 0xFF) << 0;
	    
	    if ((st=krb5_dbe_update_last_pwd_change(context, entries, 
						    last_pw_changed)) != 0)
		goto cleanup;
	}

	reallocptr = key_data;
	key_data = realloc(key_data, (sizeof(*key_data) * noofkeys));
	if (key_data == NULL) {
	    st = ENOMEM;
	    goto cleanup;
	}

	currentkey = KEYBODY(ptr);
	for (k=0; j<noofkeys; ++k, ++j) {
			
	    key_data[j].key_data_ver = 1; 
	    key_data[j].key_data_kvno = pkeyver;
			
	    key_data[j].key_data_type[0] = KEYTYPE(ptr,k); /* get  key type */
	    key_data[j].key_data_length[0] = KEYLENGTH(ptr,k); /* get key length */
	    key_data[j].key_data_type[1] = SALTTYPE(ptr,k); /* get salt type */
	    key_data[j].key_data_length[1] = SALTLENGTH(ptr,k); /* get salt length */
			
	    key_data[j].key_data_contents[0] = malloc(key_data[j].key_data_length[0]);
	    if (key_data[j].key_data_contents[0] == NULL) {
		st = ENOMEM;
		goto cleanup;
	    }
	    memcpy(key_data[j].key_data_contents[0], currentkey, 
		   key_data[j].key_data_length[0]);
			
	    currentsalt = currentkey + key_data[j].key_data_length[0];
	    if (key_data[j].key_data_length[1] != 0) {

		key_data[j].key_data_ver = 2;
		key_data[j].key_data_contents[1] = malloc(key_data[j].key_data_length[1]);
		if (key_data[j].key_data_contents[1] == NULL) {
		    st = ENOMEM;
		    goto cleanup;
		}
		memcpy(key_data[j].key_data_contents[1], currentsalt, 
		       key_data[j].key_data_length[1]);

	    } else if (key_data[j].key_data_type[1] == KRB5_KDB_SALTTYPE_NOREALM ||
		       key_data[j].key_data_type[1] == KRB5_KDB_SALTTYPE_ONLYREALM) {
		char *def_realm = NULL;
		krb5_data norealmval;
			    
		key_data[j].key_data_ver = 2;		    
		switch(key_data[j].key_data_type[1]) {
		case KRB5_KDB_SALTTYPE_ONLYREALM:
		    def_realm = entries->princ->realm.data;
		    key_data[j].key_data_length[1] = strlen (def_realm);
		    key_data[j].key_data_contents[1] = malloc (key_data[j].key_data_length[1]);
		    if (key_data[j].key_data_contents[1] == NULL) {
			st = ENOMEM;
			goto cleanup;
		    }
		    memcpy(key_data[j].key_data_contents[1], 
			   def_realm, key_data[j].key_data_length[1]);
		    break;
		      
		case KRB5_KDB_SALTTYPE_NOREALM:
		    memset(&norealmval, 0, sizeof(krb5_data));
		    if ((st = krb5_principal2salt_norealm(context, entries->princ, 
							  &norealmval)) != 0) {
			goto cleanup;
		    }

		    key_data[j].key_data_length[1] = norealmval.length;
		    key_data[j].key_data_contents[1] = (unsigned char *)norealmval.data;
		    break;
		}
	    } else if (key_data[j].key_data_type[1] == KRB5_KDB_SALTTYPE_V4) {
		key_data[j].key_data_contents[1] = NULL;
		key_data[j].key_data_ver = 2;		    
	    } else {
		key_data[j].key_data_contents[1] = NULL;
	    }
	    currentkey = currentsalt + key_data[j].key_data_length[1];
	}
	entries->n_key_data = noofkeys;
	entries->key_data = key_data;
    }

 cleanup:
    ldap_value_free_len(bvalues);	    
    free (user);
    return st;
}

static char *
getstringtime(epochtime)
    krb5_timestamp    epochtime;
{
    struct tm           tme;
    char                *strtime=NULL;
    
    strtime = calloc (50, 1);
    if (strtime == NULL)
	return NULL;
    
    if (gmtime_r((const time_t *)&epochtime, &tme) == NULL)
	return NULL;
    
    strftime(strtime, 50, "%Y%m%d%H%M%SZ", &tme);
    return strtime;
}

