
ngx-stomp
=========

A STOMP upstream module on nginx, [STOMP](https://stomp.github.io/) is the Simple (or Streaming) Text Orientated Messaging Protocol.

Table of Contents
=================

* [Introduction](#introduction)
* [Usage](#usage)
* [Response Format](#response-format)
* [Directive](#directive)
* [Installation](#installation)
* [Test](#test)
* [Support](#support)
* [Copyright & License](#copyright--license)

Introduction
============

ngx-stomp is a nginx 3rd party module which allow to send http request and proxy to stomp amqp protocol server with given pre-defined/dynamic frame.


Usage
=======
### 0. Setup your stomp upstream and subscribed queue
```nginx
# nginx.conf
  upstream stomp {
        stomp_subscribes /amq/queue/stompqueue;
        stomp_endpoint 127.0.0.1:61618 login=guest passcode=guest max_send_sess=3 max_recv_sess=3;
    }
```


### 1. Simple create stomp frame command, 
```nginx
# nginx.conf

server {
    ....
    location /sendqueue {
	   stomp_pass stomp;
	   stomp_command SEND;
	   stomp_headers "destination:/amq/queue/stompqueue
					  persistent:false
            content-type:text/plain
					  content-length:38";
	   stomp_body "This is new message sending from stomp";
	}
}
```


### 2. Simple consuming the queue destination which pre-defined from upstream
```nginx
# nginx.conf

server {
  ....
  
    location /consume {
	   stomp_pass stomp;
	   stomp_command CONSUME;
	}
}
```

[Back to TOC](#table-of-contents)

Response Format
===============
Http Headers will be Frame Command and headers, Response content will be Frame body content.


[Back to TOC](#table-of-contents)

Directive                                                                                                                                                  
=========           
                                                                                                                                                           
| Key               | Description                                                                                                                           |
| -------------     |:-------------:                                                                                                                        |
| stomp_subscribes  | which need to defined in upstream block, to tell that which destination queue going to subscribe                                      |
| stomp_endpoint    | the AMQ server which has stomp protocol feature.                                                                                      |
| max_send_sess     | the maximum connection which allow to use for sending message to amq server, default is 10                                            |
| max_recv_sess     | the maximum connection which allow to use for consuming message from amq server, default is 5                                         |
| stomp_pass        | upstream to stomp.                                                                                                                    |
| stomp_command     | frame command ( Please note that 'CONSUME' command is the extra command which allow you to consume the message via HTTP request )     |
| stomp_headers     | frame headers when sending the message                                                                                                |
| stomp_body        | message content for sending                                                                                                           |
        

[Back to TOC](#table-of-contents)           

Installation
============

```bash
wget 'http://nginx.org/download/nginx-1.13.7.tar.gz'
tar -xzvf nginx-1.13.7.tar.gz
cd nginx-1.13.7/

./configure --add-module=/path/to/ngx-stomp

make -j2
sudo make install
```

[Back to TOC](#table-of-contents)


Test
=====

It depends on nginx test suite libs, please refer [test-nginx](https://github.com/openresty/test-nginx) for installation.


```bash
cd /path/to/ngx-stomp
export PATH=/path/to/nginx-dirname:$PATH 
sudo prove t
```

[Back to TOC](#table-of-contents)


Support
=======

Please do not hesitate to contact minikawoon2017@gmail.com for any queries or development improvement.


[Back to TOC](#table-of-contents)

Copyright & License
===================

This module is licensed under the terms of the BSD 3-Clause License.

Copyright (c) 2018, Taymindis Woon <cloudleware2015@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


[Back to TOC](#table-of-contents)
