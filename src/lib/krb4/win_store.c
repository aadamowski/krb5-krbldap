/*
 * win_store.c
 *
 * Kerberos configuration storage management routines.
 *
 * Originally coded by John Rivlin / Fusion Software, Inc.
 *
 * This file incorporates replacements for the following Unix files:
 *   g_cnffil.c
 */

#include <windows.h>
#include <stdio.h>
#include <assert.h>

#include "krb.h"

#define KERBEROS_INI	"kerberos.ini"

#define INI_FILES	"Files"
#define INI_KRB_CONF	"krb.conf"
#define INI_KRB_REALMS	"krb.realms"
#define DEF_KRB_CONF	"krb.con"
#define DEF_KRB_REALMS	"krb.rea"

#define INI_DEFAULTS	"Defaults"
#define INI_USER	"User"


/*
 * Returns an open file handle to the configuration file.  This
 * file was called "krb.conf" on Unix.  Here we search for the entry
 * "krb.conf=" in the "[FILES]" section of the "kerberos.ini" file
 * located in the Windows directory.  If the entry doesn't exist in
 * the kerberos.ini file, then "krb.con" in the Windows directory is
 * used in its place.
 */
FILE*
krb__get_cnffile()
{
	FILE *cnffile = 0;
	char cnfname[FILENAME_MAX];
	char defname[FILENAME_MAX];
	UINT rc;

	rc = GetWindowsDirectory(defname, sizeof(defname));
	assert(rc > 0);

	strcat(defname, "\\");

	strcat(defname, DEF_KRB_CONF);

	GetPrivateProfileString(INI_FILES, INI_KRB_CONF, defname,
		cnfname, sizeof(cnfname), KERBEROS_INI);

	cnffile = fopen(cnfname, "r");

	return cnffile;
}


/*
 * Returns an open file handle to the realms file.  This
 * file was called "krb.realms" on Unix.  Here we search for the entry
 * "krb.realms=" in the "[FILES]" section of the "kerberos.ini" file
 * located in the Windows directory.  If the entry doesn't exist in
 * the kerberos.ini file, then "krb.rea" in the Windows directory is
 * used in its place.
 */
FILE*
krb__get_realmsfile()
{
	FILE *realmsfile = 0;
	char realmsname[FILENAME_MAX];
	char defname[FILENAME_MAX];
	UINT rc;

	rc = GetWindowsDirectory(defname, sizeof(defname));
	assert(rc > 0);
	strcat(defname, "\\");
	strcat(defname, DEF_KRB_REALMS);

	GetPrivateProfileString(INI_FILES, INI_KRB_REALMS, defname,
		realmsname, sizeof(realmsname), KERBEROS_INI);

	realmsfile = fopen(realmsname, "r");

	return realmsfile;
}


/*
 * Returns the current default user.  This information is stored in
 * the [DEFAULTS] section of the "kerberos.ini" file located in the
 * Windows directory.
 */
char FAR * INTERFACE
krb_get_default_user()
{
	static char username[ANAME_SZ];

	GetPrivateProfileString(INI_DEFAULTS, INI_USER, "",
		username, sizeof(username), KERBEROS_INI);

	return username;
}


/*
 * Sets the default user name stored in the "kerberos.ini" file.
 */
int INTERFACE
krb_set_default_user(username)
	char *username;
{
	BOOL rc;

	rc = WritePrivateProfileString(INI_DEFAULTS, INI_USER,
		username, KERBEROS_INI);

	if (rc)
		return KSUCCESS;
	else
		return KFAILURE;
}
