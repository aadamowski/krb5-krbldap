/*
 * prof_init.c --- routines that manipulate the user-visible profile_t
 * 	object.
 */

#include <stdio.h>
#include <string.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <errno.h>

#if TARGET_OS_MAC && TARGET_API_MAC_CARBON
#include <FullPOSIXPath.h>
#endif

#include "prof_int.h"

/* Find a 4-byte integer type */
#if	(SIZEOF_SHORT == 4)
typedef short	prof_int32;
#elif	(SIZEOF_INT == 4)
typedef int	prof_int32;
#elif	(SIZEOF_LONG == 4)
typedef	int	prof_int32;
#else	/* SIZEOF_LONG == 4 */
error(do not have a 4-byte integer type)
#endif	/* SIZEOF_LONG == 4 */

KRB5_DLLIMP errcode_t KRB5_CALLCONV
profile_init(files, ret_profile)
	const_profile_filespec_t *files;
	profile_t *ret_profile;
{
	const_profile_filespec_t *fs;
	profile_t profile;
	prf_file_t  new_file, last = 0;
	errcode_t retval = 0;

	initialize_prof_error_table();

	profile = malloc(sizeof(struct _profile_t));
	if (!profile)
		return ENOMEM;
	memset(profile, 0, sizeof(struct _profile_t));
	profile->magic = PROF_MAGIC_PROFILE;

        /* if the filenames list is not specified return an empty profile */
        if ( files ) {
	    for (fs = files; !PROFILE_LAST_FILESPEC(*fs); fs++) {
/*                printf ("profile_init (%s)\n", *fs); */
		retval = profile_open_file(*fs, &new_file);
		/* if this file is missing, skip to the next */
		if (retval == ENOENT) {
			continue;
		}
		if (retval) {
			profile_release(profile);
			return retval;
		}
		if (last)
			last->next = new_file;
		else
			profile->first_file = new_file;
		last = new_file;
	    }
	    /*
	     * If last is still null after the loop, then all the files were
	     * missing, so return the appropriate error.
	     */
	    if (!last) {
		profile_release(profile);
		return ENOENT;
	    }
	}

        *ret_profile = profile;
        return 0;
}

#if !TARGET_OS_MAC || TARGET_RT_MAC_MACHO
/* 
 * On CFM MacOS, profile_init_path is the same as profile_init
 */
KRB5_DLLIMP errcode_t KRB5_CALLCONV
profile_init_path(filepath, ret_profile)
	const_profile_filespec_list_t filepath;
	profile_t *ret_profile;
{
	int n_entries, i;
	int ent_len;
	const char *s, *t;
	char **filenames;
	errcode_t retval;

	/* count the distinct filename components */
	for(s = filepath, n_entries = 1; *s; s++) {
		if (*s == ':')
			n_entries++;
	}
	
	/* the array is NULL terminated */
	filenames = (char**) malloc((n_entries+1) * sizeof(char*));
	if (filenames == 0)
		return ENOMEM;

	/* measure, copy, and skip each one */
	for(s = filepath, i=0; (t = strchr(s, ':')) || (t=s+strlen(s)); s=t+1, i++) {
		ent_len = t-s;
		filenames[i] = (char*) malloc(ent_len + 1);
		if (filenames[i] == 0) {
			/* if malloc fails, free the ones that worked */
			while(--i >= 0) free(filenames[i]);
                        free(filenames);
			return ENOMEM;
		}
		strncpy(filenames[i], s, ent_len);
		filenames[i][ent_len] = 0;
		if (*t == 0) {
			i++;
			break;
		}
	}
	/* cap the array */
	filenames[i] = 0;

	retval = profile_init(filenames, ret_profile);

	/* count back down and free the entries */
	while(--i >= 0) free(filenames[i]);
	free(filenames);

	return retval;
}
#else
KRB5_DLLIMP errcode_t KRB5_CALLCONV
profile_init_path(filelist, ret_profile)
	profile_filespec_list_t filelist;
	profile_t *ret_profile;
{
	return profile_init (filelist, ret_profile);
}
#endif

#if TARGET_OS_MAC && TARGET_API_MAC_CARBON
KRB5_DLLIMP errcode_t KRB5_CALLCONV
FSp_profile_init(files, ret_profile)
	const FSSpec* files;
	profile_t *ret_profile;
{
    UInt32		fileCount = 0;
    const FSSpec*	nextSpec;
    char**		pathArray = NULL;
    UInt32		i;
    errcode_t		retval = 0;

    for (nextSpec = files; ; nextSpec++) {
        if ((nextSpec -> vRefNum == 0) &&
            (nextSpec -> parID == 0) &&
            (StrLength (nextSpec -> name) == 0))
            break;
        fileCount++;
    }
    
    pathArray = malloc ((fileCount + 1) * sizeof (char*));
    if (pathArray == NULL) {
        retval = ENOMEM;
    }
    
    if (retval == 0) {
        for (i = 0; i < fileCount + 1; i++) {
            pathArray [i] = NULL;
        }
        
        for (i = 0; i < fileCount; i++) {
            OSErr err = FSpGetFullPOSIXPath (&files [i], &pathArray [i]);
            if (err == memFullErr) {
                retval = ENOMEM;
                break;
            } else if (err != noErr) {
                retval = ENOENT;
                break;
            }
        }
    }
    
    if (retval == 0) {
        retval = profile_init (pathArray, ret_profile);
    }
    
    if (pathArray != NULL) {
        for (i = 0; i < fileCount; i++) {
            if (pathArray [i] != 0)
                free (pathArray [i]);
        }
        free (pathArray);
    }
    
    return retval;
}

KRB5_DLLIMP errcode_t KRB5_CALLCONV
FSp_profile_init_path(files, ret_profile)
	const FSSpec* files;
	profile_t *ret_profile;
{
    return FSp_profile_init (files, ret_profile);
}
#endif

KRB5_DLLIMP errcode_t KRB5_CALLCONV
profile_flush(profile)
	profile_t	profile;
{
	if (!profile || profile->magic != PROF_MAGIC_PROFILE)
		return PROF_MAGIC_PROFILE;

	if (profile->first_file)
		return profile_flush_file_data(profile->first_file->data);

	return 0;
}

KRB5_DLLIMP void KRB5_CALLCONV
profile_abandon(profile)
	profile_t	profile;
{
	prf_file_t	p, next;

	if (!profile || profile->magic != PROF_MAGIC_PROFILE)
		return;

	for (p = profile->first_file; p; p = next) {
		next = p->next;
		profile_free_file(p);
	}
	profile->magic = 0;
	free(profile);
}

KRB5_DLLIMP void KRB5_CALLCONV
profile_release(profile)
	profile_t	profile;
{
	prf_file_t	p, next;

	if (!profile || profile->magic != PROF_MAGIC_PROFILE)
		return;

	for (p = profile->first_file; p; p = next) {
		next = p->next;
		profile_close_file(p);
	}
	profile->magic = 0;
	free(profile);
}

/*
 * Here begins the profile serialization functions.
 */
errcode_t profile_ser_size(unused, profile, sizep)
    const char *unused;
    profile_t	profile;
    size_t	*sizep;
{
    size_t	required;
    prf_file_t	pfp;

    required = 3*sizeof(prof_int32);
    for (pfp = profile->first_file; pfp; pfp = pfp->next) {
	required += sizeof(prof_int32);
#ifdef PROFILE_USES_PATHS
	if (pfp->data->filespec)
	    required += strlen(pfp->data->filespec);
#else
	required += sizeof (profile_filespec_t);
#endif
    }
    *sizep += required;
    return 0;
}

static void pack_int32(oval, bufpp, remainp)
    prof_int32		oval;
    unsigned char	**bufpp;
    size_t		*remainp;
{
    (*bufpp)[0] = (unsigned char) ((oval >> 24) & 0xff);
    (*bufpp)[1] = (unsigned char) ((oval >> 16) & 0xff);
    (*bufpp)[2] = (unsigned char) ((oval >> 8) & 0xff);
    (*bufpp)[3] = (unsigned char) (oval & 0xff);
    *bufpp += sizeof(prof_int32);
    *remainp -= sizeof(prof_int32);
}

errcode_t profile_ser_externalize(unused, profile, bufpp, remainp)
    const char		*unused;
    profile_t		profile;
    unsigned char	**bufpp;
    size_t		*remainp;
{
    errcode_t		retval;
    size_t		required;
    unsigned char	*bp;
    size_t		remain;
    prf_file_t		pfp;
    prof_int32		fcount, slen;

    required = 0;
    bp = *bufpp;
    remain = *remainp;
    retval = EINVAL;
    if (profile) {
	retval = ENOMEM;
	(void) profile_ser_size(unused, profile, &required);
	if (required <= remain) {
	    fcount = 0;
	    for (pfp = profile->first_file; pfp; pfp = pfp->next)
		fcount++;
	    pack_int32(PROF_MAGIC_PROFILE, &bp, &remain);
	    pack_int32(fcount, &bp, &remain);
	    for (pfp = profile->first_file; pfp; pfp = pfp->next) {
#ifdef PROFILE_USES_PATHS
		slen = (pfp->data->filespec) ?
		    (prof_int32) strlen(pfp->data->filespec) : 0;
		pack_int32(slen, &bp, &remain);
		if (slen) {
		    memcpy(bp, pfp->data->filespec, (size_t) slen);
		    bp += slen;
		    remain -= (size_t) slen;
		}
#else
		slen = sizeof (FSSpec);
		pack_int32(slen, &bp, &remain);
		memcpy (bp, &(pfp->data->filespec), (size_t) slen);
		bp += slen;
		remain -= (size_t) slen;
#endif
	    }
	    pack_int32(PROF_MAGIC_PROFILE, &bp, &remain);
	    retval = 0;
	    *bufpp = bp;
	    *remainp = remain;
	}
    }
    return(retval);
}

static int unpack_int32(intp, bufpp, remainp)
    prof_int32		*intp;
    unsigned char	**bufpp;
    size_t		*remainp;
{
    if (*remainp >= sizeof(prof_int32)) {
	*intp = (((prof_int32) (*bufpp)[0] << 24) |
		 ((prof_int32) (*bufpp)[1] << 16) |
		 ((prof_int32) (*bufpp)[2] << 8) |
		 ((prof_int32) (*bufpp)[3]));
	*bufpp += sizeof(prof_int32);
	*remainp -= sizeof(prof_int32);
	return 0;
    }
    else
	return 1;
}

errcode_t profile_ser_internalize(unused, profilep, bufpp, remainp)
	const char		*unused;
	profile_t		*profilep;
	unsigned char	**bufpp;
	size_t		*remainp;
{
	errcode_t		retval;
	unsigned char	*bp;
	size_t		remain;
	int			i;
	prof_int32		fcount, tmp;
	profile_filespec_t		*flist = 0;

	bp = *bufpp;
	remain = *remainp;

	if (remain >= 12)
		(void) unpack_int32(&tmp, &bp, &remain);
	else
		tmp = 0;
	
	if (tmp != PROF_MAGIC_PROFILE) {
		retval = EINVAL;
		goto cleanup;
	}
	
	(void) unpack_int32(&fcount, &bp, &remain);
	retval = ENOMEM;

	flist = (profile_filespec_t *) malloc(sizeof(profile_filespec_t) * (fcount + 1));
	if (!flist)
		goto cleanup;
	
	memset(flist, 0, sizeof(char *) * (fcount+1));
	for (i=0; i<fcount; i++) {
		if (!unpack_int32(&tmp, &bp, &remain)) {
#ifdef PROFILE_USES_PATHS
			flist[i] = (char *) malloc((size_t) (tmp+1));
			if (!flist[i])
				goto cleanup;
			memcpy(flist[i], bp, (size_t) tmp);
			flist[i][tmp] = '\0';
#else
			memcpy (&flist[i], bp, (size_t) tmp);
#endif
			bp += tmp;
			remain -= (size_t) tmp;
		}
	}

	if (unpack_int32(&tmp, &bp, &remain) ||
	    (tmp != PROF_MAGIC_PROFILE)) {
		retval = EINVAL;
		goto cleanup;
	}

	if ((retval = profile_init(flist, profilep)))
		goto cleanup;
	
	*bufpp = bp;
	*remainp = remain;
    
cleanup:
	if (flist) {
#ifdef PROFILE_USES_PATHS
		for (i=0; i<fcount; i++) {
			if (flist[i])
				free(flist[i]);
		}
#endif
		free(flist);
	}
	return(retval);
}
