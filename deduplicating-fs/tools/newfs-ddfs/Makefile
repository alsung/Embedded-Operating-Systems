#	@(#)Makefile	8.2 (Berkeley) 3/27/94
# $FreeBSD$

PROG=	newfs-ddfs
# XXX(ddfs): warning about convering out-of-tree LIBADD to LDADD, 
# but it seems to work fine.
LIBADD=	ufs util
SRCS=	newfs.c mkfs.c geom_bsd_enc.c
# XXX(ddfs): no man page
MAN=
CFLAGS+=-I../../src

# WARNS?=	3

.include <bsd.prog.mk>
