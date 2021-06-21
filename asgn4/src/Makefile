KMOD=ddfs
SRCS=opt_ddb.h opt_directio.h opt_ffs.h opt_quota.h opt_suiddir.h opt_ufs.h \
	ddfs_alloc.c ddfs_balloc.c ddfs_inode.c ddfs_rawread.c \
	ddfs_snapshot.c ddfs_softdep.c ddfs_subr.c ddfs_suspend.c ddfs_tables.c \
	ddfs_vfsops.c ddfs_vnops.c ddfs_util.c \
	vnode_if.h
# turn off ffs snapshot support to avoid sysctl warnings
CFLAGS+=-DNO_FFS_SNAPSHOT -I.

.include <bsd.kmod.mk>
