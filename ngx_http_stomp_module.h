#ifndef _NGX_HTTP_UPSTREAM_STOMP_H_
#define _NGX_HTTP_UPSTREAM_STOMP_H_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <cstomp.h>

typedef struct {
    ngx_uint_t stmp_listen_port;
    ngx_array_t stomp_servers;  /* ngx_http_file_cache_t * */
} ngx_http_stomp_main_conf_t;

typedef struct {
    ngx_http_upstream_conf_t   upstream;
    ngx_http_complex_value_t   command;
    ngx_http_complex_value_t   headers;
    ngx_http_complex_value_t   body;
    ngx_http_complex_value_t   whole_frame;
    ngx_flag_t                 has_subs;
} ngx_http_stomp_loc_conf_t;

typedef struct {
    ngx_array_t                        *subscribe_queues;
    ngx_array_t                        *servers;
} ngx_http_stomp_upstream_srv_conf_t;

typedef struct {
#if defined(nginx_version) && (nginx_version >= 8022)
    ngx_addr_t                         *addrs;
#else
    ngx_peer_addr_t                    *addrs;
#endif
    ngx_uint_t                          naddrs;
    in_port_t                           port;
    // ngx_str_t                           headers;
    ngx_http_complex_value_t            username_val;
    ngx_http_complex_value_t            password_val;
    ngx_uint_t max_send_sess;
    ngx_uint_t max_recv_sess;
} ngx_stomp_upstream_server_t;

typedef enum {
    stomp_connect,
    stomp_send,
    stomp_sub,
    stomp_read,
    stomp_ack,
    stomp_resp,
    stomp_idle,
    stomp_error
} ngx_stomp_state_t;

typedef struct {
    cstmp_session_t       *sess;
    ngx_stomp_state_t     state;
    cstmp_frame_t         *frame;
    ngx_flag_t            is_direct_frame; // if it's direct frame, only frame->body will use
    // ngx_str_t             username;
    // ngx_str_t             password;
    ngx_str_t             *dest_queues;
    ngx_connection_t      *conn;
    ngx_atomic_t          in_use;
} ngx_http_stomp_sess_t;

typedef struct {
    struct sockaddr *sockaddr;
    socklen_t   socklen;
    ngx_str_t   name;
    ngx_str_t   host;
    in_port_t   port;
    ngx_http_complex_value_t   *username_val;
    ngx_http_complex_value_t   *password_val;
    ngx_array_t *stomp_send_sessions;
    ngx_array_t *stomp_recv_sessions;
} ngx_http_stomp_upstream_peer_t;

typedef struct {
    ngx_uint_t                          number;
    ngx_str_t                          *name;
    ngx_http_stomp_upstream_peer_t     peer[0];
} ngx_http_stomp_upstream_peers_t;

typedef struct {
    ngx_http_stomp_upstream_peers_t *peers;
    // ngx_str_t                 name;
    // struct sockaddr           sockaddr;
    ngx_array_t *subscribe_queues;
    ngx_http_request_t *r;
} ngx_http_stomp_upstream_peer_data_t;


typedef struct {
    // ngx_http_request_t        *request;
    // size_t                    body_offset;
    ngx_flag_t                readonly;
    ngx_list_t                *headers;
    ngx_chain_t               *response;
} ngx_http_stomp_ctx_t;


#endif /*_NGX_HTTP_UPSTREAM_STOMP_H_*/