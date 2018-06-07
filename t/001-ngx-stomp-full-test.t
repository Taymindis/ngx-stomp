# vi:filetype=perl

use lib '/home/booking/nginx_build/test-nginx/inc';
use lib '/home/booking/nginx_build/test-nginx/lib';
use Test::Nginx::Socket 'no_plan';


# $ENV{TEST_STOMP_RABBIT_MQ_HOST} ||= '127.0.0.1';
# $ENV{TEST_STOMP_RABBIT_MQ_PORT} ||= 61618;
# $ENV{TEST_STOMP_RABBIT_MQ_LOGIN} ||= 'guest';
# $ENV{TEST_STOMP_RABBIT_MQ_PASSCODE} ||= 'guest';
# $ENV{TEST_STOMP_RABBIT_MQ_QUEUES} ||= '/amq/queue/stompqueue';

our $http_config = <<'_EOC_';
    upstream stomp {
        stomp_subscribes /amq/queue/stompqueue;
        stomp_endpoint 127.0.0.1:61618 login=guest passcode=guest;
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