/*
	memcache.h
		Kerberos credential store in memory
		Originally coded by Tim Miller / Brown University
		Mods 1/92 By Peter Bosanko

		Modified May-June 1994 by Julia Menapace and John Gilmore,
		Cygnus Support.
*/

#if defined(unix) || defined(_WINDOWS)
#define _nmalloc malloc
#define _nfree free
#define _nrealloc realloc

#define noErr 0
#define memFullErr ENOMEM

#define OFFSETOF(x) x

typedef char *	NPSTR
typedef char **	Handle;
typedef int	Size;
typedef int	OSErr;

#define WM_KERBEROS_CHANGED	"Kerberos Changed"
#endif /* unix || _WINDOWS */

struct Session {
	char		name[ANAME_SZ];
	char		instance[INST_SZ];
	char		realm[REALM_SZ];
	int		numcreds;
	CREDENTIALS	**creds;
};
typedef struct Session Session;

OSErr		GetNumSessions PROTOTYPE ((int *n));
OSErr		GetNthSession PROTOTYPE ((const int n, char *name,
					  char *instance, char *realm));
OSErr		DeleteSession PROTOTYPE ((const char *name,
					  const char *instance,
					  const char *realm));

OSErr		GetCredentials PROTOTYPE ((const char *name,
					   const char *instance,
					   const char *realm,
					   CREDENTIALS *cr));	
		/* name, instance, and realm of service wanted should be
		   set in *cr before calling */
OSErr		AddCredentials PROTOTYPE ((const char *name,
					   const char *instance,
					   const char *realm,
					   const CREDENTIALS *cr));
OSErr		DeleteCredentials PROTOTYPE ((const char *uname,
					      const char *uinst,
					      const char *urealm,
					      const char *sname,
					      const char *sinst,
					      const char *srealm));
OSErr		GetNumCredentials PROTOTYPE ((const char *name,
					      const char *instance,
					      const char *realm, int *n));
OSErr		GetNthCredentials PROTOTYPE ((const char *uname,
					      const char *uinst,
					      const char *urealm, char *sname,
					      char *sinst, char *srealm,
					      const int n));
