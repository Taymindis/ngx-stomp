
#include "ngx_http_stomp_module.h"

// #define ACK_MESSAGEID_1_0_1_1 (u_char*) "message-id"
// #define ACK_MESSAGEID_1_2  (u_char*)"id"
// static const u_char * acknowledge_msg_id = ACK_MESSAGEID_1_2;
static ngx_int_t ngx_http_stomp_upstream_upstream_init(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *uscf);
static ngx_int_t ngx_http_stomp_upstream_upstream_init_peer(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *uscf);
static ngx_int_t ngx_stomp_upstream_get_peer(ngx_peer_connection_t *pc, void *data);
static ngx_int_t ngx_http_stomp_parse_direct_frame_message(ngx_http_request_t *, ngx_http_stomp_loc_conf_t*, ngx_http_stomp_ctx_t *, ngx_http_stomp_sess_t*, ngx_str_t *);
static ngx_int_t ngx_http_stomp_parse_frame(ngx_http_request_t *, ngx_http_stomp_loc_conf_t*, ngx_http_stomp_ctx_t *, ngx_http_stomp_sess_t*, ngx_str_t *);
static ngx_int_t ngx_http_stomp_parse_frame_headers(ngx_http_request_t *, cstmp_frame_t *);
static ngx_int_t ngx_http_stomp_gen_chain(ngx_http_request_t *, ngx_http_stomp_ctx_t*, cstmp_frame_t*);
static ngx_int_t ngx_stomp_upstream_done(ngx_http_request_t *, ngx_http_upstream_t *, ngx_int_t);
static void ngx_stomp_upstream_finalize_request(ngx_http_request_t *r, ngx_http_upstream_t *u, ngx_int_t rc);
static void ngx_stomp_upstream_free_peer(ngx_peer_connection_t *pc, void *data, ngx_uint_t state);
static void ngx_stomp_keepalive_dummy_handler(ngx_event_t *ev);

/** Connect to Endpoint **/
static char *ngx_http_stomp_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_stomp_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_stomp_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_stomp_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_stomp_filter_init(void *data);
static ngx_int_t ngx_http_stomp_non_buffered_filter(void *data, ssize_t bytes);
static void ngx_http_stomp_abort_request(ngx_http_request_t *r);
static void ngx_http_stomp_finalize_request(ngx_http_request_t *r, ngx_int_t rc);

static void *ngx_http_stomp_upstream_create_srv_conf(ngx_conf_t *cf);
static void *ngx_http_stomp_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_stomp_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static void ngx_stomp_upstream_rev_handler(ngx_http_request_t *r, ngx_http_upstream_t *u);
static void ngx_stomp_upstream_wev_handler(ngx_http_request_t *r, ngx_http_upstream_t *u);

/**Command and config **/
static char *ngx_conf_stomp_stomp_command_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_conf_stomp_headers_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_conf_stomp_body_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_conf_stomp_frame_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_stomp_subscribe_queue_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_stomp_upstream_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void ngx_http_stomp_upstream_cleanup(void *data);
// static void ngx_http_stomp_debug_frame(ngx_http_request_t *r, u_char *str, size_t len);
static void ngx_http_stomp_dump_frame(ngx_http_request_t *r, cstmp_frame_t*);

#define ngx_http_stomp_send(stss, fr) ({\
ngx_int_t rc;\
if(stss->is_direct_frame) {\
 rc = cstmp_send_direct(stss->sess, fr->body.start, 1);\
}else {\
 rc = cstmp_send(stss->sess, fr, 1);\
}\
rc;})

#define ngx_http_stomp_recv(stss, fr) cstmp_recv(stss->sess, fr, 1)

#define ngx_http_stomp_add_read_write_event(_c, _rw_type, _rw_ev ) \
if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {\
    if (ngx_add_conn(_c) != NGX_OK) {\
        goto STOMP_CONN_ERROR;\
    }\
} else if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {\
    if (ngx_add_event(_rw_type, _rw_ev, NGX_CLEAR_EVENT) != NGX_OK) {\
        goto STOMP_CONN_ERROR;\
    }\
} else {\
    if (ngx_add_event(_rw_type, _rw_ev, NGX_LEVEL_EVENT) != NGX_OK) {\
        goto STOMP_CONN_ERROR;\
    }\
}

#define ngx_stomp_register_read_write_event_model \
if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {\
    if (ngx_add_conn(stmpc) != NGX_OK) {\
        goto STOMP_CONN_ERROR;\
    }\
} else if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {\
    if (ngx_add_event(rev, NGX_READ_EVENT, NGX_CLEAR_EVENT) != NGX_OK) {\
        goto STOMP_CONN_ERROR;\
    }\
    if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_CLEAR_EVENT) != NGX_OK) {\
        goto STOMP_CONN_ERROR;\
    }\
} else {\
    if (ngx_add_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT) != NGX_OK) {\
        goto STOMP_CONN_ERROR;\
    }\
    if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT) != NGX_OK) {\
        goto STOMP_CONN_ERROR;\
    }\
}

#define ngx_http_stomp_add_cleanup_pool \
cln = ngx_pool_cleanup_add(r->pool, 0);\
if (cln == NULL) {\
ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Unable to register pool cleanup");\
goto STOMP_CONN_ERROR;\
}\
cln->handler = ngx_http_stomp_upstream_cleanup;\
cln->data = r

#define ngx_http_frame_error_response(errmsg)({                              \
cstmp_frame_t dummy_frame;                                                   \
dummy_frame.cmd = (u_char*)"ERROR";                                          \
ngx_str_t dummy_headers_str = ngx_string("content-type:text/plain\n\0");     \
dummy_frame.headers.start = ngx_pstrdup(r->pool, &dummy_headers_str);        \
dummy_frame.headers.last = dummy_frame.headers.start + dummy_headers_str.len;\
ngx_str_t dummy_body_str = ngx_string(errmsg);                               \
dummy_frame.body.start = ngx_pstrdup(r->pool, &dummy_body_str);              \
dummy_frame.body.last = dummy_frame.body.start + dummy_body_str.len;         \
ngx_http_stomp_gen_chain(r,  ctx, &dummy_frame);                             \
ngx_stomp_upstream_done(r, r->upstream, NGX_ERROR);})

#define ngx_http_parse_frame_on_event(_r, _cslcf, _stss, _fstr) \
if (_stss->is_direct_frame) {\
if (ngx_http_complex_value(_r, &_cslcf->whole_frame, _fstr) != NGX_OK) {\
    ngx_http_frame_error_response("ngx_stomp: no frame set.");\
    return;\
}\
if (ngx_http_stomp_parse_direct_frame_message(_r, _cslcf, ctx, stss, _fstr) != NGX_OK) {\
    ngx_http_frame_error_response("ngx_stomp: Unable to generate response");\
    return;\
}\
} else {\
if (ngx_http_complex_value(_r, &_cslcf->command, _fstr) != NGX_OK) {\
    ngx_http_frame_error_response("ngx_stomp: No command set.");\
    return;\
}\
if (ngx_http_stomp_parse_frame(_r, _cslcf, ctx, stss, _fstr) != NGX_OK) {\
    ngx_http_frame_error_response("ngx_stomp: Unable to generate response");\
    return;\
}\
}

static void ngx_http_stomp_upstream_cleanup(void *data) {
    ngx_http_request_t *r = data;
    ngx_http_stomp_sess_t *stss = r->upstream->peer.data;
    if (stss) {
        // cstmp_reset_frame(stss->frame);
        ngx_unlock(&stss->in_use);
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_stomp: remove peer sess in use");
    }
}

static void
ngx_stomp_upstream_finalize_request(ngx_http_request_t *r,
                                    ngx_http_upstream_t *u, ngx_int_t rc) {
#if defined(nginx_version) && (nginx_version < 1009001)
    ngx_time_t  *tp;
#endif
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http upstream request: %i", rc);

    if (u->cleanup) {
        *u->cleanup = NULL;
    }

    if (u->resolved && u->resolved->ctx) {
        ngx_resolve_name_done(u->resolved->ctx);
        u->resolved->ctx = NULL;
    }

#if defined(nginx_version) && (nginx_version >= 1009001)
    if (u->state && u->state->response_time) {
        u->state->response_time = ngx_current_msec - u->state->response_time;
#else
    if (u->state && u->state->response_sec) {
        tp = ngx_timeofday();
        u->state->response_sec = tp->sec - u->state->response_sec;
        u->state->response_msec = tp->msec - u->state->response_msec;
#endif

        if (u->pipe) {
            u->state->response_length = u->pipe->read_length;
        }
    }

    if (u->finalize_request) {
        u->finalize_request(r, rc);
    }

    if (u->peer.free) {
        u->peer.free(&u->peer, u->peer.data, 0);
    }

    if (u->peer.connection) {

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "close http upstream connection: %d",
                       u->peer.connection->fd);

#if defined(nginx_version) && (nginx_version >= 1001004)
        if (u->peer.connection->pool) {
            ngx_destroy_pool(u->peer.connection->pool);
        }
#endif

        ngx_close_connection(u->peer.connection);
    }

    u->peer.connection = NULL;

    if (u->pipe && u->pipe->temp_file) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http upstream temp fd: %d",
                       u->pipe->temp_file->file.fd);
    }

    if (u->header_sent
            && (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE))
    {
        rc = 0;
    }

    if (rc == NGX_DECLINED) {

        return;
    }

    r->connection->log->action = "sending to client";


    if (rc == 0) {
        rc = ngx_http_send_special(r, NGX_HTTP_LAST);
    }

    ngx_http_finalize_request(r, rc);


}

static ngx_int_t
ngx_stomp_upstream_done(ngx_http_request_t *r, ngx_http_upstream_t *u, ngx_int_t rc) {
    /* flag for keepalive */
    u->headers_in.status_n = NGX_HTTP_OK;

    ngx_stomp_upstream_finalize_request(r, u, rc);

    return NGX_DONE;
}

static ngx_int_t
ngx_http_stomp_gen_chain(
    ngx_http_request_t *r,
    ngx_http_stomp_ctx_t *ctx,
    cstmp_frame_t *frame
) {
    ngx_chain_t *out;
    size_t resp_content_len = frame->body.last - frame->body.start;
    ngx_buf_t *b;

    /*TODO HEADER**/
    if ( ngx_http_stomp_parse_frame_headers(r, frame) != NGX_OK ) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_stomp: error parsing frame headers to output chain");
        return NGX_ERROR;
    }

    if ( resp_content_len ) {
        /* Allocate a new buffer for sending out the reply. */
        b = ngx_create_temp_buf(r->pool, resp_content_len);
        b->last = ngx_copy(b->last, frame->body.start, resp_content_len);
    } else {
        /* Allocate a new buffer for sending out the reply. */
        resp_content_len = 1;
        b = ngx_create_temp_buf(r->pool, resp_content_len);
        *b->last++ = LF;
    }

    b->memory = 1; /* content is in read-only memory */
    b->last_buf = 1; // there will be no more buffers in the request

    /* Insertion in the buffer chain. */
    out = ngx_alloc_chain_link(r->pool);
    if (out == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_stomp: return Null chain");
        return NGX_ERROR;
    }

    out->buf = b;
    out->next = NULL; /* just one buffer */
    ctx->response = out;
    return NGX_OK;
}

static ngx_int_t
ngx_http_stomp_write_resp(
    ngx_http_request_t *r,
    uintptr_t status_code,
    u_char* status_line,
    u_char* content_type,
    ngx_chain_t* out
) {
    ngx_int_t rc;
    // size_t resp_content_len;
    // ngx_http_upstream_t       *u = r->upstream;


    /* Set the Content-Type header. */
    if (content_type) {
        r->headers_out.content_type.len = ngx_strlen(content_type);
        r->headers_out.content_type.data =  content_type;
    }

    r->headers_out.status = status_code;

    if (status_line) {
        r->headers_out.status_line.len = ngx_strlen(status_line);
        r->headers_out.status_line.data = status_line;
    }

    /* Get the content length of the body. */
    r->headers_out.content_length_n = ngx_buf_size(out->buf);
    // ngx_http_clear_content_length(r);

    rc = ngx_http_send_header(r); /* Send the headers */
    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "response processing failed.");
        // ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
    }


    rc = ngx_http_output_filter(r, out);
    if (rc == NGX_ERROR || rc > NGX_OK) {
        return rc;
    }

    return rc;
}

// static ngx_conf_bitmask_t  ngx_http_stomp_next_upstream_masks[] = {
//     { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
//     { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
//     { ngx_string("STOMP_CONN_ERROR_response"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
//     { ngx_string("not_found"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
//     { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
//     { ngx_null_string, 0 }
// };

static ngx_command_t  ngx_http_stomp_commands[] = {
    {   ngx_string("stomp_endpoint"),
        NGX_HTTP_UPS_CONF | NGX_CONF_1MORE,
        ngx_http_stomp_upstream_upstream,
        NGX_HTTP_SRV_CONF_OFFSET,
        0,
        NULL
    },

    {   ngx_string("stomp_subscribes"),
        NGX_HTTP_UPS_CONF | NGX_CONF_TAKE1, // change to only take 1 queue per stomp connection
        ngx_http_stomp_subscribe_queue_command,
        NGX_HTTP_SRV_CONF_OFFSET,
        0,
        NULL
    },

    {   ngx_string("stomp_pass"),
        NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
        ngx_http_stomp_pass,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    {   ngx_string("stomp_bind"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE12,
        ngx_http_upstream_bind_set_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_stomp_loc_conf_t, upstream.local),
        NULL
    },

    {   ngx_string("stomp_connect_timeout"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_stomp_loc_conf_t, upstream.connect_timeout),
        NULL
    },

    {   ngx_string("stomp_send_timeout"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_stomp_loc_conf_t, upstream.send_timeout),
        NULL
    },

    {   ngx_string("stomp_buffer_size"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_size_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_stomp_loc_conf_t, upstream.buffer_size),
        NULL
    },

    {   ngx_string("stomp_read_timeout"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_stomp_loc_conf_t, upstream.read_timeout),
        NULL
    },

    {   ngx_string("stomp_command"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_stomp_stomp_command_command,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    {   ngx_string("stomp_headers"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_stomp_headers_command,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    {   ngx_string("stomp_body"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_stomp_body_command,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    {   ngx_string("stomp_whole_frame"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_stomp_frame_command,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    ngx_null_command
};

static ngx_http_module_t  ngx_http_stomp_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */
    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */
    ngx_http_stomp_upstream_create_srv_conf, /* create server configuration */
    NULL,                                  /* merge server configuration */
    ngx_http_stomp_create_loc_conf,    /* create location configuration */
    ngx_http_stomp_merge_loc_conf      /* merge location configuration */
};

ngx_module_t  ngx_http_stomp_module = {
    NGX_MODULE_V1,
    &ngx_http_stomp_module_ctx,        /* module context */
    ngx_http_stomp_commands,           /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static void *
ngx_http_stomp_upstream_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_stomp_upstream_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_stomp_upstream_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}

static void
ngx_stomp_upstream_wev_handler(ngx_http_request_t *r, ngx_http_upstream_t *u) {
    ngx_connection_t  *c = u->peer.connection;
    ngx_http_stomp_sess_t *stss = r->upstream->peer.data;
    ngx_http_stomp_ctx_t *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_stomp_module);

    /* just to ensure u->reinit_request always gets called for upstream_next */
    u->request_sent = 1;

    cstmp_frame_t *fr = stss->frame;

    switch (stss->state) {
    case stomp_connect:
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: connecting\n");
        // if (!c->write->timer_set) {
        //     ngx_add_timer(c->write, r->upstream->conf->send_timeout);
        //     ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: add read timer");
        //     ngx_add_timer(c->read, r->upstream->conf->read_timeout);
        // }
        if (cstmp_send(stss->sess, fr, 1)) {
            ngx_http_stomp_dump_frame(r, fr);
        } else {
            ngx_log_error(NGX_LOG_ERR, c->log, 0, "ngx_stomp: failed connect");
            stss->state = stomp_error;
            goto STOMP_FAILED_PROCESS_IO;
        }
        break;
    case stomp_send:
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: sending frame\n");
        ngx_http_stomp_dump_frame(r, fr);

        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: sending\n");
        if (ngx_http_stomp_send(stss, fr)) {
            ngx_http_stomp_dump_frame(r, fr);
            stss->state = stomp_resp;
        } else {
            ngx_log_error(NGX_LOG_ERR, c->log, 0, "ngx_stomp: failed connect");
            stss->state = stomp_error;
            goto STOMP_FAILED_PROCESS_IO;
        }
        break;
    case stomp_sub:
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: sending\n");
        if (ngx_http_stomp_send(stss, fr)) {
            ngx_http_stomp_dump_frame(r, fr);
            stss->state = stomp_sub;
        } else {
            ngx_log_error(NGX_LOG_ERR, c->log, 0, "ngx_stomp: failed connect");
            stss->state = stomp_error;
            goto STOMP_FAILED_PROCESS_IO;
        }
        break;
    case stomp_ack:
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: acknowledging\n");
        if (ngx_http_stomp_send(stss, fr)) {
            ngx_http_stomp_dump_frame(r, fr);
            ngx_stomp_upstream_done(r, u, NGX_OK);
            stss->state = stomp_idle;
        } else {
            ngx_log_error(NGX_LOG_ERR, c->log, 0, "ngx_stomp: failed connect");
            stss->state = stomp_error;
            goto STOMP_FAILED_PROCESS_IO;
        }
        break;
    case stomp_read:
        break;
    default:
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: error state\n");
        stss->state = stomp_error;
        goto STOMP_FAILED_PROCESS_IO;
        break;
    }
    return;
STOMP_FAILED_PROCESS_IO:
    if (stss->sess) {
        ngx_close_socket(stss->sess->sock);
        stss->sess = NULL;
    }
    ctx = ngx_http_get_module_ctx(r, ngx_http_stomp_module);
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: IO PROCESS FAILED");
    ngx_http_frame_error_response("ngx_stomp: error while sending stomp");
}

static void
ngx_stomp_upstream_rev_handler(ngx_http_request_t *r, ngx_http_upstream_t *u) {
    ngx_connection_t  *c = u->peer.connection;
    ngx_http_stomp_sess_t *stss = r->upstream->peer.data;
    ngx_http_stomp_ctx_t *ctx;
    ngx_http_stomp_loc_conf_t *cslcf;
    ngx_str_t *destq;
    cstmp_frame_val_t val;
    u_char *messageid;
    /* just to ensure u->reinit_request always gets called for upstream_next */
    u->request_sent = 1;

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: reading now");

    cstmp_frame_t *fr = stss->frame;

    ctx = ngx_http_get_module_ctx(r, ngx_http_stomp_module);
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_stomp: Not context session");
        ngx_http_frame_error_response("ngx_stomp: Not context session while processing request");
        return;
    }

    if (stss->state == stomp_error) {
        ngx_http_frame_error_response("ngx_stomp: error while processing stomp request");
    }

    if (ngx_http_stomp_recv(stss, fr)) {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "ngx_stomp: dump frame\n");
        ngx_http_stomp_dump_frame(r, fr);

        if (ctx->readonly) {
            switch (stss->state) {
            case stomp_connect:
            case stomp_sub:
                destq = stss->dest_queues++;
                if (destq->len == 0) {
                    /*** Means all queue has done subscribed ***/
                    stss->state = stomp_ack;
                    ngx_stomp_upstream_rev_handler(r, u);
                    return;
                }
                stss->state = stomp_sub;
                cstmp_reset_frame(fr);
                if (stss->is_direct_frame) {
                    fr->cmd = (u_char*) "SUBSCRIBE";
                    cstmp_add_body_content(fr, (u_char*)"SUBSCRIBE\ndestination:");
                    cstmp_add_body_content(fr, destq->data);
                    cstmp_add_body_content(fr, (u_char*)"\nack:client\nreceipt:sub-ack\n");
                    cstmp_add_body_content(fr, (u_char*) "id:");
                    cstmp_add_body_content(fr, destq->data);
                    cstmp_add_body_content(fr, (u_char*)"\n\n");
                } else {
                    fr->cmd = (u_char*)"SUBSCRIBE";
                    cstmp_add_header(fr, (u_char*) "destination", destq->data);
                    cstmp_add_header(fr, (u_char*)"ack", (u_char*)"client");
                    cstmp_add_header(fr, (u_char*)"receipt", (u_char*)"sub-ack");
                    cstmp_add_header(fr, (u_char*)"id", destq->data);
                }
                return;
            case stomp_resp:
                ngx_stomp_upstream_done(r, u, NGX_OK);
                return;
            case stomp_read:
            default:
                if (cstmp_get_header(fr, (u_char*) "message-id", &val)) {
                    messageid = ngx_pcalloc(r->pool,  (val.len + 1) * sizeof(u_char));
                    ngx_memcpy(messageid, val.data, val.len);

                    /** Preparing the response **/
                    if (ngx_http_stomp_gen_chain(r, ctx, fr) != NGX_OK) {
                        ngx_http_frame_error_response("ngx_stomp: Unable to generate response");
                    }

                    cstmp_reset_frame(fr);

                    fr->cmd = (u_char*)"ACK";
                    cstmp_add_header(fr, (u_char*)"message-id", messageid);
                    cstmp_add_header(fr, (u_char*)"id", messageid);
                    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "ngx_stomp: generate ack");
                    stss->state = stomp_ack;
                } else {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_stomp: No message id been consume");
                    // ngx_http_frame_error_response("ngx_stomp: unable to consume the message");
                }
            }
        } else {
            if (stss->state == stomp_resp) {
                if (ngx_http_stomp_gen_chain(r, ctx, fr) == NGX_OK) {
                    ngx_stomp_upstream_done(r, u, NGX_OK);
                }
            } else if (stss->state == stomp_connect) {
                ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "ngx_stomp: preparing stomp message on first connect");
                ngx_str_t frame_str;
                stss->state = stomp_send;
                cslcf = ngx_http_get_module_loc_conf(r, ngx_http_stomp_module);
                ngx_http_parse_frame_on_event(r, cslcf, stss, &frame_str);
            }
        }
    } else {
        ngx_log_error(NGX_LOG_ERR, c->log, 0, "ngx_stomp: failed connect");
        ngx_http_frame_error_response("ngx_stomp: failed to read the response");
        return;
    }
}

static ngx_int_t
ngx_http_stomp_content_handler(ngx_http_request_t *r) {
    ngx_int_t                       rc;
    ngx_http_upstream_t            *u;
    ngx_http_stomp_ctx_t       *ctx;
    ngx_http_stomp_loc_conf_t  *cslcf;

    // if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
    //     return NGX_HTTP_NOT_ALLOWED;
    // }

    // rc = ngx_http_discard_request_body(r);

    // if (rc != NGX_OK) {
    //     return rc;
    // }

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    ngx_str_set(&u->schema, "tcp://");
    u->output.tag = (ngx_buf_tag_t) &ngx_http_stomp_module;

    cslcf = ngx_http_get_module_loc_conf(r, ngx_http_stomp_module);

    u->conf = &cslcf->upstream;

    u->create_request = ngx_http_stomp_create_request;
    u->reinit_request = ngx_http_stomp_reinit_request;
    u->process_header = ngx_http_stomp_process_header;
    u->abort_request = ngx_http_stomp_abort_request;
    u->finalize_request = ngx_http_stomp_finalize_request;
    r->state = 0;

    u->buffering = cslcf->upstream.buffering;

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_stomp_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_stomp_module);

    u->input_filter_init = ngx_http_stomp_filter_init;
    u->input_filter = ngx_http_stomp_non_buffered_filter;
    u->input_filter_ctx = NULL;

// #if defined(nginx_version) && (nginx_version >= 8011)
//     r->main->count++;
// #endif

    if (!cslcf->upstream.request_buffering
            && cslcf->upstream.pass_request_body) {
        r->request_body_no_buffering = 1;
    }

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    /* override the read/write event handler to our own */
    u->write_event_handler = ngx_stomp_upstream_wev_handler;
    u->read_event_handler = ngx_stomp_upstream_rev_handler;

    return NGX_DONE;
}


static ngx_int_t
ngx_http_stomp_create_request(ngx_http_request_t *r) {
    r->upstream->request_bufs = NULL;
    return NGX_OK;
}

static ngx_int_t
ngx_http_stomp_reinit_request(ngx_http_request_t *r) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "init request");

    ngx_http_upstream_t *u = r->upstream;
    /* override the read/write event handler to our own */
    u->write_event_handler = ngx_stomp_upstream_wev_handler;
    u->read_event_handler = ngx_stomp_upstream_rev_handler;

    return NGX_OK;
}

static ngx_int_t
ngx_http_stomp_process_header(ngx_http_request_t *r) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_stomp: ngx_http_stomp_process_header should not be here");
    return NGX_ERROR;
}

static ngx_int_t
ngx_http_stomp_filter_init(void *data) {
    return NGX_ERROR;
}

static ngx_int_t
ngx_http_stomp_non_buffered_filter(void *data, ssize_t bytes) {
    return NGX_ERROR;
}

static void
ngx_http_stomp_abort_request(ngx_http_request_t *r) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http stomp request");
    return;
}

static void
ngx_http_stomp_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "finalize http stomp request");
    ngx_http_stomp_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_stomp_module);

    /*** Stop here if no response ***/
    if (!ctx->response) {
        return;
    }

    if (rc == NGX_OK) {
        ngx_http_stomp_write_resp(r, 200, (u_char*)"200 OK", (u_char*)"text/plain", ctx->response);
    } else {
        ngx_http_stomp_write_resp(r, 500, (u_char*)"500 Internal Server Error", (u_char*)"text/plain", ctx->response);
    }
}


static void *
ngx_http_stomp_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_stomp_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_stomp_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->upstream.local = NGX_CONF_UNSET_PTR;
    conf->upstream.next_upstream_tries = NGX_CONF_UNSET_UINT;
    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.next_upstream_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    /* the hardcoded values */
    conf->upstream.cyclic_temp_file = 0;
    conf->upstream.buffering = 0;
    conf->upstream.ignore_client_abort = 0;
    conf->upstream.send_lowat = 0;
    conf->upstream.bufs.num = 0;
    conf->upstream.busy_buffers_size = 0;
    conf->upstream.max_temp_file_size = 0;
    conf->upstream.temp_file_write_size = 0;
    conf->upstream.intercept_errors = 1;
    conf->upstream.intercept_404 = 1;
    conf->upstream.pass_request_headers = 0;
    conf->upstream.pass_request_body = 0;
    conf->upstream.force_ranges = 1;


    /*Stomp Init*/
    conf->has_subs = NGX_CONF_UNSET;
    ngx_memzero(&conf->command, sizeof(ngx_http_complex_value_t));
    ngx_memzero(&conf->headers, sizeof(ngx_http_complex_value_t));
    ngx_memzero(&conf->body, sizeof(ngx_http_complex_value_t));
    ngx_memzero(&conf->whole_frame, sizeof(ngx_http_complex_value_t));

    return conf;
}


static char *
ngx_http_stomp_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_stomp_loc_conf_t *prev = parent;
    ngx_http_stomp_loc_conf_t *conf = child;

    ngx_conf_merge_ptr_value(conf->upstream.local,
                             prev->upstream.local, NULL);

    ngx_conf_merge_uint_value(conf->upstream.next_upstream_tries,
                              prev->upstream.next_upstream_tries, 0);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.next_upstream_timeout,
                              prev->upstream.next_upstream_timeout, 0);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                                 prev->upstream.next_upstream,
                                 (NGX_CONF_BITMASK_SET
                                  | NGX_HTTP_UPSTREAM_FT_ERROR
                                  | NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       | NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }


    ngx_conf_merge_value(conf->has_subs, prev->has_subs, 0);

    return NGX_CONF_OK;
}

static char *
ngx_http_stomp_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_stomp_loc_conf_t *cslcf = conf;

    ngx_str_t                 *value;
    ngx_url_t                  u;
    ngx_http_core_loc_conf_t  *clcf;

    if (cslcf->upstream.upstream) {
        return "is duplicate";
    }

    value = cf->args->elts;

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.no_resolve = 1;

    cslcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (cslcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_stomp_content_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    // cslcf->index = ngx_http_get_variable_index(cf, &ngx_http_stomp_key);
    // if (cslcf->index == NGX_ERROR) {
    //     return NGX_CONF_ERROR;
    // }

    return NGX_CONF_OK;
}

static char *
ngx_conf_stomp_stomp_command_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_stomp_loc_conf_t *cslf = conf;
    ngx_str_t                         *value;
    ngx_http_compile_complex_value_t   ccv;

    if (cslf->command.value.len != 0) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no command given, e.g. SEND/CONSUME ");
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &cslf->command;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_conf_stomp_headers_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_stomp_loc_conf_t *cslf = conf;
    ngx_str_t                         *value;
    ngx_str_t                         *header_value;
    ngx_http_compile_complex_value_t   ccv;
    u_char *rectified_headers_str, *actual_header_str;
    size_t i, j, len;

    if (cslf->headers.value.len != 0) {
        return "is duplicate";
    }

    value = cf->args->elts;
    header_value = &value[1];
    len = header_value->len;
    actual_header_str = header_value->data;

    if (len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no headers given, e.g. destination:/queue/myqueue ");
        return NGX_CONF_ERROR;
    }

    rectified_headers_str = ngx_pcalloc(cf->pool, len + 1);
    // ngx_memcpy(rectified_headers_str, header_value->data, len);

    for (i = 0, j = 0; i < len; i++) {
        if (actual_header_str[i] != '\r' &&
                actual_header_str[i] != ' ' &&
                actual_header_str[i] != '\t' &&
                actual_header_str[i] != '\v' ) {
            rectified_headers_str[j++] = actual_header_str[i];
        }
    }
    rectified_headers_str[j++] = '\n';
    header_value->data = rectified_headers_str;
    header_value->len = j;

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = header_value;
    ccv.complex_value = &cslf->headers;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    return NGX_CONF_OK;
}

static char *
ngx_conf_stomp_body_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_stomp_loc_conf_t *cslf = conf;
    ngx_str_t                         *value;
    ngx_http_compile_complex_value_t   ccv;

    if (cslf->body.value.len != 0) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no body given, e.g. your body payload ");
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &cslf->body;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    return NGX_CONF_OK;
}

static char *
ngx_conf_stomp_frame_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_stomp_loc_conf_t *cslf = conf;
    ngx_str_t                         *value;
    ngx_http_compile_complex_value_t   ccv;

    if (cslf->whole_frame.value.len != 0) {
        return "is duplicate";
    }
    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no frame given, e.g. %s\n%s\n\n%s\n", "SEND", "destination:/queue/a", "hello queue a");
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &cslf->whole_frame;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    return NGX_CONF_OK;
}

static void
ngx_http_stomp_dump_frame(ngx_http_request_t *r, cstmp_frame_t* fr) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "%s\n", fr->cmd);
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "%*s\n\n", fr->headers.last - fr->headers.start, fr->headers.start);
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "%*s\n",  fr->body.last - fr->body.start, fr->body.start);
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "\n");
}

static ngx_int_t
ngx_http_stomp_parse_frame_headers(ngx_http_request_t *r, cstmp_frame_t *fr) {
    ngx_table_elt_t *h;
    u_char *head_start_ptr, *head_last_ptr, *p;

    /** Frame Command Header **/
    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    h->hash = 1; /*to mark HTTP output headers show set 1, show missing set 0*/
    ngx_str_set(&h->key, "frame");

    h->value.len = ngx_strlen(fr->cmd);
    h->value.data = ngx_palloc(r->pool, h->value.len * sizeof(u_char));
    ngx_memcpy(h->value.data, fr->cmd, h->value.len);

    /** Frame Header Headers **/
    head_start_ptr = fr->headers.start;
    head_last_ptr = fr->headers.last;

    while ( (p = ngx_strlchr(head_start_ptr, head_last_ptr, ':')) ) {
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }

        h->hash = 1; /*to mark HTTP output headers show set 1, show missing set 0*/
        h->key.len = p - head_start_ptr;
        h->key.data = ngx_palloc(r->pool, h->key.len * sizeof(u_char));
        ngx_memcpy(h->key.data, head_start_ptr, h->key.len);
        head_start_ptr = ++p;

        if ( (p = ngx_strlchr(head_start_ptr, head_last_ptr, '\n') ) ) {
            h->value.len = p - head_start_ptr;
            h->value.data = ngx_palloc(r->pool, h->value.len * sizeof(u_char));
            ngx_memcpy(h->value.data, head_start_ptr, h->value.len);
            head_start_ptr = ++p;
        }
    }
    return NGX_OK;
}

static char *
ngx_http_stomp_subscribe_queue_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_stomp_upstream_srv_conf_t      *stmpuscf = conf;
    ngx_str_t                               *values = cf->args->elts;
    ngx_str_t                               *queue;
    ngx_uint_t                              i;

    if (stmpuscf->subscribe_queues == NULL) {
        stmpuscf->subscribe_queues = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
        if (stmpuscf->subscribe_queues == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ngx_stomp: error while setup subscribe queue");
            return NGX_CONF_ERROR;
        }
    }

    /** in order to make it easiy, subscribe ID same as queue name ***/
    for (i = 1; i < cf->args->nelts; i++) {
        queue = ngx_array_push(stmpuscf->subscribe_queues);
        if (queue == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ngx_stomp: error while setup subscribe queue");
            return NGX_CONF_ERROR;
        }
        queue->data = values[i].data;
        queue->len = values[i].len;
    }
    return NGX_CONF_OK;
}

static char *
ngx_http_stomp_upstream_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_upstream_srv_conf_t   *uscf;
    ngx_str_t                      *value = cf->args->elts;
    ngx_http_stomp_upstream_srv_conf_t      *stmpuscf = conf;
    ngx_stomp_upstream_server_t    *stmp_srv;
    ngx_uint_t                     i;
    ngx_url_t                      u;
    ngx_http_compile_complex_value_t   ccv;

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    if (stmpuscf->servers == NULL) {
        stmpuscf->servers = ngx_array_create(cf->pool, 4,
                                             sizeof(ngx_stomp_upstream_server_t));
        if (stmpuscf->servers == NULL) {
            return NGX_CONF_ERROR;
        }

        uscf->servers = stmpuscf->servers;
    }

    if (stmpuscf->subscribe_queues == NULL) {
        stmpuscf->subscribe_queues = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
        if (stmpuscf->subscribe_queues == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ngx_stomp: error while setup subscribe queue");
            return NGX_CONF_ERROR;
        }
    }

    stmp_srv = ngx_array_push(stmpuscf->servers);
    if (stmp_srv == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(stmp_srv, sizeof(ngx_stomp_upstream_server_t));

    /* parse the first name:port argument */

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.default_port = 61616; /* stomp protocol default port */

    if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
        if (u.err) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "ngx_stomp: %s in upstream \"%V\"",
                               u.err, &u.url);
        }
        return NGX_CONF_ERROR;
    }

    stmp_srv->addrs = u.addrs;
    stmp_srv->naddrs = u.naddrs;
    stmp_srv->port = u.port;
    stmp_srv->max_send_sess = 10; /*Default*/
    stmp_srv->max_recv_sess = 5; /*Default*/


    for (i = 2; i < cf->args->nelts; i++) {

        // if (ngx_strncmp(value[i].data, "headers=", sizeof("headers=") - 1)
        //         == 0)
        // {
        //     stmp_srv->headers.len = value[i].len - (sizeof("headers=") - 1);
        //     stmp_srv->headers.data = &value[i].data[sizeof("headers=") - 1];
        //     continue;
        // }

        if (ngx_strncmp(value[i].data, "login=", sizeof("login=") - 1) == 0) {

            ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
            ccv.cf = cf;
            ccv.value = &value[i];
            ccv.complex_value = &stmp_srv->username_val;

            if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
            continue;
        }

        if (ngx_strncmp(value[i].data, "passcode=", sizeof("passcode=") - 1) == 0) {

            ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
            ccv.cf = cf;
            ccv.value = &value[i];
            ccv.complex_value = &stmp_srv->password_val;

            if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
            continue;
        }

        if (ngx_strncmp(value[i].data, "max_send_sess=", sizeof("max_send_sess=") - 1)
                == 0)
        {
            stmp_srv->max_send_sess = ngx_atoi( &value[i].data[sizeof("max_send_sess=") - 1],  value[i].len - (sizeof("max_send_sess=") - 1));
            continue;
        }


        if (ngx_strncmp(value[i].data, "max_recv_sess=", sizeof("max_recv_sess=") - 1)
                == 0)
        {
            stmp_srv->max_recv_sess = ngx_atoi( &value[i].data[sizeof("max_recv_sess=") - 1],  value[i].len - (sizeof("max_recv_sess=") - 1));
            continue;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "ngx_stomp: STOMP_CONF_ERROR parameter \"%V\" in"
                           " \"stomp_server\"", &value[i]);



        return NGX_CONF_ERROR;
    }

    uscf->peer.init_upstream = ngx_http_stomp_upstream_upstream_init;
    return NGX_CONF_OK;
}


/*** Stomp Upstream ***/

static ngx_int_t
ngx_http_stomp_upstream_upstream_init(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *uscf) {
    ngx_uint_t                       i, j, k, n;
    ngx_stomp_upstream_server_t      *server;
    ngx_http_stomp_upstream_srv_conf_t *stmpuscf;
    ngx_str_t                        *null_queue;
    ngx_http_stomp_upstream_peers_t  *peers;
    ngx_http_stomp_upstream_peer_t   *peer;
    ngx_http_stomp_sess_t            *stss;

    uscf->peer.init = ngx_http_stomp_upstream_upstream_init_peer;

    stmpuscf = ngx_http_conf_upstream_srv_conf(uscf, ngx_http_stomp_module);

    if (!uscf->servers) {
        return NGX_ERROR;
    }

    if (stmpuscf->subscribe_queues) {
        null_queue = ngx_array_push(stmpuscf->subscribe_queues);
        if (null_queue == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ngx_stomp: error while setup subscribe queue");
            return NGX_ERROR;
        }
        null_queue->len = 0;
    }


    server = uscf->servers->elts;
    n = 0;

    for (i = 0; i < uscf->servers->nelts; i++) {
        n += server[i].naddrs;
    }

    if (n == 0) {
        return NGX_ERROR;
    }

    peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_stomp_upstream_peers_t)
                        + sizeof(ngx_http_stomp_upstream_peer_t) * n);

    if (peers == NULL) {
        return NGX_ERROR;
    }

    peers->number = n;
    peers->name = &uscf->host;

    n = 0;

    for (i = 0; i < uscf->servers->nelts; i++) {
        for (j = 0; j < server[i].naddrs; j++) {
            peer = &peers->peer[n];


            peer->sockaddr = server[i].addrs[j].sockaddr;
            peer->socklen = server[i].addrs[j].socklen;
            peer->name = server[i].addrs[j].name;


            peer->host.data = ngx_pnalloc(cf->pool,
                                          NGX_SOCKADDR_STRLEN);
            if (peer->host.data == NULL) {
                return NGX_ERROR;
            }

            peer->host.len = ngx_sock_ntop(peer->sockaddr,
#if defined(nginx_version) && (nginx_version >= 1005003)
                                           peer->socklen,
#endif
                                           peer->host.data,
                                           NGX_SOCKADDR_STRLEN, 0);
            if (peer->host.len == 0) {
                return NGX_ERROR;
            }

            peer->port = ntohs(((struct sockaddr_in *)server[i].addrs[j].sockaddr)->sin_port);

            peer->username_val = &server[i].username_val;
            peer->password_val = &server[i].password_val;

            /* Cache Session */
            peer->stomp_send_sessions = ngx_array_create(cf->pool, server[i].max_send_sess,
                                        sizeof(ngx_http_stomp_sess_t));
            if (peer->stomp_send_sessions == NULL) {

                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "ngx_stomp: unable to establish stomp session");
                return NGX_ERROR;
            }

            for (k = 0; k < server[i].max_send_sess; k++) {
                stss = ngx_array_push(peer->stomp_send_sessions);
                stss->in_use = 0;
                stss->sess = NULL;
                stss->conn = NULL;
            }

            peer->stomp_recv_sessions = ngx_array_create(cf->pool, server[i].max_recv_sess,
                                        sizeof(ngx_http_stomp_sess_t));
            if (peer->stomp_recv_sessions == NULL) {

                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "ngx_stomp: unable to establish stomp session");
                return NGX_ERROR;
            }

            for (k = 0; k < server[i].max_recv_sess; k++) {
                stss = ngx_array_push(peer->stomp_recv_sessions);
                stss->in_use = 0;
                stss->sess = NULL;
                stss->conn = NULL;
            }
            n++;
        }
    }
    uscf->peer.data = peers;

    return NGX_OK;
}

static ngx_int_t
ngx_http_stomp_upstream_upstream_init_peer(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *uscf) {

    ngx_http_stomp_upstream_peer_data_t     *uhpd;
    ngx_http_stomp_upstream_srv_conf_t *stmpuscf;

    uhpd = ngx_pcalloc(r->pool, sizeof(ngx_http_stomp_upstream_peer_data_t)
                       + sizeof(uintptr_t) *
                       ((ngx_http_stomp_upstream_peers_t *)uscf->peer.data)->number /
                       (8 * sizeof(uintptr_t)));
    if (uhpd == NULL) {
        return NGX_ERROR;
    }

    stmpuscf = ngx_http_conf_upstream_srv_conf(uscf, ngx_http_stomp_module);

    if (stmpuscf == NULL) {
        return NGX_ERROR;
    }

    uhpd->r = r;
    uhpd->peers = uscf->peer.data;
    uhpd->subscribe_queues = stmpuscf->subscribe_queues;

    r->upstream->peer.data = uhpd;
    r->upstream->peer.get = ngx_stomp_upstream_get_peer;
    r->upstream->peer.free = ngx_stomp_upstream_free_peer;


    return NGX_OK;
}

static ngx_int_t
ngx_stomp_upstream_get_peer(ngx_peer_connection_t *pc, void *data) {
    ngx_http_stomp_upstream_peer_data_t  *uhpd = data;
    ngx_http_stomp_upstream_peer_t       *peer = NULL;
    ngx_http_stomp_upstream_peers_t      *peers;
    ngx_connection_t                     *stmpc;
    ngx_http_request_t                   *r;
    ngx_uint_t                           i, j, npeers, max_sess;
    ngx_http_stomp_ctx_t                 *ctx;
    ngx_event_t                          *rev, *wev;
    ngx_array_t                          *sess_arr;
    ngx_http_stomp_sess_t                *stss;
    cstmp_frame_t                        *fr;
    ngx_http_stomp_loc_conf_t            *cslcf;
    ngx_str_t                            direct_frame_str, command_str;
    ngx_pool_cleanup_t                   *cln;
    ngx_flag_t                           is_direct_frame = 0;

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, pc->log, 0, "ngx_stomp: get upstream request");

    r = uhpd->r;
    // peer = &peers->peer[i];

    cslcf = ngx_http_get_module_loc_conf(r, ngx_http_stomp_module);
    ctx = ngx_http_get_module_ctx(r, ngx_http_stomp_module);

    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, pc->log, 0, "ngx_stomp: failed to get a session");
        goto STOMP_REQUEST_ERROR;
    }

    peers = uhpd->peers;
    npeers =  peers->number;

    ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "ngx_stomp: total Npeer = %d",  npeers);

    /**Process Stomp Message**/
    if (cslcf->whole_frame.value.len) {
        is_direct_frame = 1;
        if (ngx_http_complex_value(r, &cslcf->whole_frame, &direct_frame_str) != NGX_OK) {
            ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "%s", "No frame set?");
            goto STOMP_REQUEST_ERROR;
        }
        /** Ignore the read if it is not subscribe command **/
        if (ngx_strncmp(direct_frame_str.data, "CONSUME", 7 ) == 0) {
            ctx->readonly = 1;
        } else {
            ctx->readonly = 0;
        }
    } else if ((cslcf->command.value.len) ) {
        if (ngx_http_complex_value(r, &cslcf->command, &command_str) != NGX_OK) {
            ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "%s", "No command set?");
            goto STOMP_REQUEST_ERROR;
        }

        if (ngx_strncmp(command_str.data, "CONSUME", command_str.len) == 0) {
            ctx->readonly = 1;
        } else {
            ctx->readonly = 0;
        }
    } else {
        goto STOMP_REQUEST_ERROR;
    }

    if (ctx->readonly) {
        for (i = 0; i < npeers; i++) {
            peer = &peers->peer[i];
            sess_arr = peer->stomp_recv_sessions;
            max_sess = sess_arr->nelts;
            stss = (ngx_http_stomp_sess_t*)sess_arr->elts;
            for (j = 0; j < max_sess; j++) {
                if (ngx_trylock(&stss->in_use)) {
                    ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "ngx_stomp: get peer");
                    goto FOUND_PEER;
                }
                stss++;
            }
        }
    } else {
        for (i = 0; i < npeers; i++) {
            peer = &peers->peer[i];
            sess_arr = peer->stomp_send_sessions;
            max_sess = sess_arr->nelts;
            stss = (ngx_http_stomp_sess_t*)sess_arr->elts;
            for (j = 0; j < max_sess; j++) {
                if (ngx_trylock(&stss->in_use)) {
                    ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "ngx_stomp: get peer");
                    goto FOUND_PEER;
                }
                stss++;
            }
        }
    }

FOUND_PEER:
    if (i == npeers) {
        r->upstream->peer.data = NULL;
        goto STOMP_OUT_OF_USAGE;
    }

    stss->is_direct_frame = is_direct_frame;

    ngx_http_stomp_add_cleanup_pool;

    if (stss->sess) {
        if (!ctx->readonly) {
            ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "ngx_stomp: preparing stomp sending frame");
            if (is_direct_frame) {
                if (ngx_http_stomp_parse_direct_frame_message(r, cslcf, ctx, stss, &direct_frame_str) != NGX_OK) {
                    goto STOMP_REQUEST_ERROR;
                }
            } else {
                if (ngx_http_stomp_parse_frame(r, cslcf, ctx, stss, &command_str) != NGX_OK) {
                    goto STOMP_REQUEST_ERROR;
                }
            }
        }

        ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "ngx_stomp: reuse stomp sess");

        ngx_connection_t * c = stss->conn;
        c->idle = 0;
        c->log = pc->log;
#if defined(nginx_version) && (nginx_version >= 1001004)
        c->pool->log = pc->log;
#endif
        c->read->log = pc->log;
        c->write->log = pc->log;

        pc->connection = c;
        pc->cached = 1;

        pc->name = &peer->name;
        pc->sockaddr = peer->sockaddr;
        pc->socklen = peer->socklen;

        r->upstream->peer.data = stss;

        // rev = c->read;
        // wev = c->write;
        // rev->log = pc->log;
        // wev->log = pc->log;

        if (ctx->readonly) {
            c->write->ready = 0;
            ngx_http_stomp_add_read_write_event(c, c->read, NGX_READ_EVENT);
            stss->state = stomp_read;
            // ngx_stomp_upstream_rev_handler(r, r->upstream);
        } else {
            stss->state = stomp_send;
            ngx_stomp_upstream_wev_handler(r, r->upstream);
        }

        /*END FINALIZE */
        return NGX_AGAIN;
    }

    pc->sockaddr = peer->sockaddr;
    pc->socklen = peer->socklen;
    pc->name = &peer->name;
    pc->cached = 0;

    if (!(stss->sess = cstmp_connect_t((const char*) peer->host.data, peer->port, 3 /*send timeout 3 sec*/, 3000 ))) {
        goto STOMP_CONN_ERROR;
    }

    stmpc = pc->connection = ngx_get_connection(stss->sess->sock, pc->log);

    if (stmpc == NULL) {
        ngx_log_error(NGX_LOG_ERR, pc->log, 0, "ngx_stomp: failed to get a free nginx connection");

        goto STOMP_CONN_ERROR;
    }

    if (ctx->readonly) {
        stss->dest_queues = uhpd->subscribe_queues->elts;
    }

    ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "ngx_stomp: connection established");

    stmpc->log = pc->log;
    stmpc->log_error = pc->log_error;
    stmpc->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

    fr = stss->frame = cstmp_new_frame();
    r->upstream->peer.data = stss;

    rev = stmpc->read;
    wev = stmpc->write;

    rev->log = pc->log;
    wev->log = pc->log;

    /* register the connection with ngx_stomp connection fd into the
     * nginx event model */
    ngx_stomp_register_read_write_event_model;

    ngx_str_t username, password;
    stss->state = stomp_connect;

    fr->cmd = (u_char*)"CONNECT";
    cstmp_add_header_str(fr, (u_char*)"accept-version:1.2"); // for direct string set method

    if (peer->username_val->value.len) {
        if (ngx_http_complex_value(r, peer->username_val, &username) != NGX_OK) {
            goto STOMP_REQUEST_ERROR;
        }
        username.data += sizeof("login"); // same with sizeof("login=") -1
        username.len -= sizeof("login");
        cstmp_add_header(fr, (u_char*)"login", username.data); // for key val set method
    }
    if (peer->password_val->value.len) {
        if (ngx_http_complex_value(r, peer->password_val, &password) != NGX_OK) {
            goto STOMP_REQUEST_ERROR;
        }

        password.data += sizeof("passcode"); // same with sizeof("passcode=") -1
        password.len -= sizeof("passcode");
        cstmp_add_header(fr, (u_char*)"passcode", password.data); // in case you need len specified
    }
    stmpc->log->action = "connecting to stomp ";
    ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "ngx_stomp: Connecting host=%V, port=%d", &peer->host, peer->port);

    return NGX_AGAIN;

STOMP_REQUEST_ERROR:
    ngx_log_error(NGX_LOG_ERR, pc->log, 0, "ngx_stomp: Error processing stomp message request");
#if defined(nginx_version) && (nginx_version >= 8017)
    return NGX_ERROR;
#else
    /* To generate error response */
    pc->connection = ngx_get_connection(0, pc->log);
    return NGX_AGAIN;
#endif

STOMP_CONN_ERROR:
    if (stss->sess) {
        ngx_close_socket(stss->sess->sock);
        stss->sess = NULL;
    }
    ngx_unlock(&stss->in_use);
    ngx_log_error(NGX_LOG_ERR, pc->log, 0, "ngx_stomp: failed to get a free nginx connection");
#if defined(nginx_version) && (nginx_version >= 8017)
    return NGX_ERROR;
#else
    /* To generate error response */
    pc->connection = ngx_get_connection(0, pc->log);

    return NGX_AGAIN;
#endif

STOMP_OUT_OF_USAGE:
    ngx_log_error(NGX_LOG_ERR, pc->log, 0, "ngx_stomp: out of session usage, retry");
    pc->connection = NULL;
    ngx_http_frame_error_response("ngx_stomp: out of session usage, please expand session usage or retry later");
    return NGX_ERROR;
}

static void
ngx_stomp_upstream_free_peer(ngx_peer_connection_t *pc, void *data,
                             ngx_uint_t state)
{

    ngx_http_stomp_sess_t  *stss = data;
    // ngx_event_t *rev, *wev;
    ngx_connection_t *c = pc->connection;
    if ( pc->connection != NULL  /*&& (u->headers_in.status_n == NGX_HTTP_OK) */) {
        if (c->read->timer_set) {
            ngx_del_timer(c->read);
        }

        if (c->write->timer_set) {
            ngx_del_timer(c->write);
        }

        if (c->write->active && (ngx_event_flags & NGX_USE_LEVEL_EVENT)) {
            if (ngx_del_event(c->write, NGX_WRITE_EVENT, 0) != NGX_OK) {
                return;
            }
        }

        pc->connection = NULL;

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                       "ngx_stomp: free keepalive peer: saving connection %p",
                       c);


        stss->conn = c;

        c->write->handler = ngx_stomp_keepalive_dummy_handler;
        c->read->handler = ngx_stomp_keepalive_dummy_handler;//ngx_ngx_stomp_keepalive_close_handler;

        c->data = stss;
        c->idle = 1;
        c->log = ngx_cycle->log;
#if defined(nginx_version) && (nginx_version >= 1001004)
        c->pool->log = ngx_cycle->log;
#endif
        c->read->log = ngx_cycle->log;
        c->write->log = ngx_cycle->log;
        // stss->socklen = pc->socklen;
        // ngx_memcpy(stss->sockaddr, pc->sockaddr, pc->socklen);
        // stss->name.data = pc->name->data;
        // stss->name.len = pc->name->len;

        ngx_log_error(NGX_LOG_DEBUG, pc->log, 0, "ngx_stomp: release session");
        // ngx_unlock(&stpeer->in_use);
    }
}

static ngx_int_t
ngx_http_stomp_parse_direct_frame_message(ngx_http_request_t *r, ngx_http_stomp_loc_conf_t *cslcf,
        ngx_http_stomp_ctx_t *ctx, ngx_http_stomp_sess_t *stss, ngx_str_t *direct_frame_str) {

    cstmp_frame_t                  *frame;
    cstmp_reset_frame(stss->frame);
    frame = stss->frame;

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "direct parsing frame message");
    /***
    *   To make sure ask for server acknowledge to prevent the read block
    ***/
    if (!ctx->readonly && !ngx_strstrn(direct_frame_str->data , "receipt:", 8)) {
        u_char *after_cmd = (u_char*) ngx_strchr(direct_frame_str->data, '\n');

        cstmp_add_body_content_and_len(frame, direct_frame_str->data, after_cmd - direct_frame_str->data);
        cstmp_add_body_content_and_len(frame, (u_char*) "\nreceipt:server-ack", 19 * sizeof(u_char));
        cstmp_add_body_content_and_len(frame,  after_cmd, (direct_frame_str->data + direct_frame_str->len) - after_cmd);
    } else {
        cstmp_add_body_content_and_len(frame, direct_frame_str->data, direct_frame_str->len);
    }

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "Direct Frame string %*s", frame->body.last - frame->body.start, frame->body.start);
    return NGX_OK;
}

static ngx_int_t
ngx_http_stomp_parse_frame(ngx_http_request_t *r, ngx_http_stomp_loc_conf_t *cslcf,
                           ngx_http_stomp_ctx_t *ctx, ngx_http_stomp_sess_t *stss, ngx_str_t *command) {
    /** Has been managed by pool clean up **/
    // cstmp_set_malloc_management(ngx_http_stomp_default_malloc, ngx_http_stomp_default_free, r);
    ngx_str_t                           headers;
    ngx_str_t                           body;
    u_char                              *head_ptr, *end_head_ptr, *p;
    cstmp_frame_t                       *frame;
    cstmp_frame_val_t                   receipt_header;

    frame = stss->frame;

    cstmp_reset_frame(frame);

    if (!frame) {
        return NGX_ERROR;
    }

    /* If DISCONNECT? */
    if ((cslcf->headers.value.len) ) {
        frame->cmd = command->data;

        if (ngx_http_complex_value(r, &cslcf->headers, &headers) != NGX_OK) {
            return NGX_ERROR;
        }

        head_ptr = headers.data;
        end_head_ptr = headers.data + headers.len;

        while ( (p = ngx_strlchr(head_ptr, end_head_ptr, '\n')) ) {
            cstmp_add_header_str_and_len(frame, head_ptr, p - head_ptr);
            head_ptr = p + 1;
        }

        /***
        *   To make sure ask for server acknowledge to prevent the read block
        ***/
        if (!ctx->readonly) {
            if (!cstmp_get_header(frame, (u_char*)"receipt", &receipt_header )) {
                cstmp_add_header(frame, (u_char*)"receipt", (u_char*)"server-ack");
            }
        }

        if (cslcf->body.value.len) {
            if (ngx_http_complex_value(r, &cslcf->body, &body) != NGX_OK) {
                return NGX_ERROR;
            }
            cstmp_add_body_content_and_len(frame, body.data, body.len);
        }
        return NGX_OK;
    } else {
        return NGX_DECLINED;
    }
}

static void
ngx_stomp_keepalive_dummy_handler(ngx_event_t *ev) {

}