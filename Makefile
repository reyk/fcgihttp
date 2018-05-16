PROG=		fcgihttp
SRCS=		main.c http.c
NOMAN=		yes

CFLAGS+=	-Wall
CFLAGS+=        -I/usr/local/include

LDADD=		-levent -ltls -lssl -lcrypto 
DPADD=		${LIBTLS} ${LIBCRYPTO} ${LIBSSL} ${LIBEVENT}

LDADD+=         -L/usr/local/lib
LDADD+=		-lkcgi -lz

.include <bsd.prog.mk>
