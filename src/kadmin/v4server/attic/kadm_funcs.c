/*
 * $Source$
 * $Author$
 *
 * Copyright 1988 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 *
 * Kerberos administration server-side database manipulation routines
 */

#ifndef	lint
static char rcsid_kadm_funcs_c[] =
"$Id$";
#endif	lint

#include <mit-copyright.h>
/*
kadm_funcs.c
the actual database manipulation code
*/

#include <stdio.h>
#include <sys/param.h>
#include <ndbm.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/file.h>
#include <kadm.h>
#include <kadm_err.h>
#include <krb_db.h>
#include <syslog.h>
#ifdef NEED_SYS_FCNTL_H
#include <sys/fcntl.h>
#endif

#include "kadm_server.h"

extern Kadm_Server server_parm;

krb5_error_code
kadm_entry2princ(entry, princ)
     krb5_db_entry entry;
     Principal *princ;
{
  char realm[REALM_SZ];		/* dummy values only */
  krb5_error_code retval;
  time_t lcltim;

  /* NOTE: does not convert the key */
  memset(princ, 0, sizeof (*princ));
  retval = krb5_524_conv_principal(entry.principal,
				   princ->name, princ->instance, realm);
  if (retval)
    return retval;
  princ->exp_date = entry.expiration;
  lcltim = entry.expiration;
  strncpy(princ->exp_date_txt, ctime(&lcltim),
	  DATE_SZ);
  lcltim = princ->mod_date = entry.mod_date;
  strncpy(princ->mod_date_txt, ctime(&lcltim),
	  DATE_SZ);
  princ->attributes = entry.attributes;
  princ->max_life = entry.max_life;
  princ->kdc_key_ver = entry.mkvno;
  princ->key_version = entry.kvno;
  retval = krb5_524_conv_principal(entry.mod_name,
				   princ->mod_name, princ->mod_instance,
				   realm);
  if (retval)
    return retval;
  return 0;
}

krb5_error_code
kadm_princ2entry(princ, entry)
     Principal princ;
     krb5_db_entry *entry;
{
  krb5_error_code retval;

  /* NOTE: does not convert the key */
  memset(entry, 0, sizeof (*entry));
  /* yeah yeah stupid v4 database doesn't store realm names */
  retval = krb5_425_conv_principal(princ.name, princ.instance,
				   server_parm.krbrlm, &entry->principal);
  if (retval)
    return retval;
  entry->kvno = princ.key_version;
  entry->max_life = princ.max_life;
  entry->max_renewable_life = server_parm.max_rlife; /* XXX yeah well */
  entry->mkvno = server_parm.mkvno; /* XXX */
  entry->expiration = princ.exp_date;
  retval = krb5_425_conv_principal(princ.mod_name, princ.mod_instance,
				   server_parm.krbrlm, &entry->mod_name);
  if (retval)
    return retval;
  entry->mod_date = princ.mod_date;
  entry->attributes = princ.attributes;
  entry->salt_type = KRB5_KDB_SALTTYPE_V4;
}

check_access(pname, pinst, prealm, acltype)
char *pname;
char *pinst;
char *prealm;
enum acl_types acltype;
{
    char checkname[MAX_K_NAME_SZ];
    char filename[MAXPATHLEN];
    extern char *acldir;

    (void) sprintf(checkname, "%s.%s@%s", pname, pinst, prealm);
    
    switch (acltype) {
    case ADDACL:
	(void) sprintf(filename, "%s%s", acldir, ADD_ACL_FILE);
	break;
    case GETACL:
	(void) sprintf(filename, "%s%s", acldir, GET_ACL_FILE);
	break;
    case MODACL:
	(void) sprintf(filename, "%s%s", acldir, MOD_ACL_FILE);
	break;
    case STABACL:
	(void) sprintf(filename, "%s%s", acldir, STAB_ACL_FILE);
	break;
    }
    return(acl_check(filename, checkname));
}

int
wildcard(str)
char *str;
{
    if (!strcmp(str, WILDCARD_STR))
	return(1);
    return(0);
}

#define failadd(code) {  (void) syslog(LOG_ERR, "FAILED addding '%s.%s' (%s)", valsin->name, valsin->instance, error_message(code)); return code; }

kadm_add_entry (rname, rinstance, rrealm, valsin, valsout)
char *rname;				/* requestors name */
char *rinstance;			/* requestors instance */
char *rrealm;				/* requestors realm */
Kadm_vals *valsin;
Kadm_vals *valsout;
{
  Principal data_i, data_o;		/* temporary principal */
  u_char flags[4];
  krb5_principal default_princ;
  krb5_error_code retval;
  krb5_db_entry newentry, tmpentry;
  krb5_boolean more;
  krb5_keyblock newpw;
  krb5_encrypted_keyblock encpw;
  int numfound;

  if (!check_access(rname, rinstance, rrealm, ADDACL)) {
    syslog(LOG_WARNING, "WARNING: '%s.%s@%s' tried to add an entry for '%s.%s'",
	   rname, rinstance, rrealm, valsin->name, valsin->instance);
    return KADM_UNAUTH;
  }

  /* Need to check here for "legal" name and instance */
  if (wildcard(valsin->name) || wildcard(valsin->instance)) {
      failadd(KADM_ILL_WILDCARD);
  }

  syslog(LOG_INFO, "request to add an entry for '%s.%s' from '%s.%s@%s'",
	 valsin->name, valsin->instance, rname, rinstance, rrealm);

  kadm_vals_to_prin(valsin->fields, &data_i, valsin);
  (void) strncpy(data_i.name, valsin->name, ANAME_SZ);
  (void) strncpy(data_i.instance, valsin->instance, INST_SZ);

  if (!IS_FIELD(KADM_EXPDATE,valsin->fields))
    data_i.exp_date = server_parm.expiration;
  if (!IS_FIELD(KADM_ATTR,valsin->fields))
    data_i.attributes = server_parm.flags;
  if (!IS_FIELD(KADM_MAXLIFE,valsin->fields))
    data_i.max_life = server_parm.max_life;

  if ((newpw.contents = (krb5_octet *)malloc(8)) == NULL)
    failadd(KADM_NOMEM);
  data_i.key_low = ntohl(data_i.key_low);
  data_i.key_high = ntohl(data_i.key_high);
  memcpy(newpw.contents, &data_i.key_low, 4);
  memcpy((char *)(((krb4_int32 *) newpw.contents) + 1), &data_i.key_high, 4);
  newpw.length = 8;
  newpw.keytype = KEYTYPE_DES;
  /* encrypt new key in master key */
  retval = krb5_kdb_encrypt_key(&server_parm.master_encblock,
				&newpw, &encpw);
  memset((char *)newpw.contents, 0, newpw.length);
  free(newpw.contents);
  if (retval) {
    failadd(retval);
  }
  data_o = data_i;

  retval = kadm_princ2entry(data_i, &newentry);
  if (retval) {
    memset((char *)encpw.contents, 0, encpw.length);
    free(encpw.contents);
    krb5_db_free_principal(&newentry, 1);
    failadd(retval);
  }

  newentry.key = encpw;
  numfound = 1;
  retval = krb5_db_get_principal(newentry.principal,
				 &tmpentry, &numfound, &more);

  if (retval) {
    krb5_db_free_principal(&newentry, 1);
    failadd(retval);
  }
  krb5_db_free_principal(&tmpentry, numfound);
  if (numfound) {
    krb5_db_free_principal(&newentry, 1);
    failadd(KADM_INUSE);
  } else {
    newentry.kvno = ++data_i.key_version;
    if (retval = krb5_timeofday(&newentry.mod_date)) {
	krb5_db_free_principal(&newentry, 1);
	failadd(retval);
    }
    if (newentry.mod_name)
      krb5_free_principal(newentry.mod_name);
    newentry.mod_name = NULL;	/* in case the following breaks */
    retval = krb5_425_conv_principal(rname, rinstance, rrealm,
				     &newentry.mod_name);
    if (retval) {
      krb5_db_free_principal(&newentry, 1);
      failadd(retval);
    }

    numfound = 1;
    retval = krb5_db_put_principal(&newentry, &numfound);
    if (retval) {
      krb5_db_free_principal(&newentry, 1);
      failadd(retval);
    }
    if (!numfound) {
      krb5_db_free_principal(&newentry, 1);
      failadd(KADM_UK_SERROR);
    } else {
      numfound = 1;
      retval = krb5_db_get_principal(newentry.principal,
				     &tmpentry,
				     &numfound, &more);
      krb5_db_free_principal(&newentry, 1);
      if (retval) {
	failadd(retval);
      } else if (numfound != 1 || more) {
	krb5_db_free_principal(&tmpentry, numfound);
	failadd(KADM_UK_RERROR);
      }
      kadm_entry2princ(tmpentry, &data_o);
      krb5_db_free_principal(&tmpentry, numfound);
      memset((char *)flags, 0, sizeof(flags));
      SET_FIELD(KADM_NAME,flags);
      SET_FIELD(KADM_INST,flags);
      SET_FIELD(KADM_EXPDATE,flags);
      SET_FIELD(KADM_ATTR,flags);
      SET_FIELD(KADM_MAXLIFE,flags);
      kadm_prin_to_vals(flags, valsout, &data_o);
      syslog(LOG_INFO, "'%s.%s' added.", valsin->name, valsin->instance);
      return KADM_DATA;		/* Set all the appropriate fields */
    }
  }
}
#undef failadd

#define failget(code) {  (void) syslog(LOG_ERR, "FAILED retrieving '%s.%s' (%s)", valsin->name, valsin->instance, error_message(code)); return code; }

kadm_get_entry (rname, rinstance, rrealm, valsin, flags, valsout)
char *rname;				/* requestors name */
char *rinstance;			/* requestors instance */
char *rrealm;				/* requestors realm */
Kadm_vals *valsin;			/* what they wannt to get */
u_char *flags;				/* which fields we want */
Kadm_vals *valsout;			/* what data is there */
{
  int numfound;			/* check how many were returned */
  krb5_boolean more;		/* To point to more name.instances */
  Principal data_o;		/* Data object to hold Principal */
  krb5_principal inprinc;
  krb5_db_entry entry;
  krb5_error_code retval;

  if (!check_access(rname, rinstance, rrealm, GETACL)) {
    syslog(LOG_WARNING, "WARNING: '%s.%s@%s' tried to get '%s.%s's entry",
	    rname, rinstance, rrealm, valsin->name, valsin->instance);
    return KADM_UNAUTH;
  }

  if (wildcard(valsin->name) || wildcard(valsin->instance)) {
      failget(KADM_ILL_WILDCARD);
  }

  syslog(LOG_INFO, "retrieve '%s.%s's entry for '%s.%s@%s'",
	     valsin->name, valsin->instance, rname, rinstance, rrealm);

  retval = krb5_425_conv_principal(valsin->name, valsin->instance,
				   server_parm.krbrlm, &inprinc);
  if (retval)
    failget(retval);
  /* Look up the record in the database */
  numfound = 1;
  retval = krb5_db_get_principal(inprinc, &entry, &numfound, &more);
  krb5_free_principal(inprinc);
  if (retval) {
    failget(retval);
  } else if (!numfound || more) {
    failget(KADM_NOENTRY);
  }
  retval = kadm_entry2princ(entry, &data_o);
  krb5_db_free_principal(&entry, 1);
  if (retval) {
    failget(retval);
  }
  kadm_prin_to_vals(flags, valsout, &data_o);
  syslog(LOG_INFO, "'%s.%s' retrieved.", valsin->name, valsin->instance);
  return KADM_DATA;		/* Set all the appropriate fields */
}
#undef failget

#define failmod(code) {  (void) syslog(LOG_ERR, "FAILED modifying '%s.%s' (%s)", valsin1->name, valsin1->instance, error_message(code)); return code; }

kadm_mod_entry (rname, rinstance, rrealm, valsin1, valsin2, valsout)
char *rname;				/* requestors name */
char *rinstance;			/* requestors instance */
char *rrealm;				/* requestors realm */
Kadm_vals *valsin1, *valsin2;		/* holds the parameters being
					   passed in */
Kadm_vals *valsout;		/* the actual record which is returned */
{
  int numfound;
  krb5_boolean more;
  Principal data_o, temp_key;
  u_char fields[4];
  krb5_keyblock newpw;
  krb5_encrypted_keyblock encpw;
  krb5_error_code retval;
  krb5_principal theprinc, rprinc;
  krb5_db_entry newentry, odata;

  if (wildcard(valsin1->name) || wildcard(valsin1->instance)) {
      failmod(KADM_ILL_WILDCARD);
  }

  if (!check_access(rname, rinstance, rrealm, MODACL)) {
    syslog(LOG_WARNING, "WARNING: '%s.%s@%s' tried to change '%s.%s's entry",
	       rname, rinstance, rrealm, valsin1->name, valsin1->instance);
    return KADM_UNAUTH;
  }

  syslog(LOG_INFO, "request to modify '%s.%s's entry from '%s.%s@%s' ",
	     valsin1->name, valsin1->instance, rname, rinstance, rrealm);
  krb5_425_conv_principal(valsin1->name, valsin1->instance,
			  server_parm.krbrlm, &theprinc);
  if (retval)
    failmod(retval);
  numfound = 1;
  retval = krb5_db_get_principal(theprinc, &newentry, &numfound, &more);
  if (retval) {
    krb5_free_principal(theprinc);
    failmod(retval);
  } else if (numfound == 1) {
    kadm_vals_to_prin(valsin2->fields, &temp_key, valsin2);
    krb5_free_principal(newentry.principal);
    newentry.principal = theprinc;
    if (IS_FIELD(KADM_EXPDATE,valsin2->fields))
      newentry.expiration = temp_key.exp_date;
    if (IS_FIELD(KADM_ATTR,valsin2->fields))
      newentry.attributes = temp_key.attributes;
    if (IS_FIELD(KADM_MAXLIFE,valsin2->fields))
      newentry.max_life = temp_key.max_life; 
    if (IS_FIELD(KADM_DESKEY,valsin2->fields)) {
      newentry.kvno++;
      newentry.mkvno = server_parm.mkvno;
      if ((newpw.contents = (krb5_octet *)malloc(8)) == NULL) {
	krb5_db_free_principal(&newentry, 1);
	memset((char *)&temp_key, 0, sizeof (temp_key));
	failmod(KADM_NOMEM);
      }
      newpw.length = 8;
      newpw.keytype = KEYTYPE_DES;
      temp_key.key_low = ntohl(temp_key.key_low);
      temp_key.key_high = ntohl(temp_key.key_high);
      memcpy(newpw.contents, &temp_key.key_low, 4);
      memcpy(newpw.contents + 4, &temp_key.key_high, 4);
      /* encrypt new key in master key */
      retval = krb5_kdb_encrypt_key(&server_parm.master_encblock,
				    &newpw, &encpw);
      memset(newpw.contents, 0, newpw.length);
      free(newpw.contents);
      memset((char *)&temp_key, 0, sizeof(temp_key));
      if (retval) {
	krb5_db_free_principal(&newentry, 1);
	failmod(retval);
      }
      if (newentry.key.contents) {
	memset((char *)newentry.key.contents, 0, newentry.key.length);
	free(newentry.key.contents);
      }
      newentry.key = encpw;
    }
    if (retval = krb5_timeofday(&newentry.mod_date)) {
	krb5_db_free_principal(&newentry, 1);
	failmod(retval);
    }
    retval = krb5_425_conv_principal(rname, rinstance, rrealm,
				     &newentry.mod_name);
    if (retval) {
      krb5_db_free_principal(&newentry, 1);
      failmod(retval);
    }
    numfound = 1;
    retval = krb5_db_put_principal(&newentry, &numfound);
    memset((char *)&data_o, 0, sizeof(data_o));
    if (retval) {
      krb5_db_free_principal(&newentry, 1);
      failmod(retval);
    } else {
      numfound = 1;
      retval = krb5_db_get_principal(newentry.principal, &odata,
				     &numfound, &more);
      krb5_db_free_principal(&newentry, 1);
      if (retval) {
	failmod(retval);
      } else if (numfound != 1 || more) {
	krb5_db_free_principal(&odata, numfound);
	failmod(KADM_UK_RERROR);
      }
      retval = kadm_entry2princ(odata, &data_o);
      krb5_db_free_principal(&odata, 1);
      if (retval)
	failmod(retval);
      memset((char *) fields, 0, sizeof(fields));
      SET_FIELD(KADM_NAME,fields);
      SET_FIELD(KADM_INST,fields);
      SET_FIELD(KADM_EXPDATE,fields);
      SET_FIELD(KADM_ATTR,fields);
      SET_FIELD(KADM_MAXLIFE,fields);
      kadm_prin_to_vals(fields, valsout, &data_o);
      syslog(LOG_INFO, "'%s.%s' modified.", valsin1->name, valsin1->instance);
      return KADM_DATA;		/* Set all the appropriate fields */
    }
  } else {
    failmod(KADM_NOENTRY);
  }
}
#undef failmod

#define failchange(code) {  syslog(LOG_ERR, "FAILED changing key for '%s.%s@%s' (%s)", rname, rinstance, rrealm, error_message(code)); return code; }

kadm_change (rname, rinstance, rrealm, newpw)
char *rname;
char *rinstance;
char *rrealm;
des_cblock newpw;
{
  int numfound;
  krb5_boolean more;
  krb5_principal rprinc;
  krb5_error_code retval;
  krb5_keyblock localpw;
  krb5_encrypted_keyblock encpw;
  krb5_db_entry odata;

  if (strcmp(server_parm.krbrlm, rrealm)) {
      syslog(LOG_ERR, "change key request from wrong realm, '%s.%s@%s'!\n",
		 rname, rinstance, rrealm);
      return(KADM_WRONG_REALM);
  }

  if (wildcard(rname) || wildcard(rinstance)) {
      failchange(KADM_ILL_WILDCARD);
  }
  syslog(LOG_INFO, "'%s.%s@%s' wants to change its password",
	     rname, rinstance, rrealm);
  retval = krb5_425_conv_principal(rname, rinstance,
				   server_parm.krbrlm, &rprinc);
  if (retval)
    failchange(retval);
  if ((localpw.contents = (krb5_octet *)malloc(8)) == NULL)
    failchange(KADM_NOMEM);
  memcpy(localpw.contents, newpw, 8);
  localpw.keytype = KEYTYPE_DES;
  localpw.length = 8;
  /* encrypt new key in master key */
  retval = krb5_kdb_encrypt_key(&server_parm.master_encblock,
				&localpw, &encpw);
  memset((char *)localpw.contents, 0, 8);
  free(localpw.contents);
  if (retval) {
    krb5_free_principal(rprinc);
    failchange(retval);
  }
  numfound = 1;
  retval = krb5_db_get_principal(rprinc, &odata, &numfound, &more);
  krb5_free_principal(rprinc);
  if (retval) {
    failchange(retval);
  } else if (numfound == 1) {
    odata.key = encpw;
    odata.kvno++;
    odata.mkvno = server_parm.mkvno;
    if (retval = krb5_timeofday(&odata.mod_date)) {
	krb5_db_free_principal(&odata, 1);
	failchange(retval);
    }
    krb5_425_conv_principal(rname, rinstance,
			    server_parm.krbrlm, &odata.mod_name);
    if (retval) {
      krb5_db_free_principal(&odata, 1);
      failchange(retval);
    }
    numfound = 1;
    retval = krb5_db_put_principal(&odata, &numfound);
    krb5_db_free_principal(&odata, 1);
    if (retval) {
      failchange(retval);
    } else if (more) {
      failchange(KADM_UK_SERROR);
    } else {
      syslog(LOG_INFO,
	     "'%s.%s@%s' password changed.", rname, rinstance, rrealm);
      return KADM_SUCCESS;
    }
  }
  else {
    failchange(KADM_NOENTRY);
  }
}
#undef failchange

check_pw(newpw, checkstr)
	des_cblock	newpw;
	char		*checkstr;
{
#ifdef NOENCRYPTION
	return 0;
#else /* !NOENCRYPTION */
	des_cblock	checkdes;

	(void) des_string_to_key(checkstr, checkdes);
	return(!memcmp(checkdes, newpw, sizeof(des_cblock)));
#endif /* NOENCRYPTION */
}

char *reverse(str)
	char	*str;
{
	static char newstr[80];
	char	*p, *q;
	int	i;

	i = strlen(str);
	if (i >= sizeof(newstr))
		i = sizeof(newstr)-1;
	p = str+i-1;
	q = newstr;
	q[i]='\0';
	for(; i > 0; i--) 
		*q++ = *p--;
	
	return(newstr);
}

int lower(str)
	char	*str;
{
	register char	*cp;
	int	effect=0;

	for (cp = str; *cp; cp++) {
		if (isupper(*cp)) {
			*cp = tolower(*cp);
			effect++;
		}
	}
	return(effect);
}

des_check_gecos(gecos, newpw)
	char	*gecos;
	des_cblock newpw;
{
	char		*cp, *ncp, *tcp;
	
	for (cp = gecos; *cp; ) {
		/* Skip past punctuation */
		for (; *cp; cp++)
			if (isalnum(*cp))
				break;
		/* Skip to the end of the word */
		for (ncp = cp; *ncp; ncp++)
			if (!isalnum(*ncp) && *ncp != '\'')
				break;
		/* Delimit end of word */
		if (*ncp)
			*ncp++ = '\0';
		/* Check word to see if it's the password */
		if (*cp) {
			if (check_pw(newpw, cp))
				return(KADM_INSECURE_PW);
			tcp = reverse(cp);
			if (check_pw(newpw, tcp))
				return(KADM_INSECURE_PW);
			if (lower(cp)) {
				if (check_pw(newpw, cp))
					return(KADM_INSECURE_PW);
				tcp = reverse(cp);
				if (check_pw(newpw, tcp))
					return(KADM_INSECURE_PW);
			}
			cp = ncp;				
		} else
			break;
	}
	return(0);
}

str_check_gecos(gecos, pwstr)
	char	*gecos;
	char	*pwstr;
{
	char		*cp, *ncp, *tcp;
	
	for (cp = gecos; *cp; ) {
		/* Skip past punctuation */
		for (; *cp; cp++)
			if (isalnum(*cp))
				break;
		/* Skip to the end of the word */
		for (ncp = cp; *ncp; ncp++)
			if (!isalnum(*ncp) && *ncp != '\'')
				break;
		/* Delimit end of word */
		if (*ncp)
			*ncp++ = '\0';
		/* Check word to see if it's the password */
		if (*cp) {
			if (!strcasecmp(pwstr, cp))
				return(KADM_INSECURE_PW);
			tcp = reverse(cp);
			if (!strcasecmp(pwstr, tcp))
				return(KADM_INSECURE_PW);
			cp = ncp;				
		} else
			break;
	}
	return(0);
}


kadm_approve_pw(rname, rinstance, rrealm, newpw, pwstring)
char *rname;
char *rinstance;
char *rrealm;
des_cblock newpw;
char *pwstring;
{
	static DBM *pwfile = NULL;
	int		retval;
	datum		passwd, entry;
	struct passwd	*ent;
#ifdef HESIOD
	extern struct passwd *hes_getpwnam();
#endif
	
	if (pwstring && !check_pw(newpw, pwstring))
		/*
		 * Someone's trying to toy with us....
		 */
		return(KADM_PW_MISMATCH);
	if (pwstring && (strlen(pwstring) < 5))
		return(KADM_INSECURE_PW);
	if (!pwfile) {
		pwfile = dbm_open(PW_CHECK_FILE, O_RDONLY, 0644);
	}
	if (pwfile) {
		passwd.dptr = (char *) newpw;
		passwd.dsize = 8;
		entry = dbm_fetch(pwfile, passwd);
		if (entry.dptr)
			return(KADM_INSECURE_PW);
	}
	if (check_pw(newpw, rname) || check_pw(newpw, reverse(rname)))
		return(KADM_INSECURE_PW);
#ifdef HESIOD
	ent = hes_getpwnam(rname);
#else
	ent = getpwnam(rname);
#endif
	if (ent && ent->pw_gecos) {
		if (pwstring)
			retval = str_check_gecos(ent->pw_gecos, pwstring);
		else
			retval = des_check_gecos(ent->pw_gecos, newpw);
		if (retval)
			return(retval);
	}
	return(0);
}

/*
 * This routine checks to see if a principal should be considered an
 * allowable service name which can be changed by kadm_change_srvtab.
 *
 * We do this check by using the ACL library.  This makes the
 * (relatively) reasonable assumption that both the name and the
 * instance will  not contain '.' or '@'. 
 */
kadm_check_srvtab(name, instance)
	char	*name;
	char	*instance;
{
	FILE	*f;
	char filename[MAXPATHLEN];
	char buf[ANAME_SZ], *cp;
	extern char *acldir;

	(void) sprintf(filename, "%s%s", acldir, STAB_SERVICES_FILE);
	if (!acl_check(filename, name))
		return(KADM_NOT_SERV_PRINC);

	(void) sprintf(filename, "%s%s", acldir, STAB_HOSTS_FILE);
	if (acl_check(filename, instance))
		return(KADM_NOT_SERV_PRINC);
	return 0;
}

/*
 * Routine to allow some people to change the key of a srvtab
 * principal to a random key, which the admin server will return to
 * the client.
 */
#define failsrvtab(code) {  syslog(LOG_ERR, "change_srvtab: FAILED changing '%s.%s' by '%s.%s@%s' (%s)", values->name, values->instance, rname, rinstance, rrealm, error_message(code)); return code; }

kadm_chg_srvtab(rname, rinstance, rrealm, values)
     char *rname;		/* requestors name */
     char *rinstance;		/* requestors instance */
     char *rrealm;		/* requestors realm */
     Kadm_vals *values;
{
  int numfound, ret, isnew = 0;
  des_cblock new_key;
  Principal principal;
  krb5_principal inprinc;
  krb5_error_code retval;
  krb5_db_entry odata;
  krb5_boolean more;
  krb5_keyblock newpw;
  krb5_encrypted_keyblock encpw;

  if (!check_access(rname, rinstance, rrealm, STABACL))
    failsrvtab(KADM_UNAUTH);
  if (wildcard(rname) || wildcard(rinstance))
    failsrvtab(KADM_ILL_WILDCARD);
  if (ret = kadm_check_srvtab(values->name, values->instance))
    failsrvtab(ret);

  retval = krb5_425_conv_principal(values->name, values->instance,
				   server_parm.krbrlm, &inprinc);
  if (retval)
    failsrvtab(retval);
  /*
   * OK, get the entry
   */
  numfound = 1;
  retval = krb5_db_get_principal(inprinc, &odata, &numfound, &more);
  if (retval) {
    krb5_free_principal(inprinc);
    failsrvtab(retval);
  } else if (numfound) {
    odata.kvno++;
  } else {
    /*
     * This is a new srvtab entry that we're creating
     */
    isnew = 1;
    memset((char *)&odata, 0, sizeof (odata));
    odata.principal = inprinc;
    odata.kvno = 1;
    odata.max_life = server_parm.max_life;
    odata.max_renewable_life = server_parm.max_rlife;
    odata.mkvno = server_parm.mkvno;
    odata.expiration = server_parm.expiration;
    odata.attributes = 0;
  }

#ifdef NOENCRYPTION
  memset(new_key, 0, sizeof(new_key));
  new_key[0] = 127;
#else
  des_new_random_key(new_key);
#endif
  /*
   * Store the new key in the return structure; also fill in the
   * rest of the fields.
   */
  if ((newpw.contents = (krb5_octet *)malloc(8)) == NULL) {
    krb5_db_free_principal(&odata, 1);
    failsrvtab(KADM_NOMEM);
  }
  newpw.keytype = KEYTYPE_DES;
  newpw.length = 8;
  memcpy((char *)newpw.contents, new_key, 8);
  memset((char *)new_key, 0, sizeof (new_key));
  memcpy((char *)&values->key_low, newpw.contents, 4);
  memcpy((char *)&values->key_high, newpw.contents + 4, 4);
  values->key_low = htonl(values->key_low);
  values->key_high = htonl(values->key_high);
  values->max_life = odata.kvno;
  values->exp_date = odata.expiration;
  values->attributes = odata.attributes;
  memset(values->fields, 0, sizeof(values->fields));
  SET_FIELD(KADM_NAME, values->fields);
  SET_FIELD(KADM_INST, values->fields);
  SET_FIELD(KADM_EXPDATE, values->fields);
  SET_FIELD(KADM_ATTR, values->fields);
  SET_FIELD(KADM_MAXLIFE, values->fields);
  SET_FIELD(KADM_DESKEY, values->fields);

  /*
   * Encrypt the new key with the master key, and then update
   * the database record
   */
  retval = krb5_kdb_encrypt_key(&server_parm.master_encblock,
				&newpw, &encpw);
  memset((char *)newpw.contents, 0, 8);
  free(newpw.contents);
  if (odata.key.contents) {
    memset((char *)odata.key.contents, 0, odata.key.length);
    free(odata.key.contents);
  }
  odata.key = encpw;
  if (retval) {
    krb5_db_free_principal(&odata, 1);
    failsrvtab(retval);
  }
  if (retval = krb5_timeofday(&odata.mod_date)) {
      krb5_db_free_principal(&odata, 1);
      failsrvtab(retval);
  }
  retval = krb5_425_conv_principal(rname, rinstance,
				   server_parm.krbrlm, &odata.mod_name);
  if (retval) {
    krb5_db_free_principal(&odata, 1);
    failsrvtab(retval);
  }
  numfound = 1;
  retval = krb5_db_put_principal(&odata, &numfound);
  krb5_db_free_principal(&odata, 1);
  if (retval) {
    failsrvtab(retval);
  }
  else if (!numfound) {
    failsrvtab(KADM_UK_SERROR);
  } else {
    syslog(LOG_INFO, "change_srvtab: service '%s.%s' %s by %s.%s@%s.",
	   values->name, values->instance,
	   numfound ? "changed" : "created",
	   rname, rinstance, rrealm);
    return KADM_DATA;
  }
}

#undef failsrvtab
