OM_uint32 gss_wrap(OM_uint32 *, gss_ctx_id_t, int, gss_qop_t, gss_buffer_t, int *, gss_buffer_t);
OM_uint32 gss_release_buffer(OM_uint32 *, gss_buffer_t);
OM_uint32 gss_unwrap(OM_uint32 *, gss_ctx_id_t, gss_buffer_t, gss_buffer_t, int *, gss_qop_t *);
OM_uint32 gss_delete_sec_context(OM_uint32 *, gss_ctx_id_t *, gss_buffer_t);
OM_uint32 gss_display_status(OM_uint32 *, OM_uint32, int, gss_OID, OM_uint32 *, gss_buffer_t);
OM_uint32 gss_init_sec_context(OM_uint32 *, gss_cred_id_t, gss_ctx_id_t *, gss_name_t, gss_OID, OM_uint32, OM_uint32, gss_channel_bindings_t, gss_buffer_t, gss_OID *, gss_buffer_t, OM_uint32 *, OM_uint32 *);
OM_uint32 gss_import_name(OM_uint32 *, gss_buffer_t, gss_OID, gss_name_t *);
OM_uint32 gss_release_name(OM_uint32 *, gss_name_t *);
OM_uint32 gss_wrap_size_limit(OM_uint32 *, gss_ctx_id_t, int, gss_qop_t, OM_uint32, OM_uint32 *);
