#include "krb5.h"
	
#if TARGET_OS_MAC
#include <Kerberos/CredentialsCache2.h>
#endif

#if defined(_MSDOS) || defined(_WIN32)
#include "cacheapi.h"
#endif

#define kStringLiteralLen 255

/* globals to be exported */
extern krb5_cc_ops krb5_cc_stdcc_ops;

/*
 * structure to stash in the cache's data field
 */
typedef struct _stdccCacheData {
     	char *cache_name;
	ccache_p *NamedCache;
} stdccCacheData, *stdccCacheDataPtr;


/* function protoypes  */

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_close
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_destroy 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_end_seq_get 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id , krb5_cc_cursor *cursor ));

KRB5_DLLIMP void KRB5_CALLCONV krb5_stdcc_shutdown 
        KRB5_PROTOTYPE((void));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_generate_new 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache *id ));

KRB5_DLLIMP char * KRB5_CALLCONV krb5_stdcc_get_name 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_get_principal 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id , krb5_principal *princ ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_initialize 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id , krb5_principal princ ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_next_cred 
        KRB5_PROTOTYPE((krb5_context, 
		   krb5_ccache id , 
		   krb5_cc_cursor *cursor , 
		   krb5_creds *creds ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_resolve 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache *id , const char *residual ));
     
KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_retrieve 
        KRB5_PROTOTYPE((krb5_context, 
		   krb5_ccache id , 
		   krb5_flags whichfields , 
		   krb5_creds *mcreds , 
		   krb5_creds *creds ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_start_seq_get 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id , krb5_cc_cursor *cursor ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_store 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id , krb5_creds *creds ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_set_flags 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id , krb5_flags flags ));

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV krb5_stdcc_remove 
        KRB5_PROTOTYPE((krb5_context, krb5_ccache id , krb5_flags flags, krb5_creds *creds));
