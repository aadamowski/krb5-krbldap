/*
Copyright 1990, Daniel J. Bernstein. All rights reserved.

Please address any questions or comments to the author at brnstnd@acf10.nyu.edu.
*/

#ifndef KRB5_RC_IO_H
#define KRB5_RC_IO_H
#include "krb5/krb5.h"
#include "krb5/osconf.h"

typedef struct krb5_rc_iostuff
 {
  int fd;
  int mark; /* on newer systems, should be pos_t */
  char *fn;
 }
krb5_rc_iostuff;

/* first argument is always iostuff for result file */

krb5_error_code krb5_rc_io_creat PROTOTYPE((krb5_rc_iostuff *,char **));
krb5_error_code krb5_rc_io_open PROTOTYPE((krb5_rc_iostuff *,char *));
krb5_error_code krb5_rc_io_move PROTOTYPE((krb5_rc_iostuff *,krb5_rc_iostuff *));
krb5_error_code krb5_rc_io_write PROTOTYPE((krb5_rc_iostuff *,krb5_pointer,int));
krb5_error_code krb5_rc_io_read PROTOTYPE((krb5_rc_iostuff *,krb5_pointer,int));
krb5_error_code krb5_rc_io_close PROTOTYPE((krb5_rc_iostuff *));
krb5_error_code krb5_rc_io_destroy PROTOTYPE((krb5_rc_iostuff *));
krb5_error_code krb5_rc_io_mark PROTOTYPE((krb5_rc_iostuff *));
krb5_error_code krb5_rc_io_unmark PROTOTYPE((krb5_rc_iostuff *));

#endif
