# vi:filetype=perl

use lib '/home/booking/nginx_build/test-nginx/inc';
use lib '/home/booking/nginx_build/test-nginx/lib';
use Test::Nginx::Socket 'no_plan';

our $http_config = <<'_EOC_';
    upstream stomp {
        stomp_subscribes /amq/queue/stompqueue;
        stomp_endpoint 127.0.0.1:61618 login=guest passcode=guest max_send_sess=3 max_recv_sess=3;
    }
_EOC_

no_shuffle();
run_tests();

#no_diff();

__DATA__


=== TEST 1: test sending via GET
--- http_config eval: $::http_config
--- config
	location /sendGet {
	   stomp_pass stomp;
	   stomp_command SEND;
	   stomp_headers "destination:/amq/queue/stompqueue
	                  persistent:false
	                  content-type:text/plain";
	   stomp_body "This is new message from stomp";
	}
--- request
GET /sendGet
--- error_code: 200
--- timeout: 10
--- response_headers
receipt-id: server-ack


=== TEST 2: test sending via POST
--- http_config eval: $::http_config
--- config
	location /sendPost {
	   stomp_pass stomp;
	   stomp_command SEND;
	   stomp_headers "destination:/amq/queue/stompqueue
	                  persistent:false
	                  content-type:text/plain";
       stomp_body $request_body;
	}
--- request
POST /sendPost
{this is the message from STOMP}
--- error_code: 200
--- timeout: 10
--- response_headers
receipt-id: server-ack


=== TEST 3: test consuming via GET
--- http_config eval: $::http_config
--- config
	location /consume {
	   stomp_pass stomp;
	   stomp_command CONSUME;
	}
--- request
POST /consume
--- error_code: 200
--- timeout: 10
--- response_headers
frame: MESSAGE


=== TEST 4: test consuming queue again
--- http_config eval: $::http_config
--- config
	location /consume {
	   stomp_pass stomp;
	   stomp_command CONSUME;
	}
--- request
GET /consume
--- error_code: 200
--- timeout: 10
--- response_headers
frame: MESSAGE