/*
 * admin/edit/loadv4.c
 *
 * Copyright 1990,1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
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
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * Generate (from scratch) a Kerberos V5 KDC database, filling it in with the
 * entries from a V4 database.
 */

#ifdef KRB5_KRB4_COMPAT

#include <des.h>
#include <krb.h>
#include <krb_db.h>
/* MKEYFILE is now defined in kdc.h */
#include <kdc.h>

static C_Block master_key;
static Key_schedule master_key_schedule;
static long master_key_version;

static char *v4_mkeyfile = "/.k";

#include "k5-int.h"
#include "com_err.h"
#include "adm.h"
#include "adm_proto.h"
#include <stdio.h>

#include <netinet/in.h>			/* ntohl */

#define PROGNAME argv[0]

enum ap_op {
    NULL_KEY,				/* setup null keys */
    MASTER_KEY,				/* use master key as new key */
    RANDOM_KEY				/* choose a random key */
};

struct realm_info {
    krb5_deltat max_life;
    krb5_deltat max_rlife;
    krb5_timestamp expiration;
    krb5_flags flags;
    krb5_encrypt_block *eblock;
    krb5_pointer rseed;
};

static struct realm_info rblock = { /* XXX */
    KRB5_KDB_MAX_LIFE,
    KRB5_KDB_MAX_RLIFE,
    KRB5_KDB_EXPIRATION,
    KRB5_KDB_DEF_FLAGS,
    0
};

static int verbose = 0;

static krb5_error_code add_principal 
	PROTOTYPE((krb5_context,
		   krb5_principal, 
		   enum ap_op,
		   struct realm_info *));

static int v4init PROTOTYPE((char *, char *, int, char *));
static krb5_error_code enter_in_v5_db PROTOTYPE((krb5_context,
						 char *, Principal *));
static krb5_error_code process_v4_dump PROTOTYPE((krb5_context, char *,
						  char *));
static krb5_error_code fixup_database PROTOTYPE((krb5_context, char *));
	
static int create_local_tgt = 0;

static void
usage(who, status)
char *who;
int status;
{
    fprintf(stderr, "usage: %s [-d v5dbpathname] [-t] [-n] [-r realmname] [-K] [-k enctype]\n\
\t[-M mkeyname] -f inputfile\n",
	    who);
    return;
}

static krb5_keyblock master_keyblock;
static krb5_principal master_princ;
static krb5_encrypt_block master_encblock;

static krb5_data tgt_princ_entries[] = {
	{0, KRB5_TGS_NAME_SIZE, KRB5_TGS_NAME},
	{0, 0, 0} };

static krb5_data db_creator_entries[] = {
	{0, sizeof("db_creation")-1, "db_creation"} };

/* XXX knows about contents of krb5_principal, and that tgt names
 are of form TGT/REALM@REALM */
static krb5_principal_data tgt_princ = {
        0,					/* magic number */
	{0, 0, 0},				/* krb5_data realm */
	tgt_princ_entries,			/* krb5_data *data */
	2,					/* int length */
	KRB5_NT_SRV_INST			/* int type */
};

static krb5_principal_data db_create_princ = {
        0,					/* magic number */
	{0, 0, 0},				/* krb5_data realm */
	db_creator_entries,			/* krb5_data *data */
	1,					/* int length */
	KRB5_NT_SRV_INST			/* int type */
};


void
load_v4db(argc, argv)
int argc;
char *argv[];
{
    krb5_error_code retval;
    /* The kdb library will default to this, but it is convenient to
       make it explicit (error reporting and temporary filename generation
       use it).  */
    char *dbname = DEFAULT_KDB_FILE;
    char *v4dbname = 0;
    char *v4dumpfile = 0;
    char *realm = 0;
    char *mkey_name = 0;
    char *mkey_fullname;
    char *defrealm;
    int enctypedone = 0;
    int v4manual = 0;
    int read_mkey = 0;
    int tempdb = 0;
    char *tempdbname;
    krb5_context context;
    char *stash_file = (char *) NULL;
    krb5_realm_params *rparams;
    int	persist, op_ind;

    krb5_init_context(&context);

    krb5_init_ets(context);

    if (strrchr(argv[0], '/'))
	argv[0] = strrchr(argv[0], '/')+1;

    persist = 1;
    op_ind = 1;
    while (persist && (op_ind < argc)) {
	if (!strcmp(argv[op_ind], "-d") && ((argc - op_ind) >= 2)) {
	    dbname = argv[op_ind+1];
	    op_ind++;
	}
	else if (!strcmp(argv[op_ind], "-T")) {
	    create_local_tgt = 1;
	}
	else if (!strcmp(argv[op_ind], "-t")) {
	    tempdb = 1;
	}
	else if (!strcmp(argv[op_ind], "-r") && ((argc - op_ind) >= 2)) {
	    realm = argv[op_ind+1];
	    op_ind++;
	}
	else if (!strcmp(argv[op_ind], "-K")) {
	    read_mkey = 1;
	}
	else if (!strcmp(argv[op_ind], "-v")) {
	    verbose = 1;
	}
	else if (!strcmp(argv[op_ind], "-k") && ((argc - op_ind) >= 2)) {
	    if (!krb5_string_to_enctype(argv[op_ind+1],
					&master_keyblock.enctype))
		enctypedone++;
	    else
		com_err(argv[0], 0, "%s is an invalid enctype",
			argv[op_ind+1]);
	    op_ind++;
	}
	else if (!strcmp(argv[op_ind], "-M") && ((argc - op_ind) >= 2)) {
	    mkey_name = argv[op_ind+1];
	    op_ind++;
	}
	else if (!strcmp(argv[op_ind], "-n")) {
	    v4manual++;
	}
	else if (!strcmp(argv[op_ind], "-f") && ((argc - op_ind) >= 2)) {
	    if (v4dbname) {
		usage(PROGNAME, 1);
		return;
	    }
	    v4dumpfile = argv[op_ind+1];
	    op_ind++;
	}
	else
	    persist = 0;
	op_ind++;
    }

    /*
     * Attempt to read the KDC profile.  If we do, then read appropriate values
     * from it and augment values supplied on the command line.
     */
    if (!(retval = krb5_read_realm_params(context,
					  realm,
					  (char *) NULL,
					  (char *) NULL,
					  &rparams))) {
	/* Get the value for the database */
	if (rparams->realm_dbname && !dbname)
	    dbname = strdup(rparams->realm_dbname);

	/* Get the value for the master key name */
	if (rparams->realm_mkey_name && !mkey_name)
	    mkey_name = strdup(rparams->realm_mkey_name);

	/* Get the value for the master key type */
	if (rparams->realm_enctype_valid && !enctypedone) {
	    master_keyblock.enctype = rparams->realm_enctype;
	    enctypedone++;
	}

	/* Get the value for the stashfile */
	if (rparams->realm_stash_file)
	    stash_file = strdup(rparams->realm_stash_file);

	/* Get the value for maximum ticket lifetime. */
	if (rparams->realm_max_life_valid)
	    rblock.max_life = rparams->realm_max_life;

	/* Get the value for maximum renewable ticket lifetime. */
	if (rparams->realm_max_rlife_valid)
	    rblock.max_rlife = rparams->realm_max_rlife;

	/* Get the value for the default principal expiration */
	if (rparams->realm_expiration_valid)
	    rblock.expiration = rparams->realm_expiration;

	/* Get the value for the default principal flags */
	if (rparams->realm_flags_valid)
	    rblock.flags = rparams->realm_flags;

	krb5_free_realm_params(context, rparams);
    }

    if (!v4dumpfile) {
	usage(PROGNAME, 1);
	return;
    }

    if (!enctypedone)
	master_keyblock.enctype = DEFAULT_KDC_ENCTYPE;

    if (!valid_enctype(master_keyblock.enctype)) {
	com_err(PROGNAME, KRB5_PROG_KEYTYPE_NOSUPP,
		"while setting up enctype %d", master_keyblock.enctype);
	return;
    }

    krb5_use_enctype(context, &master_encblock, master_keyblock.enctype);

    /* If the user has not requested locking, don't modify an existing database. */
    if (! tempdb) {
	retval = krb5_db_set_name(context, dbname);
	if (retval != ENOENT) {
	    fprintf(stderr,
		    "%s: The v5 database appears to already exist.\n",
		    PROGNAME);
	    return;
	}
	tempdbname = dbname;
    } else {
	int dbnamelen = strlen(dbname);
	tempdbname = malloc(dbnamelen + 2);
	if (tempdbname == 0) {
	    com_err(PROGNAME, ENOMEM, "allocating temporary filename");
	    return;
	}
	strcpy(tempdbname, dbname);
	tempdbname[dbnamelen] = '~';
	tempdbname[dbnamelen+1] = 0;
	(void) kdb5_db_destroy(context, tempdbname);
    }
	

    if (!realm) {
	if (retval = krb5_get_default_realm(context, &defrealm)) {
	    com_err(PROGNAME, retval, "while retrieving default realm name");
	    return;
	}	    
	realm = defrealm;
    }

    /* assemble & parse the master key name */

    if (retval = krb5_db_setup_mkey_name(context, mkey_name, realm,
					 &mkey_fullname, &master_princ)) {
	com_err(PROGNAME, retval, "while setting up master key name");
	return;
    }

    krb5_princ_set_realm_data(context, &db_create_princ, realm);
    krb5_princ_set_realm_length(context, &db_create_princ, strlen(realm));
    krb5_princ_set_realm_data(context, &tgt_princ, realm);
    krb5_princ_set_realm_length(context, &tgt_princ, strlen(realm));
    krb5_princ_component(context, &tgt_princ,1)->data = realm;
    krb5_princ_component(context, &tgt_princ,1)->length = strlen(realm);

    printf("Initializing database '%s' for realm '%s',\n\
master key name '%s'\n",
	   dbname, realm, mkey_fullname);

    if (read_mkey) {
	puts("You will be prompted for the version 5 database Master Password.");
	puts("It is important that you NOT FORGET this password.");
	fflush(stdout);
    }

    if (retval = krb5_db_fetch_mkey(context, master_princ, &master_encblock,
				    read_mkey, read_mkey, stash_file, 0, 
				    &master_keyblock)) {
	com_err(PROGNAME, retval, "while reading master key");
	return;
    }
    if (retval = krb5_process_key(context, &master_encblock, &master_keyblock)) {
	com_err(PROGNAME, retval, "while processing master key");
	return;
    }

    rblock.eblock = &master_encblock;
    if (retval = krb5_init_random_key(context, &master_encblock,
				      &master_keyblock, &rblock.rseed)) {
	com_err(PROGNAME, retval, "while initializing random key generator");
	(void) krb5_finish_key(context, &master_encblock);
	return;
    }
    if (retval = krb5_db_create(context, tempdbname)) {
	(void) krb5_finish_key(context, &master_encblock);
	(void) krb5_finish_random_key(context, &master_encblock, &rblock.rseed);
	(void) krb5_dbm_db_destroy(context, tempdbname);
	com_err(PROGNAME, retval, "while creating %sdatabase '%s'",
		tempdb ? "temporary " : "", tempdbname);
	return;
    }
    if (retval = krb5_db_set_name(context, tempdbname)) {
	(void) krb5_finish_key(context, &master_encblock);
	(void) krb5_finish_random_key(context, &master_encblock, &rblock.rseed);
	(void) krb5_dbm_db_destroy(context, tempdbname);
        com_err(PROGNAME, retval, "while setting active database to '%s'",
                tempdbname);
        return;
    }
    if (v4init(PROGNAME, v4dbname, v4manual, v4dumpfile)) {
	(void) krb5_finish_key(context, &master_encblock);
	(void) krb5_finish_random_key(context, &master_encblock, &rblock.rseed);
	(void) krb5_dbm_db_destroy(context, tempdbname);
	return;
    }
    if ((retval = krb5_db_init(context)) || 
	(retval = krb5_dbm_db_open_database(context))) {
	(void) krb5_finish_key(context, &master_encblock);
	(void) krb5_finish_random_key(context, &master_encblock, &rblock.rseed);
	(void) krb5_dbm_db_destroy(context, tempdbname);
	com_err(PROGNAME, retval, "while initializing the database '%s'",
		tempdbname);
	return;
    }

    if (retval = add_principal(context, master_princ, MASTER_KEY, &rblock)) {
	(void) krb5_db_fini(context);
	(void) krb5_finish_key(context, &master_encblock);
	(void) krb5_finish_random_key(context, &master_encblock, &rblock.rseed);
	(void) krb5_dbm_db_destroy(context, tempdbname);
	com_err(PROGNAME, retval, "while adding K/M to the database");
	return;
    }

    if (create_local_tgt &&
	(retval = add_principal(context, &tgt_princ, RANDOM_KEY, &rblock))) {
	(void) krb5_db_fini(context);
	(void) krb5_finish_key(context, &master_encblock);
	(void) krb5_finish_random_key(context, &master_encblock, &rblock.rseed);
	(void) krb5_dbm_db_destroy(context, tempdbname);
	com_err(PROGNAME, retval, "while adding TGT service to the database");
	return;
    }

    retval = process_v4_dump(context, v4dumpfile, realm);
    putchar('\n');
    if (retval)
	com_err(PROGNAME, retval, "while translating entries to the database");
    else {
	retval = fixup_database(context, realm);
    }
    
    /* clean up; rename temporary database if there were no errors */
    if (retval == 0) {
	if (retval = krb5_db_fini (context))
	    com_err(PROGNAME, retval, "while shutting down database");
	else if (tempdb && (retval = krb5_dbm_db_rename(context, tempdbname,
							dbname)))
	    com_err(PROGNAME, retval, "while renaming temporary database");
    } else {
	(void) krb5_db_fini (context);
	if (tempdb)
		(void) krb5_dbm_db_destroy (context, tempdbname);
    }
    (void) krb5_finish_key(context, &master_encblock);
    (void) krb5_finish_random_key(context, &master_encblock, &rblock.rseed);
    memset((char *)master_keyblock.contents, 0, master_keyblock.length);
    krb5_free_context(context);
    return;
}

static int
v4init(pname, name, manual, dumpfile)
char *pname, *name;
int manual;
char *dumpfile;
{
    int fd;
    int ok = 0;

    if (!manual) {
	fd = open(v4_mkeyfile, O_RDONLY, 0600);
	if (fd >= 0) {
	    if (read(fd, master_key, sizeof(master_key)) == sizeof(master_key))
		ok = 1;
	    close(fd);
	}
    }
    if (!ok) {
	des_read_password(master_key, "V4 Kerberos master key: ", 0);
	printf("\n");
    }
    key_sched(master_key, master_key_schedule);
    return 0;
}

static krb5_error_code
enter_in_v5_db(context, realm, princ)
krb5_context context;
char *realm;
Principal *princ;
{
    krb5_db_entry entry;
    krb5_error_code retval;
    krb5_keyblock v4v5key;
    int nentries = 1;
    des_cblock v4key;
    char *name;
    krb5_timestamp	mod_time;
    krb5_principal	mod_princ;
    krb5_keysalt	keysalt;

    /* don't convert local TGT if we created a TGT already.... */
    if (create_local_tgt && !strcmp(princ->name, "krbtgt") &&
	!strcmp(princ->instance, realm)) {
	    if (verbose)
		    printf("\nignoring local TGT: '%s.%s' ...",
			   princ->name, princ->instance);
	    return 0;
    }
    if (!strcmp(princ->name, KERB_M_NAME) &&
	!strcmp(princ->instance, KERB_M_INST)) {
	des_cblock key_from_db;
	int val;

	/* here's our chance to verify the master key */
	/*
	 * use the master key to decrypt the key in the db, had better
	 * be the same! 
	 */
	memcpy(key_from_db, (char *)&princ->key_low, 4);
	memcpy(((char *) key_from_db) + 4, (char *)&princ->key_high, 4);
	pcbc_encrypt((C_Block *) &key_from_db,
		     (C_Block *) &key_from_db,
		     (long) sizeof(C_Block),
		     master_key_schedule,
		     (C_Block *) master_key,
		     DECRYPT);
	val = memcmp((char *) master_key, (char *) key_from_db,
		     sizeof(master_key));
	memset((char *)key_from_db, 0, sizeof(key_from_db));
	if (val) {
	    return KRB5_KDB_BADMASTERKEY;
	}
	if (verbose)
	    printf("\nignoring '%s.%s' ...", princ->name, princ->instance);
	return 0;
    }
    memset((char *) &entry, 0, sizeof(entry));
    if (retval = krb5_425_conv_principal(context, princ->name, princ->instance,
					 realm, &entry.princ))
	return retval;
    if (verbose) {
	if (retval = krb5_unparse_name(context, entry.princ, &name))
	   name = strdup("<not unparsable name!>");
	if (verbose)
	    printf("\ntranslating %s...", name);
	free(name);
    }

    if (retval = krb5_build_principal(context, &mod_princ,
				      strlen(realm),
				      realm, princ->mod_name,
				      princ->mod_instance[0] ? princ->mod_instance : 0,
				      0)) {
	krb5_free_principal(context, entry.princ);
	return retval;
    }
    mod_time = princ->mod_date;

    entry.max_life = princ->max_life * 60 * 5;
    entry.max_renewable_life = rblock.max_rlife;
    entry.len = KRB5_KDB_V1_BASE_LENGTH;
    entry.expiration = princ->exp_date;
    entry.attributes = rblock.flags;	/* XXX is there a way to convert
					   the old attrs? */

    memcpy((char *)v4key, (char *)&(princ->key_low), 4);
    memcpy((char *) (((char *) v4key) + 4), (char *)&(princ->key_high), 4);
    pcbc_encrypt((C_Block *) &v4key,
		 (C_Block *) &v4key,
		 (long) sizeof(C_Block),
		 master_key_schedule,
		 (C_Block *) master_key,
		 DECRYPT);

    v4v5key.magic = KV5M_KEYBLOCK;
    v4v5key.contents = (krb5_octet *)v4key;
    v4v5key.enctype = ENCTYPE_DES_CBC_CRC;
    v4v5key.length = sizeof(v4key);

    retval = krb5_dbe_create_key_data(context, &entry);
    if (retval) {
	krb5_free_principal(context, entry.princ);
	krb5_free_principal(context, mod_princ);
	return retval;
    }

    keysalt.type = KRB5_KDB_SALTTYPE_V4;
    keysalt.data.length = 0;
    keysalt.data.data = (char *) NULL;
    retval = krb5_dbekd_encrypt_key_data(context, rblock.eblock,
					 &v4v5key, &keysalt, 
					 princ->key_version,
					 &entry.key_data[0]);
    if (!retval)
	retval = krb5_dbe_update_mod_princ_data(context, &entry,
						mod_time, mod_princ);
    if (retval) {
	krb5_db_free_principal(context, &entry, 1);
	krb5_free_principal(context, mod_princ);
	return retval;
    }
    memset((char *)v4key, 0, sizeof(v4key));

    retval = krb5_db_put_principal(context, &entry, &nentries);

    if (!retval && !strcmp(princ->name, "krbtgt") &&
	strcmp(princ->instance, realm) && princ->instance[0]) {
	    krb5_free_principal(context, entry.princ);
	    if (retval = krb5_build_principal(context, &entry.princ,
					      strlen(princ->instance),
					      princ->instance,
					      "krbtgt", realm, 0))
		    return retval;
	    retval = krb5_db_put_principal(context, &entry, &nentries);
    }

    krb5_db_free_principal(context, &entry, 1);
    krb5_free_principal(context, mod_princ);

    return retval;
}

static krb5_error_code
add_principal(context, princ, op, pblock)
krb5_context context;
krb5_principal princ;
enum ap_op op;
struct realm_info *pblock;
{
    krb5_db_entry entry;
    krb5_error_code retval;
    krb5_keyblock *rkey;
    int nentries = 1;
    krb5_timestamp mod_time;
    krb5_principal mod_princ;

    memset((char *) &entry, 0, sizeof(entry));
    if (retval = krb5_copy_principal(context, princ, &entry.princ))
	return(retval);
    entry.max_life = pblock->max_life;
    entry.max_renewable_life = pblock->max_rlife;
    entry.len = KRB5_KDB_V1_BASE_LENGTH;
    entry.expiration = pblock->expiration;
    
    if ((retval = krb5_timeofday(context, &mod_time))) {
	krb5_db_free_principal(context, &entry, 1);
	return retval;
    }
    entry.attributes = pblock->flags;

    if (retval = krb5_dbe_create_key_data(context, &entry)) {
	krb5_db_free_principal(context, &entry, 1);
	return(retval);
    }

    switch (op) {
    case MASTER_KEY:
	entry.attributes |= KRB5_KDB_DISALLOW_ALL_TIX;
	if (retval = krb5_dbekd_encrypt_key_data(context, pblock->eblock,
						 &master_keyblock,
						 (krb5_keysalt *) NULL, 1,
						 &entry.key_data[0])) {
	    krb5_db_free_principal(context, &entry, 1);
	    return retval;
	}
	break;
    case RANDOM_KEY:
	if (retval = krb5_random_key(context, pblock->eblock, pblock->rseed,
				     &rkey)) {
	    krb5_db_free_principal(context, &entry, 1);
	    return retval;
	}
	if (retval = krb5_dbekd_encrypt_key_data(context, pblock->eblock,
						 rkey,
						 (krb5_keysalt *) NULL, 1,
						 &entry.key_data[0])) {
	    krb5_db_free_principal(context, &entry, 1);
	    return(retval);
	}
	krb5_free_keyblock(context, rkey);
	break;
    case NULL_KEY:
	return EOPNOTSUPP;
    default:
	break;
    }

    retval = krb5_dbe_update_mod_princ_data(context, &entry,
					    mod_time, &db_create_princ);
    if (!retval)
	retval = krb5_db_put_principal(context, &entry, &nentries);
    krb5_db_free_principal(context, &entry, 1);
    return retval;
}

/*
 * Convert a struct tm * to a UNIX time.
 */


#define daysinyear(y) (((y) % 4) ? 365 : (((y) % 100) ? 366 : (((y) % 400) ? 365 : 366)))

#define SECSPERDAY 24*60*60
#define SECSPERHOUR 60*60
#define SECSPERMIN 60

static int cumdays[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
			     365};

static int leapyear[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static int nonleapyear[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static long
maketime(tp, local)
register struct tm *tp;
int local;
{
    register long retval;
    int foo;
    int *marray;

    if (tp->tm_mon < 0 || tp->tm_mon > 11 ||
	tp->tm_hour < 0 || tp->tm_hour > 23 ||
	tp->tm_min < 0 || tp->tm_min > 59 ||
	tp->tm_sec < 0 || tp->tm_sec > 59) /* out of range */
	return 0;

    retval = 0;
    if (tp->tm_year < 1900)
	foo = tp->tm_year + 1900;
    else
	foo = tp->tm_year;

    if (foo < 1901 || foo > 2038)	/* year is too small/large */
	return 0;

    if (daysinyear(foo) == 366) {
	if (tp->tm_mon > 1)
	    retval+= SECSPERDAY;	/* add leap day */
	marray = leapyear;
    } else
	marray = nonleapyear;

    if (tp->tm_mday < 0 || tp->tm_mday > marray[tp->tm_mon])
	return 0;			/* out of range */

    while (--foo >= 1970)
	retval += daysinyear(foo) * SECSPERDAY;

    retval += cumdays[tp->tm_mon] * SECSPERDAY;
    retval += (tp->tm_mday-1) * SECSPERDAY;
    retval += tp->tm_hour * SECSPERHOUR + tp->tm_min * SECSPERMIN + tp->tm_sec;

    if (local) {
	/* need to use local time, so we retrieve timezone info */
	struct timezone tz;
	struct timeval tv;
	if (gettimeofday(&tv, &tz) < 0) {
	    /* some error--give up? */
	    return(retval);
	}
	retval += tz.tz_minuteswest * SECSPERMIN;
    }
    return(retval);
}

static long
time_explode(cp)
register char *cp;
{
    char wbuf[5];
    struct tm tp;
    int local;

    memset((char *)&tp, 0, sizeof(tp));
    
    if (strlen(cp) > 10) {		/* new format */
	(void) strncpy(wbuf, cp, 4);
	wbuf[4] = 0;
	tp.tm_year = atoi(wbuf);
	cp += 4;			/* step over the year */
	local = 0;			/* GMT */
    } else {				/* old format: local time, 
					   year is 2 digits, assuming 19xx */
	wbuf[0] = *cp++;
	wbuf[1] = *cp++;
	wbuf[2] = 0;
	tp.tm_year = 1900 + atoi(wbuf);
	local = 1;			/* local */
    }

    wbuf[0] = *cp++;
    wbuf[1] = *cp++;
    wbuf[2] = 0;
    tp.tm_mon = atoi(wbuf)-1;

    wbuf[0] = *cp++;
    wbuf[1] = *cp++;
    tp.tm_mday = atoi(wbuf);
    
    wbuf[0] = *cp++;
    wbuf[1] = *cp++;
    tp.tm_hour = atoi(wbuf);
    
    wbuf[0] = *cp++;
    wbuf[1] = *cp++;
    tp.tm_min = atoi(wbuf);


    return(maketime(&tp, local));
}

static krb5_error_code
process_v4_dump(context, dumpfile, realm)
krb5_context context;
char *dumpfile;
char *realm;
{
    krb5_error_code retval;
    FILE *input_file;
    Principal aprinc;
    char    exp_date_str[50];
    char    mod_date_str[50];
    int     temp1, temp2, temp3;
    long time_explode();

    input_file = fopen(dumpfile, "r");
    if (!input_file)
	return errno;

    for (;;) {			/* explicit break on eof from fscanf */
	int nread;

	memset((char *)&aprinc, 0, sizeof(aprinc));
	nread = fscanf(input_file,
		       "%s %s %d %d %d %hd %x %x %s %s %s %s\n",
		       aprinc.name,
		       aprinc.instance,
		       &temp1,
		       &temp2,
		       &temp3,
		       &aprinc.attributes,
		       &aprinc.key_low,
		       &aprinc.key_high,
		       exp_date_str,
		       mod_date_str,
		       aprinc.mod_name,
		       aprinc.mod_instance);
	if (nread != 12) {
	    retval = nread == EOF ? 0 : KRB5_KDB_DB_CORRUPT;
	    break;
	}
	aprinc.key_low = ntohl (aprinc.key_low);
	aprinc.key_high = ntohl (aprinc.key_high);
	aprinc.max_life = (unsigned char) temp1;
	aprinc.kdc_key_ver = (unsigned char) temp2;
	aprinc.key_version = (unsigned char) temp3;
	aprinc.exp_date = time_explode(exp_date_str);
	aprinc.mod_date = time_explode(mod_date_str);
	if (aprinc.instance[0] == '*')
	    aprinc.instance[0] = '\0';
	if (aprinc.mod_name[0] == '*')
	    aprinc.mod_name[0] = '\0';
	if (aprinc.mod_instance[0] == '*')
	    aprinc.mod_instance[0] = '\0';
	if (retval = enter_in_v5_db(context, realm, &aprinc))
	    break;
    }
    (void) fclose(input_file);
    return retval;
}

static krb5_error_code fixup_database(context, realm)
    krb5_context context;
    char * realm;
{
    krb5_db_entry entry;
    krb5_error_code retval;
    int nprincs;
    krb5_boolean more;

    nprincs = 1;
    if (retval = krb5_db_get_principal(context, &tgt_princ, &entry, 
				       &nprincs, &more))
	return retval;
    
    if (nprincs == 0)
	return 0;
    
    entry.attributes |= KRB5_KDB_SUPPORT_DESMD5;
    
    retval = krb5_db_put_principal(context, &entry, &nprincs);
    
    if (nprincs)
	krb5_db_free_principal(context, &entry, nprincs);
    
    return retval;
}
    
#else /* KRB5_KRB4_COMPAT */
void
load_v4db(argc, argv)
	int argc;
	char *argv[];
{
	printf("This version of krb5_edit does not support the V4 load command.\n");
}
#endif /* KRB5_KRB4_COMPAT */
