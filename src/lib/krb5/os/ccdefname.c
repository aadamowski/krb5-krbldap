/*
 * lib/krb5/os/ccdefname.c
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
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
 * Return default cred. cache name.
 */

#define NEED_WINDOWS
#include "k5-int.h"
#include <stdio.h>

#ifdef _MACINTOSH
static CInfoPBRec	theCatInfo;
static	char		*FileBuffer;
static	int			indexCount;
static FSSpec		theWorkingFile;

static char*
GetDirName(short vrefnum, long dirid, char *dststr)
{
CInfoPBRec	theCatInfo;
FSSpec		theParDir;
char		str[37];
char		*curstr;
OSErr		err;
	// Get info on the directory itself, it's name and it's parent
	theCatInfo.dirInfo.ioCompletion		= NULL;
	theCatInfo.dirInfo.ioNamePtr		= (StringPtr) str;
	theCatInfo.dirInfo.ioVRefNum		= vrefnum;
	theCatInfo.dirInfo.ioFDirIndex		= -1;
	theCatInfo.dirInfo.ioDrDirID		= dirid;
	err = PBGetCatInfo(&theCatInfo, FALSE);

	// If I'm looking at the root directory and I've tried going up once
	// start returning down the call chain
	if (err != noErr || (dirid == 2 && theCatInfo.hFileInfo.ioFlParID == 2))
		return dststr;

	// Construct a file spec for the parent
	curstr = GetDirName(theCatInfo.dirInfo.ioVRefNum, theCatInfo.hFileInfo.ioFlParID, dststr);

	// Copy the pascal string to the end of a C string
	BlockMoveData(&str[1], curstr, str[0]);
	curstr += str[0];
	*curstr++ = ':';
	
	// return a pointer to the end of the string (for someone below to append to)
	return curstr;
}

static void
GetPathname(FSSpec *theFile, char *dststr)
{
FSSpec		theParDir;
char		*curstr;
OSErr		err;

	// Start crawling up the directory path recursivly
	curstr = GetDirName(theFile->vRefNum, theFile->parID, dststr);
	BlockMoveData(&theFile->name[1], curstr, theFile->name[0]);
	curstr += theFile->name[0];
	*curstr = 0;
}
#endif


KRB5_DLLIMP char FAR * KRB5_CALLCONV
krb5_cc_default_name(context)
    krb5_context context;
{
    char *name = getenv(KRB5_ENV_CCNAME);
    static char name_buf[160];
    
    if (name == 0) {

#ifdef HAVE_MACSOCK_H
{
short	vRefnum;
long	parID;
OSErr	theErr;
FSSpec	krbccSpec;
char	pathbuf[255];

	theErr = FindFolder(kOnSystemDisk, kPreferencesFolderType, kDontCreateFolder, &vRefnum, &parID);
	FSMakeFSSpec(vRefnum, parID, "\pkrb5cc", &krbccSpec);
	GetPathname(&krbccSpec, &pathbuf);
	sprintf(name_buf, "STDIO:%s", pathbuf);
//	strcpy (name_buf, "STDIO:krb5cc");
}
#else
#if defined(_MSDOS) || defined(_WIN32)
        {
            char defname[160];                  /* Default value */

#if defined(_WIN32)
	    /* If the RegKRB5CCNAME variable is set, it will point to
	     * the registry key that has the name of the cache to use.
	     * The Gradient PC-DCE sets the registry key
	     * [HKEY_CURRENT_USER\Software\Gradient\DCE\Default\KRB5CCNAME]
	     * to point at the cache file name (including the FILE: prefix).
	     * By indirecting with the RegKRB5CCNAME entry in kerberos.ini,
	     * we can accomodate other versions that might set a registry
	     * variable.
	     */
	    char newkey[256];
	    
	    LONG name_buf_size;
	    HKEY hkey;
	    DWORD ipType;
	    int found = 0;
	    char *cp;
    

	    GetPrivateProfileString(INI_FILES, "RegKRB5CCNAME", "", 
				    newkey, sizeof(newkey), KERBEROS_INI); 
	    if (strlen(newkey)) {
		cp = strrchr(newkey,'\\');
		if (cp) {
		    *cp = '\0'; /* split the string */
		    cp++;
		} else
		    cp = "";
		if (RegOpenKeyEx(HKEY_CURRENT_USER, newkey, 0,
				 KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS) {
		    name_buf_size = sizeof(name_buf);
		    if (RegQueryValueEx(hkey, cp, 0, &ipType, 
					name_buf, &name_buf_size)
			== ERROR_SUCCESS) 
			found = 1;
		}
	    }
	    if(!(found)) {
#endif
		
		GetWindowsDirectory (defname, sizeof(defname)-7);
		strcat (defname, "\\krb5cc");
		strcpy (name_buf, "FILE:");
		GetPrivateProfileString(INI_FILES, INI_KRB_CCACHE, defname,
					name_buf+5, sizeof(name_buf)-5, KERBEROS_INI);
#if defined(_WIN32)
	    }
#endif
	}
#else
	sprintf(name_buf, "FILE:/tmp/krb5cc_%d", getuid());
#endif
#endif
	name = name_buf;
    }
    return name;
}
    
