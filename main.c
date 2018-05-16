/*
 * Copyright (c) 2018 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

#include <kcgi.h>
#include "http.h"

static int connect_timeout = 3 * 1000;

enum pageids {
	PAGE_INDEX,
	PAGE__MAX
};

const char *pagenames[PAGE__MAX] = {
	".",
};

void
page_error(struct kreq	*r, int code)
{
	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[code]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE],
	    "%s", kmimetypes[KMIME_TEXT_PLAIN]);
	khttp_body(r);
	khttp_puts(r, khttps[code]);
	khttp_free(r);
}

int
resolve_host(struct source *ip, const char *host, const char *port)
{
	static char	 hbuf[256];
	struct addrinfo	 hints, *res, *res0;
	int		 error;

	ip->ip = NULL;
	ip->family = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if ((error = getaddrinfo(host, port, &hints, &res0)) != 0)
		errx(1, "%s", gai_strerror(error));
	for (res = res0; res; res = res->ai_next) {
		if (getnameinfo(res->ai_addr, res->ai_addrlen,
		    hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST) != 0)
			continue;
		ip->ip = hbuf;
		ip->family = AF_INET ? 4 : 6;
		break;
	}
	freeaddrinfo(res0);

	return (ip->family ? 0 : -1);
}

int
main(int argc, char *argv[])
{
	struct kfcgi	*fcgi;
	struct kreq	 r;
	struct httpget	*g = NULL;
	struct source	 ip;
	struct httphead	*reqhead[16];
	char		*urn;
	const char	*host, *port = "80", *errstr;
	size_t		 i, j, portnum;

	if (argc < 2)
		errx(1, "usage: %s host [port]", argv[0]);
	host = argv[1];
	port = argc > 2 ? argv[2] : port;

	portnum = strtonum(port, 1, 0xffff, &errstr);
	if (errstr != NULL)
		errx(1, "port: %s", errstr);

	if (resolve_host(&ip, host, port) == -1)
		errx(1, "%s", argv[1]);

	if (http_init() == -1)
		errx(1, "http_init");

	if (khttp_fcgi_init(&fcgi, NULL, 0,
	    pagenames, PAGE__MAX, 0) != KCGI_OK)
		errx(1, "khttp_fcgi_init");

	if (pledge("stdio recvfd inet dns", NULL) == -1)
		err(1, "pledge");

	while (khttp_fcgi_parse(fcgi, &r) == KCGI_OK) {
		if (resolve_host(&ip, host, port) == -1) {
			page_error(&r, 502);
			continue;
		}
		urn = *r.fullpath == '\0' ? "/" : r.fullpath;

		for (i = j = 0; i < r.reqsz; i++) {
			if (strcasecmp("Host", r.reqs[i].key) == 0 ||
			    strcasecmp("Connection", r.reqs[i].key) == 0)
				continue;
			reqhead[j] = (struct httphead *)&r.reqs[i];
			j++;
		}
		reqhead[j++] = &(struct httphead){ "Connection", "close" };
		reqhead[j] = NULL;

		if ((g = http_get(&ip, 1, argv[1], portnum,
		    urn, NULL, 0, reqhead)) == NULL) {
			warnx("http_get");
			page_error(&r, 502);
			continue;
		}
		if (!g->code) {
			page_error(&r, 500);
			http_get_free(g);
			continue;
		}

		khttp_head(&r, kresps[KRESP_STATUS], "%d", g->code);
		for (i = 0; i < g->headsz; i++) {
			if (strcasecmp("Connection", g->head[i].key) == 0 ||
			    strcasecmp("Server", g->head[i].key) == 0 ||
			    strcasecmp("Status", g->head[i].key) == 0)
				continue;
			khttp_head(&r, g->head[i].key,
			    "%s", g->head[i].val);
		}
		khttp_head(&r, "Connection", "close");
		khttp_body(&r);
		khttp_write(&r, g->bodypart, g->bodypartsz);

		http_get_free(g);
		khttp_free(&r);
	}

	khttp_fcgi_free(fcgi);

	return (0);
}

int
connect_wait(int s, const struct sockaddr *name, socklen_t namelen)
{
	struct pollfd	 pfd[1];
	int		 error = 0, flag;
	socklen_t	 errlen = sizeof(error);

	if ((flag = fcntl(s, F_GETFL, 0)) == -1 ||
	    (fcntl(s, F_SETFL, flag | O_NONBLOCK)) == -1)
		return (-1);

	error = connect(s, name, namelen);
	do {
		pfd[0].fd = s;
		pfd[0].events = POLLOUT;

		if ((error = poll(pfd, 1, connect_timeout)) == -1)
			continue;
		if (error == 0) {
			error = ETIMEDOUT;
			goto done;
		}
		if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &errlen) == -1)
			continue;
	} while (error != 0 && error == EINTR);

 done:
	if (fcntl(s, F_SETFL, flag & ~O_NONBLOCK) == -1)
		return (-1);

	if (error != 0) {
		errno = error;
		return (-1);
	}

	return (0);
}
