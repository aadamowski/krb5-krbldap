/*
 * util/et/error_message.c
 *
 * Copyright 1987 by the Student Information Processing Board
 * of the Massachusetts Institute of Technology
 *
 * For copyright info, see "mit-sipb-copyright.h".
 */

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>
#include "com_err.h"
#include "error_table.h"
#include "mit-sipb-copyright.h"
#include "internal.h"

#ifdef _MACINTOSH
#define sys_nerr 100
#endif

static const char copyright[] =
    "Copyright 1986, 1987, 1988 by the Student Information Processing Board\nand the department of Information Systems\nof the Massachusetts Institute of Technology";

static char buffer[25];

struct et_list * _et_list = (struct et_list *) NULL;

KRB5_DLLIMP const char * KRB5_CALLCONV error_message (code)
long code;
{
    int offset;
    long l_offset;
    struct et_list *et;
    long table_num;
    int started = 0;
    char *cp;

#if defined(_MSDOS) || defined(_WIN32)
#define HAVE_STRERROR 1
/*
** Winsock defines errors in the range 10000-10100. These are equivalent
** to 10000 plus the Berkeley error numbers.
*
* (Does windows strerror() work right here?)
*
* XXX NO.  We need to do our own table lookup for Winsock error
* messages!!!  --- TYT
* 
*/
    if (code >= 10000 && code <= 10100)		/* Is it Winsock error? */
	code -= 10000;				/* Turn into Berkeley errno */
#endif

    l_offset = code & ((1<<ERRCODE_RANGE)-1);
    offset = (int) l_offset;
    table_num = code - l_offset;
    if (!table_num) {
#ifdef HAVE_STRERROR
	cp = strerror(offset);
	if (cp)
	    return cp;
	goto oops;
#else
#ifdef HAVE_SYSERRLIST
        if (offset < sys_nerr)
	    return(sys_errlist[offset]);
	else
	    goto oops;
#else
		goto oops;
#endif /* HAVE_SYSERRLIST */
#endif /* HAVE_STRERROR */
    }
    for (et = _et_list; et; et = et->next) {
	if (et->table->base == table_num) {
	    /* This is the right table */
	    if (et->table->n_msgs <= offset)
		goto oops;
	    return(et->table->msgs[offset]);
	}
    }
oops:
    strcpy (buffer, "Unknown code ");
    if (table_num) {
	strcat (buffer, error_table_name (table_num));
	strcat (buffer, " ");
    }
    for (cp = buffer; *cp; cp++)
	;
    if (offset >= 100) {
	*cp++ = '0' + offset / 100;
	offset %= 100;
	started++;
    }
    if (started || offset >= 10) {
	*cp++ = '0' + offset / 10;
	offset %= 10;
    }
    *cp++ = '0' + offset;
    *cp = '\0';
    return(buffer);
}
