/*
Copyright 1990, Daniel J. Bernstein. All rights reserved.

Please address any questions or comments to the author at brnstnd@acf10.nyu.edu.
*/

#include <sys/types.h>
#include <sys/file.h>
#ifdef BSD
#include <limits.h>
#endif
#include <string.h>
#include <stdio.h> /* for P_tmpdir */
#include <malloc.h>
#include <errno.h>
extern int errno; /* this should be in errno.h, but isn't on some systems */
#define FREE(x) ((void) free((char *) (x)))

#include "rc_io.h"
#include "krb5/krb5_err.h"

#define UNIQUE1 getpid() /* hopefully unique number */
#define UNIQUE2 time(0) /* hopefully unique number */

int dirlen = 0;
char *dir;

#define GETDIR { if (!dirlen) getdir(); }

static void getdir()
{
 if (!dirlen)
  {
   if (!(dir = getenv("KRB5RCACHEDIR")))
     if (!(dir = getenv("TMPDIR")))
#ifdef RCTMPDIR
       dir = RCTMPDIR;
#else
       dir = "/tmp";
#endif
   dirlen = strlen(dir) + 1;
  }
}

krb5_error_code krb5_rc_io_creat (d, fn)
krb5_rc_iostuff *d;
char **fn;
{
 char *c;

 GETDIR;
 if (fn && *fn)
  {
   if (!(d->fn = malloc(strlen(*fn) + dirlen + 1)))
     return KRB5_RC_IO_MALLOC;
   (void) strcpy(d->fn,dir);
   (void) strcat(d->fn,"/");
   (void) strcat(d->fn,*fn);
   d->fd = open(d->fn,O_WRONLY | O_CREAT | O_TRUNC,0600);
  }
 else
  {
      /* each %d's is max 11 digits (-, 10 digits of 32-bit number)
	 11 * 2 = 22, + /krb5_RC + aaa = 33, +2 for slop */
   if (!(d->fn = malloc(35 + dirlen)))
     return KRB5_RC_IO_MALLOC;
   if (fn)
     if (!(*fn = malloc(35)))
      { FREE(d->fn); return KRB5_RC_IO_MALLOC; }
   (void) sprintf(d->fn,"%s/krb5_RC%d%d",dir,UNIQUE1,UNIQUE2);
   c = d->fn + strlen(d->fn);
   (void) strcpy(c,"aaa");
   while ((d->fd = open(d->fn,O_WRONLY | O_CREAT | O_TRUNC,0600)) == -1)
    {
     if ((c[2]++) == 'z')
      {
       c[2] = 'a';
       if ((c[1]++) == 'z')
	{
         c[1] = 'a';
         if ((c[0]++) == 'z')
           break; /* sigh */
        }
      }
    }
   if (fn)
     (void) strcpy(*fn,d->fn + dirlen);
  }
 if (d->fd == -1)
   switch(errno)
    {
     case EBADF: FREE(d->fn); return KRB5_RC_IO_UNKNOWN; break;
     case EFBIG: FREE(d->fn); return KRB5_RC_IO_SPACE; break;
     case EDQUOT: FREE(d->fn); return KRB5_RC_IO_SPACE; break;
     case ENOSPC: FREE(d->fn); return KRB5_RC_IO_SPACE; break;
     case EIO: FREE(d->fn); return KRB5_RC_IO_IO; break;
     case EPERM: FREE(d->fn); return KRB5_RC_IO_PERM; break;
     case EACCES: FREE(d->fn); return KRB5_RC_IO_PERM; break;
     case EROFS: FREE(d->fn); return KRB5_RC_IO_PERM; break;
     default: FREE(d->fn); return KRB5_RC_IO_UNKNOWN; break;
    }
 return 0;
}

krb5_error_code krb5_rc_io_open (d, fn)
krb5_rc_iostuff *d;
char *fn;
{
 GETDIR;
 if (!(d->fn = malloc(strlen(fn) + dirlen + 1)))
   return KRB5_RC_IO_MALLOC;
 (void) strcpy(d->fn,dir);
 (void) strcat(d->fn,"/");
 (void) strcat(d->fn,fn);
 d->fd = open(d->fn,O_RDWR,0600);
 if (d->fd == -1)
   switch(errno)
    {
     case EBADF: FREE(d->fn); return KRB5_RC_IO_UNKNOWN; break;
     case EFBIG: FREE(d->fn); return KRB5_RC_IO_SPACE; break;
     case EDQUOT: FREE(d->fn); return KRB5_RC_IO_SPACE; break;
     case ENOSPC: FREE(d->fn); return KRB5_RC_IO_SPACE; break;
     case EIO: FREE(d->fn); return KRB5_RC_IO_IO; break;
     case EPERM: FREE(d->fn); return KRB5_RC_IO_PERM; break;
     case EACCES: FREE(d->fn); return KRB5_RC_IO_PERM; break;
     case EROFS: FREE(d->fn); return KRB5_RC_IO_PERM; break;
     default: FREE(d->fn); return KRB5_RC_IO_UNKNOWN; break;
    }
 return 0;
}

krb5_error_code krb5_rc_io_move (new, old)
krb5_rc_iostuff *new;
krb5_rc_iostuff *old;
{
 if (rename(old->fn,new->fn) == -1) /* MUST be atomic! */
   return KRB5_RC_IO_UNKNOWN;
 (void) krb5_rc_io_close(new);
 new->fn = old->fn;
 new->fd = old->fd;
 return 0;
}

krb5_error_code krb5_rc_io_write (d, buf, num)
krb5_rc_iostuff *d;
krb5_pointer buf;
int num;
{
 if (write(d->fd,(char *) buf,num) == -1)
   switch(errno)
    {
     case EBADF: return KRB5_RC_IO_UNKNOWN; break;
     case EFBIG: return KRB5_RC_IO_SPACE; break;
     case EDQUOT: return KRB5_RC_IO_SPACE; break;
     case ENOSPC: return KRB5_RC_IO_SPACE; break;
     case EIO: return KRB5_RC_IO_IO; break;
     default: return KRB5_RC_IO_UNKNOWN; break;
    }
 if (fsync(d->fd) == -1)
   switch(errno)
    {
     case EBADF: return KRB5_RC_IO_UNKNOWN; break;
     case EIO: return KRB5_RC_IO_IO; break;
     default: return KRB5_RC_IO_UNKNOWN; break;
    }
 return 0;
}

krb5_error_code krb5_rc_io_read (d, buf, num)
krb5_rc_iostuff *d;
krb5_pointer buf;
int num;
{
 int count;
 if ((count = read(d->fd,(char *) buf,num)) == -1)
   switch(errno)
    {
     case EBADF: return KRB5_RC_IO_UNKNOWN; break;
     case EIO: return KRB5_RC_IO_IO; break;
     default: return KRB5_RC_IO_UNKNOWN; break;
    }
 if (count == 0)
     return KRB5_RC_IO_EOF;
 return 0;
}

krb5_error_code krb5_rc_io_close (d)
krb5_rc_iostuff *d;
{
 FREE(d->fn);
 if (close(d->fd) == -1) /* can't happen */
   return KRB5_RC_IO_UNKNOWN;
 return 0;
}

krb5_error_code krb5_rc_io_destroy (d)
krb5_rc_iostuff *d;
{
 if (unlink(d->fn) == -1)
   switch(errno)
    {
     case EBADF: return KRB5_RC_IO_UNKNOWN; break;
     case EIO: return KRB5_RC_IO_IO; break;
     case EPERM: return KRB5_RC_IO_PERM; break;
     case EBUSY: return KRB5_RC_IO_PERM; break;
     case EROFS: return KRB5_RC_IO_PERM; break;
     default: return KRB5_RC_IO_UNKNOWN; break;
    }
 return 0;
}

krb5_error_code krb5_rc_io_mark (d)
krb5_rc_iostuff *d;
{
 d->mark = lseek(d->fd,0,L_INCR); /* can't fail */
 return 0;
}

krb5_error_code krb5_rc_io_unmark (d)
krb5_rc_iostuff *d;
{
 (void) lseek(d->fd,d->mark,L_SET); /* if it fails, tough luck */
 return 0;
}
