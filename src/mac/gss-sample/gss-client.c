/*
 * Copyright 1994 by OpenVision Technologies, Inc.
 * 
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 * 
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#define GSSAPI_V2

#include "gss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

usage()
{
     fprintf(stderr, "Usage: gss-client [-port port] [-v2] host service msg\r");
     exit(1);
}

int main(argc, argv)
     int argc;
     char **argv;
{
     char *service_name, *server_host, *msg;
     u_short port = 4444;
     int v2 = 0;
     
     argc = ccommand(&argv);
     
     /* Parse arguments. */
     argc--; argv++;
     while (argc) {
	  if (strcmp(*argv, "-port") == 0) {
	       argc--; argv++;
	       if (!argc) usage();
	       port = atoi(*argv);
	  } else if (strcmp(*argv, "-v2") == 0) {
	       v2 = 1;
	  } else 
	       break;
	  argc--; argv++;
     }
     if (argc != 3)
	  usage();

     server_host = *argv++;
     service_name = *argv++;
     msg = *argv++;

     if (call_server(server_host, port, v2, service_name, msg) < 0)
	  exit(1);

     return 0;
}

/*
 * Function: call_server
 *
 * Purpose: Call the "sign" service.
 *
 * Arguments:
 *
 * 	host		(r) the host providing the service
 * 	port		(r) the port to connect to on host
 * 	service_name	(r) the GSS-API service name to authenticate to	
 * 	msg		(r) the message to have "signed"
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 * 
 * call_server opens a TCP connection to <host:port> and establishes a
 * GSS-API context with service_name over the connection.  It then
 * seals msg in a GSS-API token with gss_seal, sends it to the server,
 * reads back a GSS-API signature block for msg from the server, and
 * verifies it with gss_verify.  -1 is returned if any step fails,
 * otherwise 0 is returned.
 */
int call_server(char *host, u_short port, int dov2, char *service_name, char *msg)
{
     gss_ctx_id_t context;
     gss_buffer_desc in_buf, out_buf, context_token;
     int state;
     SOCKET s;
     OM_uint32 maj_stat, min_stat;
     gss_name_t	src_name, targ_name;
     gss_buffer_desc sname, tname;
     OM_uint32 lifetime;
     gss_OID mechanism;
     int is_local;
#ifdef	GSSAPI_V2
     OM_uint32 context_flags;
     int is_open;
     gss_qop_t qop_state;
     gss_OID_set mech_names;
     gss_buffer_desc oid_name;
#else	/* GSSAPI_V2 */
     int context_flags;
#endif	/* GSSAPI_V2 */
     

     /* Open connection */
     if ((s = connect_to_server(host, port)) == (SOCKET) -1)
	  return -1;

     /* Establish context */
     if (client_establish_context(s, service_name, &context) < 0)
	  return -1;

#ifdef	GSSAPI_V2
     if (dov2) {
	 /*
	  * Attempt to save and then restore the context.
	  */
	 maj_stat = gss_export_sec_context(&min_stat,
					   &context,
					   &context_token);
	 if (maj_stat != GSS_S_COMPLETE) {
	     display_status("exporting context", maj_stat, min_stat);
	     return -1;
	 }
	 maj_stat = gss_import_sec_context(&min_stat,
					   &context_token,
					   &context);
	 if (maj_stat != GSS_S_COMPLETE) {
	     display_status("importing context", maj_stat, min_stat);
	     return -1;
	 }
	 (void) gss_release_buffer(&min_stat, &context_token);
     }
#endif	/* GSSAPI_V2 */

     /* Get context information */
     maj_stat = gss_inquire_context(&min_stat, context,
				    &src_name, &targ_name, &lifetime,
				    &mechanism, &context_flags,
				    &is_local
#ifdef	GSSAPI_V2
				    , &is_open
#endif	/* GSSAPI_V2 */
				    );
     if (maj_stat != GSS_S_COMPLETE) {
	 display_status("inquiring context", maj_stat, min_stat);
	 return -1;
     }

     maj_stat = gss_display_name(&min_stat, src_name, &sname,
				 (gss_OID *) NULL);
     if (maj_stat != GSS_S_COMPLETE) {
	 display_status("displaying context", maj_stat, min_stat);
	 return -1;
     }
     maj_stat = gss_display_name(&min_stat, targ_name, &tname,
				 (gss_OID *) NULL);
     if (maj_stat != GSS_S_COMPLETE) {
	 display_status("displaying context", maj_stat, min_stat);
	 return -1;
     }
     fprintf(stderr, "\"%s\" to \"%s\"\r   lifetime %d, flags %x, %s",
	     sname.value, tname.value, lifetime, context_flags,
	     (is_local) ? "locally initiated" : "remotely initiated");
#ifdef	GSSAPI_V2
     fprintf(stderr, " %s", (is_open) ? "open" : "closed");
#endif	/* GSSAPI_V2 */
     fprintf(stderr, "\r");

     (void) gss_release_name(&min_stat, &src_name);
     (void) gss_release_name(&min_stat, &targ_name);
     (void) gss_release_buffer(&min_stat, &sname);
     (void) gss_release_buffer(&min_stat, &tname);

#ifdef	GSSAPI_V2
     if (dov2) {
	 size_t	i;

	 /* Now get the names supported by the mechanism */
	 maj_stat = gss_inquire_names_for_mech(&min_stat,
					       mechanism,
					       &mech_names);
	 if (maj_stat != GSS_S_COMPLETE) {
	     display_status("inquiring mech names", maj_stat, min_stat);
	     return -1;
	 }

	 maj_stat = gss_oid_to_str(&min_stat,
				   mechanism,
				   &oid_name);
	 if (maj_stat != GSS_S_COMPLETE) {
	     display_status("converting oid->string", maj_stat, min_stat);
	     return -1;
	 }
	 fprintf(stderr, "Mechanism %s supports %d names\r",
		 oid_name.value, mech_names->count);
	 (void) gss_release_buffer(&min_stat, &oid_name);
	 for (i=0; i<mech_names->count; i++) {
	     gss_OID	tmpoid;
	     int	is_present;

	     maj_stat = gss_oid_to_str(&min_stat,
				       &mech_names->elements[i],
				       &oid_name);
	     if (maj_stat != GSS_S_COMPLETE) {
		 display_status("converting oid->string", maj_stat, min_stat);
		 return -1;
	     }
	     fprintf(stderr, "%d: %s\r", i, oid_name.value);

	     maj_stat = gss_str_to_oid(&min_stat,
				       &oid_name,
				       &tmpoid);
	     if (maj_stat != GSS_S_COMPLETE) {
		 display_status("converting string->oid", maj_stat, min_stat);
		 return -1;
	     }

	     maj_stat = gss_test_oid_set_member(&min_stat,
						tmpoid,
						mech_names,
						&is_present);
	     if (maj_stat != GSS_S_COMPLETE) {
		 display_status("testing oid presence", maj_stat, min_stat);
		 return -1;
	     }
	     if (!is_present) {
		 fprintf(stderr, "%s is not present in list?\r",
			 oid_name.value);
	     }
	     (void) gss_release_oid(&min_stat, &tmpoid);
	     (void) gss_release_buffer(&min_stat, &oid_name);
	 }

	 (void) gss_release_oid_set(&min_stat, &mech_names);
	 (void) gss_release_oid(&min_stat, &mechanism);
     }
#endif	/* GSSAPI_V2 */

     /* Seal the message */
     in_buf.value = msg;
     in_buf.length = strlen(msg) + 1;
#ifdef	GSSAPI_V2
     if (dov2)
	 maj_stat = gss_wrap(&min_stat, context, 1, GSS_C_QOP_DEFAULT,
			     &in_buf, &state, &out_buf);
     else
#endif	/* GSSAPI_V2 */
     maj_stat = gss_seal(&min_stat, context, 1, GSS_C_QOP_DEFAULT,
			 &in_buf, &state, &out_buf);
     if (maj_stat != GSS_S_COMPLETE) {
	  display_status("sealing message", maj_stat, min_stat);
	  return -1;
     } else if (! state) {
	  fprintf(stderr, "Warning!  Message not encrypted.\r");
     }

     /* Send to server */
     if (send_token(s, &out_buf) < 0)
	  return -1;
     (void) gss_release_buffer(&min_stat, &out_buf);

     /* Read signature block into out_buf */
     if (recv_token(s, &out_buf) < 0)
	  return -1;

     /* Verify signature block */
#ifdef	GSSAPI_V2
     if (dov2)
	 maj_stat = gss_verify_mic(&min_stat, context, &in_buf,
				   &out_buf, &qop_state);
     else
#endif	/* GSSAPI_V2 */
     maj_stat = gss_verify(&min_stat, context, &in_buf, &out_buf, &state);
     if (maj_stat != GSS_S_COMPLETE) {
	  display_status("verifying signature", maj_stat, min_stat);
	  return -1;
     }
     (void) gss_release_buffer(&min_stat, &out_buf);

     printf("Signature verified.\r");

     /* Delete context */
     maj_stat = gss_delete_sec_context(&min_stat, &context, &out_buf);
     if (maj_stat != GSS_S_COMPLETE) {
	  display_status("deleting context", maj_stat, min_stat);
	  return -1;
     }
     (void) gss_release_buffer(&min_stat, &out_buf);
     
     closesocket(s);
     
     return 0;
}

/*
 * Function: connect_to_server
 *
 * Purpose: Opens a TCP connection to the name host and port.
 *
 * Arguments:
 *
 * 	host		(r) the target host name
 * 	port		(r) the target port, in host byte order
 *
 * Returns: the established socket file desciptor, or -1 on failure
 *
 * Effects:
 *
 * The host name is resolved with gethostbyname(), and the socket is
 * opened and connected.  If an error occurs, an error message is
 * displayed and -1 is returned.
 */
SOCKET connect_to_server(char *host, u_short port)
{
     struct sockaddr_in saddr;
     struct hostent *hp;
     SOCKET s;
     
     if ((hp = gethostbyname(host)) == NULL) {
	  fprintf(stderr, "Unknown host: %s\r", host);
	  return (SOCKET) -1;
     }
     
     saddr.sin_family = hp->h_addrtype;
     memcpy((char *)&saddr.sin_addr, hp->h_addr, sizeof(saddr.sin_addr));
     saddr.sin_port = htons(port);

     if ((s = socket(AF_INET, SOCK_STREAM, 0)) == (SOCKET) -1) {
	  perror("creating socket");
	  return (SOCKET) -1;
     }
     if (connect(s, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
	  perror("connecting to server");
	  return (SOCKET) -1;
     }
     return s;
}


/*
 * Function: client_establish_context
 *
 * Purpose: establishes a GSS-API context with a specified service and
 * returns the context handle
 *
 * Arguments:
 *
 * 	s		(r) an established TCP connection to the service
 * 	service_name	(r) the ASCII service name of the service
 * 	context		(w) the established GSS-API context
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 * 
 * service_name is imported as a GSS-API name and a GSS-API context is
 * established with the corresponding service; the service should be
 * listening on the TCP connection s.  The default GSS-API mechanism
 * is used, and mutual authentication and replay detection are
 * requested.
 * 
 * If successful, the context handle is returned in context.  If
 * unsuccessful, the GSS-API error messages are displayed on stderr
 * and -1 is returned.
 */
int client_establish_context(SOCKET s, char *service_name, gss_ctx_id_t *gss_context)
{
     gss_buffer_desc send_tok, recv_tok, *token_ptr;
     gss_name_t target_name;
     OM_uint32 maj_stat, min_stat;

     /*
      * Import the name into target_name.  Use send_tok to save
      * local variable space.
      */
     send_tok.value = service_name;
     send_tok.length = strlen(service_name) + 1;
     maj_stat = gss_import_name(&min_stat, &send_tok,
				(gss_OID) gss_nt_service_name, &target_name);
     if (maj_stat != GSS_S_COMPLETE) {
	  display_status("parsing name", maj_stat, min_stat);
	  return -1;
     }
     
     /*
      * Perform the context-establishement loop.
      *
      * On each pass through the loop, token_ptr points to the token
      * to send to the server (or GSS_C_NO_BUFFER on the first pass).
      * Every generated token is stored in send_tok which is then
      * transmitted to the server; every received token is stored in
      * recv_tok, which token_ptr is then set to, to be processed by
      * the next call to gss_init_sec_context.
      * 
      * GSS-API guarantees that send_tok's length will be non-zero
      * if and only if the server is expecting another token from us,
      * and that gss_init_sec_context returns GSS_S_CONTINUE_NEEDED if
      * and only if the server has another token to send us.
      */
     
     token_ptr = GSS_C_NO_BUFFER;
     *gss_context = GSS_C_NO_CONTEXT;

     do {
	  maj_stat =
	       gss_init_sec_context(&min_stat,
				    GSS_C_NO_CREDENTIAL,
				    gss_context,
				    target_name,
				    GSS_C_NULL_OID,
				    GSS_C_MUTUAL_FLAG | GSS_C_REPLAY_FLAG,
				    0,
				    NULL,	/* no channel bindings */
				    token_ptr,
				    NULL,	/* ignore mech type */
				    &send_tok,
				    NULL,	/* ignore ret_flags */
				    NULL);	/* ignore time_rec */

	  if (token_ptr != GSS_C_NO_BUFFER)
	       (void) gss_release_buffer(&min_stat, &recv_tok);

	  if (maj_stat!=GSS_S_COMPLETE && maj_stat!=GSS_S_CONTINUE_NEEDED) {
	       display_status("initializing context", maj_stat, min_stat);
	       (void) gss_release_name(&min_stat, &target_name);
	       return -1;
	  }

	  if (send_tok.length != 0) {
	       if (send_token(s, &send_tok) < 0) {
		    (void) gss_release_buffer(&min_stat, &send_tok);
		    (void) gss_release_name(&min_stat, &target_name);
		    return -1;
	       }
	  }
	  (void) gss_release_buffer(&min_stat, &send_tok);
	  
	  if (maj_stat == GSS_S_CONTINUE_NEEDED) {
	       if (recv_token(s, &recv_tok) < 0) {
		    (void) gss_release_name(&min_stat, &target_name);
		    return -1;
	       }
	       token_ptr = &recv_tok;
	  }
     } while (maj_stat == GSS_S_CONTINUE_NEEDED);

     (void) gss_release_name(&min_stat, &target_name);
     return 0;
}
