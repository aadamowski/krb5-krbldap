/*
 * Copyright 1993 OpenVision Technologies, Inc., All Rights Reserved.
 *
 * $Id$
 * $Source$
 * 
 * $Log$
 * Revision 1.5.2.1  1996/06/20 02:16:53  marc
 * File added to the repository on a branch
 *
 * Revision 1.5  1996/05/17  21:36:50  bjaspan
 * rename to kadm5, begin implementing version 2
 *
 * Revision 1.4  1996/05/16 21:45:51  bjaspan
 * u_int32 -> long, add krb5_context
 *
 * Revision 1.3  1994/09/20 16:25:05  bjaspan
 * [secure-admin/2436: API versioning fixes to various admin files]
 * [secure-releng/2502: audit secure-admin/2436: random API versioning fixes]
 *
 * Sandbox:
 *
 *  Unnecessary variable initialization removed.
 *
 * Revision 1.3  1994/09/12  20:26:39  jik
 * Unnecessary variable initialization removed.
 *
 * Revision 1.2  1994/08/16  18:52:02  jik
 * Versioning changes.
 *
 * Revision 1.1  1993/11/10  23:10:39  bjaspan
 * Initial revision
 *
 */

#if !defined(lint) && !defined(__CODECENTER__)
static char *rcsid = "$Header$";
#endif

#include    <rpc/rpc.h>
#include    <kadm5/admin.h>
#include    <kadm5/kadm_rpc.h>
#include    "client_internal.h"

kadm5_ret_t kadm5_get_privs(void *server_handle, long *privs)
{
     getprivs_ret *r;
     kadm5_server_handle_t handle = server_handle;

     r = get_privs_1(&handle->api_version, handle->clnt);
     if (r == NULL)
	  return KADM5_RPC_ERROR;
     else if (r->code == KADM5_OK)
	  *privs = r->privs;
     return r->code;
}
