/*
 * admin/edit/kdb5_edit.c
 *
 * (C) Copyright 1990,1991, 1996 by the Massachusetts Institute of Technology.
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
 * Edit a KDC database.
 */

#include <stdio.h>
#include <k5-int.h>
#include <kadm5/admin.h>
#include <kadm5/adb.h>
#include <time.h>
#include "kdb5_util.h"

char	*Err_no_master_msg = "Master key not entered!\n";
char	*Err_no_database = "Database not currently opened!\n";

/*
 * XXX Ick, ick, ick.  These global variables shouldn't be global....
 */
static char *mkey_password = 0;

/*
 * I can't figure out any way for this not to be global, given how ss
 * works.
 */

int exit_status = 0;
krb5_context util_context;
osa_adb_policy_t policy_db;
kadm5_config_params global_params;

/*
 * Script input, specified by -s.
 */
FILE *scriptfile = (FILE *) NULL;

static void
usage(who, status)
    char *who;
    int status;
{
    fprintf(stderr,
	    "usage: %s [-d dbpathname ] [-r realmname] [-R request ]\n",
	    who);
    fprintf(stderr, "\t [-k enctype] [-M mkeyname] [-f stashfile]\n");
    exit(status);
}

krb5_keyblock master_keyblock;
krb5_principal master_princ;
krb5_db_entry master_entry;
krb5_encrypt_block master_encblock;
krb5_pointer master_random;
int	valid_master_key = 0;

char *progname;
krb5_boolean manual_mkey = FALSE;
krb5_boolean dbactive = FALSE;

char *kdb5_util_Init(argc, argv)
    int argc;
    char *argv[];
{
    extern char *optarg;	
    int optchar;
    krb5_error_code retval;
    char *request = NULL;

    retval = krb5_init_context(&util_context);
    if (retval) {
	    fprintf(stderr, "krb5_init_context failed with error #%ld\n",
		    (long) retval);
	    exit(1);
    }
    krb5_init_ets(util_context);
    initialize_adb_error_table();

    if (strrchr(argv[0], '/'))
	argv[0] = strrchr(argv[0], '/')+1;

    progname = argv[0];

    while ((optchar = getopt(argc, argv, "P:d:a:r:R:k:M:e:ms:f:")) != EOF) {
	switch(optchar) {
        case 'P':		/* Only used for testing!!! */
	    mkey_password = optarg;
	    manual_mkey = TRUE;
	    break;
	case 'd':
	    global_params.dbname = optarg;
	    global_params.mask |= KADM5_CONFIG_DBNAME;
	    break;
       case 'a':
	    global_params.admin_dbname = optarg;
	    global_params.mask |= KADM5_CONFIG_ADBNAME;
	    break;
	case 'r':
	    global_params.realm = optarg;
	    global_params.mask |= KADM5_CONFIG_REALM;
	    /* not sure this is really necessary */
	    if ((retval = krb5_set_default_realm(util_context,
						 global_params.realm))) {
		 com_err(progname, retval, "while setting default realm name");
		 exit(1);
	    }
	    break;
        case 'R':
	    request = optarg;
	    break;
	case 'k':
	    if (krb5_string_to_enctype(optarg, &global_params.enctype))
		 com_err(argv[0], 0, "%s is an invalid enctype", optarg);
	    global_params.mask |= KADM5_CONFIG_ENCTYPE;
	    break;
	case 'M':			/* master key name in DB */
	    global_params.mkey_name = optarg;
	    global_params.mask |= KADM5_CONFIG_MKEY_NAME;
	    break;
	case 'm':
	    manual_mkey = TRUE;
	    global_params.mkey_from_kbd = 1;
	    global_params.mask |= KADM5_CONFIG_MKEY_FROM_KBD;
	    break;
	case 's':
	    /* Open the script file */
	    if (!(scriptfile = fopen(optarg, "r"))) {
		com_err(argv[0], errno, "while opening script file %s",
			optarg);
		exit(1);
	    }
	    break;
	case 'f':
	    global_params.stash_file = optarg;
	    global_params.mask |= KADM5_CONFIG_STASH_FILE;
	    break;
	case '?':
	default:
	    usage(progname, 1);
	    /*NOTREACHED*/
	}
    }

    if (retval = kadm5_get_config_params(util_context, NULL, NULL,
					 &global_params, &global_params)) {
	 com_err(argv[0], retval, "while retreiving configuration parameters");
	 exit(1);
    }

    /*
     * Dump creates files which should not be world-readable.  It is
     * easiest to do a single umask call here; any shells run by the
     * ss command interface will have umask = 77 but that is not a
     * serious problem.
     */
    (void) umask(077);

    master_keyblock.enctype = global_params.enctype;
    if (master_keyblock.enctype != ENCTYPE_UNKNOWN) {
	if (!valid_enctype(master_keyblock.enctype)) {
	    char tmp[32];
	    if (krb5_enctype_to_string(master_keyblock.enctype,
				       tmp, sizeof(tmp)))
		com_err(argv[0], KRB5_PROG_KEYTYPE_NOSUPP,
			"while setting up enctype %d", master_keyblock.enctype);
	    else
		com_err(argv[0], KRB5_PROG_KEYTYPE_NOSUPP, tmp);
	    exit(1);
	}
	krb5_use_enctype(util_context, &master_encblock,
			 master_keyblock.enctype);
    }


    open_db_and_mkey();

    exit_status = 0;	/* It's OK if we get errors in open_db_and_mkey */
    return request;
}

#if 0
/*
 * This function is no longer used in kdb5_util (and it would no
 * longer work, anyway).
 */
void set_dbname(argc, argv)
    int argc;
    char *argv[];
{
    krb5_error_code retval;

    if (argc < 3) {
	com_err(argv[0], 0, "Too few arguments");
	com_err(argv[0], 0, "Usage: %s dbpathname realmname", argv[0]);
	exit_status++;
	return;
    }
    if (dbactive) {
	if ((retval = krb5_db_fini(util_context)) && retval!= KRB5_KDB_DBNOTINITED) {
	    com_err(argv[0], retval, "while closing previous database");
	    exit_status++;
	    return;
	}
	if (valid_master_key) {
		(void) krb5_finish_key(util_context, &master_encblock);
		(void) krb5_finish_random_key(util_context, &master_encblock,
					      &master_random);
		memset((char *)master_keyblock.contents, 0,
		       master_keyblock.length);
		krb5_xfree(master_keyblock.contents);
		master_keyblock.contents = NULL;
		valid_master_key = 0;
	}
	krb5_free_principal(util_context, master_princ);
	dbactive = FALSE;
    }

    (void) set_dbname_help(argv[0], argv[1]);
    return;
}
#endif

/*
 * open_db_and_mkey: Opens the KDC and policy database, and sets the
 * global master_* variables.  Sets dbactive to TRUE if the databases
 * are opened, and valid_master_key to 1 if the global master
 * variables are set properly.  Returns 0 on success, and 1 on
 * failure, but it is not considered a failure if the master key
 * cannot be fetched (the master key stash file may not exist when the
 * program is run).
 */
int open_db_and_mkey()
{
    krb5_error_code retval;
    int nentries, i;
    krb5_boolean more;
    krb5_data scratch, pwd;

    dbactive = FALSE;
    valid_master_key = 0;

    if ((retval = krb5_db_set_name(util_context, global_params.dbname))) {
	com_err(progname, retval, "while setting active database to '%s'",
		global_params.dbname);
	exit_status++;
	return(1);
    } 
    if ((retval = krb5_db_init(util_context))) {
	com_err(progname, retval, "while initializing database");
	exit_status++;
	return(1);
    }
    if (retval = osa_adb_open_policy(&policy_db, &global_params)) {
	 com_err(progname, retval, "opening policy database");
	 exit_status++;
	 return (1);
    }

   /* assemble & parse the master key name */

    if ((retval = krb5_db_setup_mkey_name(util_context,
					  global_params.mkey_name,
					  global_params.realm, 
					  0, &master_princ))) {
	com_err(progname, retval, "while setting up master key name");
	exit_status++;
	return(1);
    }
    nentries = 1;
    if ((retval = krb5_db_get_principal(util_context, master_princ, 
					&master_entry, &nentries, &more))) {
	com_err(progname, retval, "while retrieving master entry");
	exit_status++;
	(void) krb5_db_fini(util_context);
	return(1);
    } else if (more) {
	com_err(progname, KRB5KDC_ERR_PRINCIPAL_NOT_UNIQUE,
		"while retrieving master entry");
	exit_status++;
	(void) krb5_db_fini(util_context);
	return(1);
    } else if (!nentries) {
	com_err(progname, KRB5_KDB_NOENTRY, "while retrieving master entry");
	exit_status++;
	(void) krb5_db_fini(util_context);
	return(1);
    }

    krb5_db_free_principal(util_context, &master_entry, nentries);

    /* the databases are now open, and the master principal exists */
    dbactive = TRUE;
    
    if (mkey_password) {
	pwd.data = mkey_password;
	pwd.length = strlen(mkey_password);
	retval = krb5_principal2salt(util_context, master_princ, &scratch);
	if (retval) {
	    com_err(progname, retval, "while calculated master key salt");
	    return(1);
	}

	/* If no encryption type is set, use the default */
	if (master_keyblock.enctype == ENCTYPE_UNKNOWN) {
		master_keyblock.enctype = DEFAULT_KDC_ENCTYPE;
		if (!valid_enctype(master_keyblock.enctype)) {
			char tmp[32];
			if (krb5_enctype_to_string(master_keyblock.enctype,
						   tmp, sizeof(tmp)))
				com_err(progname, KRB5_PROG_KEYTYPE_NOSUPP,
					"while setting up enctype %d", master_keyblock.enctype);
			else
				com_err(progname, KRB5_PROG_KEYTYPE_NOSUPP, tmp);
			exit(1);
		}
		krb5_use_enctype(util_context, &master_encblock,
				 master_keyblock.enctype);
	}

	retval = krb5_string_to_key(util_context, &master_encblock, 
				    &master_keyblock, &pwd, &scratch);
	if (retval) {
	    com_err(progname, retval,
		    "while transforming master key from password");
	    return(1);
	}
	free(scratch.data);
	mkey_password = 0;
    } else if ((retval = krb5_db_fetch_mkey(util_context, master_princ, 
					    &master_encblock, manual_mkey, 
					    FALSE, global_params.stash_file,
					    0, &master_keyblock))) {
	com_err(progname, retval, "while reading master key");
	com_err(progname, 0, "Warning: proceeding without master key");
	exit_status++;
	return(0);
    }
    if ((retval = krb5_db_verify_master_key(util_context, master_princ, 
					    &master_keyblock,&master_encblock))
	) {
	com_err(progname, retval, "while verifying master key");
	exit_status++;
	memset((char *)master_keyblock.contents, 0, master_keyblock.length);
	krb5_xfree(master_keyblock.contents);
	return(1);
    }
    if ((retval = krb5_process_key(util_context, &master_encblock,
				   &master_keyblock))) {
	com_err(progname, retval, "while processing master key");
	exit_status++;
	memset((char *)master_keyblock.contents, 0, master_keyblock.length);
	krb5_xfree(master_keyblock.contents);
	return(1);
    }
    if ((retval = krb5_init_random_key(util_context, &master_encblock,
				       &master_keyblock,
				       &master_random))) {
	com_err(progname, retval, "while initializing random key generator");
	exit_status++;
	(void) krb5_finish_key(util_context, &master_encblock);
	memset((char *)master_keyblock.contents, 0, master_keyblock.length);
	krb5_xfree(master_keyblock.contents);
	return(1);
    }

    valid_master_key = 1;
    dbactive = TRUE;
    return 0;
}

#ifdef HAVE_GETCWD
#undef getwd
#endif

int 
quit()
{
    krb5_error_code retval;
    static krb5_boolean finished = 0;

    if (finished)
	return 0;
    if (valid_master_key) {
	    (void) krb5_finish_key(util_context, &master_encblock);
	    (void) krb5_finish_random_key(util_context, &master_encblock, 
					  &master_random);
    }
    retval = krb5_db_fini(util_context);
    memset((char *)master_keyblock.contents, 0, master_keyblock.length);
    finished = TRUE;
    if (retval && retval != KRB5_KDB_DBNOTINITED) {
	com_err(progname, retval, "while closing database");
	exit_status++;
	return 1;
    }
    return 0;
}
