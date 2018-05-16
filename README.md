fcgihttp
========

FCGI to HTTP(S) proxy.  This is just a proof of concept.

> DON'T USE IN PRODUCTION

# Example

	mkdir -p /var/fcgihttp/etc/ssl
	cp fcgihttp /var/fcgihttp/
	cp /etc/resolv.conf /var/fcgihttp/etc/
	cp /etc/ssl/cert.pem /var/fcgihttp/etc/ssl/
	kfcgi -d -u www -n 2 -r -p /var/fcgihttp -- /fcgihttp www.example.com 443

# TODO

* Stream HTTP response (use custom function instead of http_get())
* ...
