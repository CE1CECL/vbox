/* $Id$ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, utility functions.
 *
 * Utility functions (mainly conversion from/to VirtualBox/Linux data structures).
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "vfsmod.h"
#include <iprt/asm.h>
#include <iprt/err.h>
#include <linux/nfs_fs.h>
#include <linux/vfs.h>

/*
 * sf_reg_aops and sf_backing_dev_info are just quick implementations to make
 * sendfile work. For more information have a look at
 *
 *   http://us1.samba.org/samba/ftp/cifs-cvs/ols2006-fs-tutorial-smf.odp
 *
 * and the sample implementation
 *
 *   http://pserver.samba.org/samba/ftp/cifs-cvs/samplefs.tar.gz
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)

DECLINLINE(void) sf_ftime_from_timespec(time_t * time, RTTIMESPEC *ts)
{
	int64_t t = RTTimeSpecGetNano(ts);
	do_div(t, RT_NS_1SEC);
	*time = t;
}

DECLINLINE(void) sf_timespec_from_ftime(RTTIMESPEC * ts, time_t *time)
{
	RTTimeSpecSetNano(ts, RT_NS_1SEC_64 * *time);
}

#else	/* >= 2.6.0 */

# if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
DECLINLINE(void) sf_ftime_from_timespec(struct timespec *tv, RTTIMESPEC *ts)
# else
DECLINLINE(void) sf_ftime_from_timespec(struct timespec64 *tv, RTTIMESPEC *ts)
# endif
{
	int64_t t = RTTimeSpecGetNano(ts);
	tv->tv_nsec = do_div(t, RT_NS_1SEC);
	tv->tv_sec  = t;
}

# if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
DECLINLINE(void) sf_timespec_from_ftime(RTTIMESPEC *ts, struct timespec *tv)
# else
DECLINLINE(void) sf_timespec_from_ftime(RTTIMESPEC *ts, struct timespec64 *tv)
# endif
{
	RTTimeSpecSetNano(ts, tv->tv_nsec + tv->tv_sec * (int64_t)RT_NS_1SEC);
}

#endif	/* >= 2.6.0 */

/* set [inode] attributes based on [info], uid/gid based on [sf_g] */
void sf_init_inode(struct inode *inode, struct sf_inode_info *sf_i, PSHFLFSOBJINFO info, struct sf_glob_info *sf_g)
{
	PCSHFLFSOBJATTR attr = &info->Attr;
	int mode;

	TRACE();

	sf_i->ts_up_to_date = jiffies;
	sf_i->force_restat  = 0;

#define mode_set(r) attr->fMode & (RTFS_UNIX_##r) ? (S_##r) : 0;
	mode = mode_set(IRUSR);
	mode |= mode_set(IWUSR);
	mode |= mode_set(IXUSR);

	mode |= mode_set(IRGRP);
	mode |= mode_set(IWGRP);
	mode |= mode_set(IXGRP);

	mode |= mode_set(IROTH);
	mode |= mode_set(IWOTH);
	mode |= mode_set(IXOTH);

#undef mode_set

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
	inode->i_mapping->a_ops = &sf_reg_aops;
# if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 19, 0)
	inode->i_mapping->backing_dev_info = &sf_g->bdi; /* This is needed for mmap. */
# endif
#endif

	if (RTFS_IS_DIRECTORY(attr->fMode)) {
		inode->i_mode = sf_g->dmode != ~0 ? (sf_g->dmode & 0777) : mode;
		inode->i_mode &= ~sf_g->dmask;
		inode->i_mode |= S_IFDIR;
		inode->i_op = &sf_dir_iops;
		inode->i_fop = &sf_dir_fops;
		/* XXX: this probably should be set to the number of entries
		   in the directory plus two (. ..) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
		set_nlink(inode, 1);
#else
		inode->i_nlink = 1;
#endif
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 8)
	else if (RTFS_IS_SYMLINK(attr->fMode)) {
		inode->i_mode = sf_g->fmode != ~0 ? (sf_g->fmode & 0777) : mode;
		inode->i_mode &= ~sf_g->fmask;
		inode->i_mode |= S_IFLNK;
		inode->i_op = &sf_lnk_iops;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
		set_nlink(inode, 1);
# else
		inode->i_nlink = 1;
# endif
	}
#endif
	else {
		inode->i_mode = sf_g->fmode != ~0 ? (sf_g->fmode & 0777) : mode;
		inode->i_mode &= ~sf_g->fmask;
		inode->i_mode |= S_IFREG;
		inode->i_op = &sf_reg_iops;
		inode->i_fop = &sf_reg_fops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
		set_nlink(inode, 1);
#else
		inode->i_nlink = 1;
#endif
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
	inode->i_uid = make_kuid(current_user_ns(), sf_g->uid);
	inode->i_gid = make_kgid(current_user_ns(), sf_g->gid);
#else
	inode->i_uid = sf_g->uid;
	inode->i_gid = sf_g->gid;
#endif

	inode->i_size = info->cbObject;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19) && !defined(KERNEL_FC6)
	inode->i_blksize = 4096;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 11)
	inode->i_blkbits = 12;
#endif
	/* i_blocks always in units of 512 bytes! */
	inode->i_blocks = (info->cbAllocated + 511) / 512;

	sf_ftime_from_timespec(&inode->i_atime, &info->AccessTime);
	sf_ftime_from_timespec(&inode->i_ctime, &info->ChangeTime);
	sf_ftime_from_timespec(&inode->i_mtime, &info->ModificationTime);
}

/**
 * Update the inode with new object info from the host.
 */
void sf_update_inode(struct inode *pInode, struct sf_inode_info *pInfoInfo, PSHFLFSOBJINFO pObjInfo, struct sf_glob_info *sf_g)
{
	/** @todo  make lock/rcu safe.  */
	sf_init_inode(pInode, pInfoInfo, pObjInfo, sf_g);
}


int sf_stat(const char *caller, struct sf_glob_info *sf_g,
	    SHFLSTRING *path, PSHFLFSOBJINFO result, int ok_to_fail)
{
	int rc;
	VBOXSFCREATEREQ *pReq;
	NOREF(caller);

	TRACE();

	pReq = (VBOXSFCREATEREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq) + path->u16Size);
	if (pReq) {
		RT_ZERO(*pReq);
		memcpy(&pReq->StrPath, path, SHFLSTRING_HEADER_SIZE + path->u16Size);
		pReq->CreateParms.Handle = SHFL_HANDLE_NIL;
		pReq->CreateParms.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;

		LogFunc(("Calling VbglR0SfHostReqCreate on %s\n", path->String.utf8));
		rc = VbglR0SfHostReqCreate(sf_g->map.root, pReq);
		if (RT_SUCCESS(rc)) {
			if (pReq->CreateParms.Result == SHFL_FILE_EXISTS) {
				*result = pReq->CreateParms.Info;
				rc = 0;
			} else {
				if (!ok_to_fail)
					LogFunc(("VbglR0SfHostReqCreate on %s: file does not exist: %d (caller=%s)\n",
						 path->String.utf8, pReq->CreateParms.Result, caller));
				rc = -ENOENT;
			}
		} else if (rc == VERR_INVALID_NAME) {
			rc = -ENOENT; /* this can happen for names like 'foo*' on a Windows host */
		} else {
			LogFunc(("VbglR0SfHostReqCreate failed on %s: %Rrc (caller=%s)\n", path->String.utf8, rc, caller));
			rc = -EPROTO;
		}
		VbglR0PhysHeapFree(pReq);
	}
	else
		rc = -ENOMEM;
	return rc;
}

/**
 * Revalidate an inode.
 *
 * This is called directly as inode-op on 2.4, indirectly as dir-op
 * sf_dentry_revalidate() on 2.4/2.6, indirectly as inode-op through
 * sf_getattr() on 2.6.  The job is to find out whether dentry/inode is still
 * valid.  The test fails if @a dentry does not have an inode or sf_stat() is
 * unsuccessful, otherwise we return success and update inode attributes.
 */
int sf_inode_revalidate(struct dentry *dentry)
{
	int rc;
	struct inode *pInode = dentry ? dentry->d_inode : NULL;
	if (pInode) {
		struct sf_inode_info *sf_i = GET_INODE_INFO(pInode);
		struct sf_glob_info  *sf_g = GET_GLOB_INFO(pInode->i_sb);
		AssertReturn(sf_i, -EINVAL);
		AssertReturn(sf_g, -EINVAL);

		/*
		 * Can we get away without any action here?
		 */
		if (   !sf_i->force_restat
		    && jiffies - sf_i->ts_up_to_date < sf_g->ttl)
			rc = 0;
		else {
			/*
			 * No, we have to query the file info from the host.
			 * Try get a handle we can query, any kind of handle will do here.
			 */
			struct sf_handle *pHandle = sf_handle_find(sf_i, 0, 0);
			if (pHandle) {
				/* Query thru pHandle. */
				VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
				if (pReq) {
					RT_ZERO(*pReq);
					rc = VbglR0SfHostReqQueryObjInfo(sf_g->map.root, pReq, pHandle->hHost);
					if (RT_SUCCESS(rc)) {
						/*
						 * Reset the TTL and copy the info over into the inode structure.
						 */
						sf_update_inode(pInode, sf_i, &pReq->ObjInfo, sf_g);
					} else if (rc == VERR_INVALID_HANDLE) {
						rc = -ENOENT; /* Restore.*/
					} else {
						LogFunc(("VbglR0SfHostReqQueryObjInfo failed on %#RX64: %Rrc\n", pHandle->hHost, rc));
						rc = -RTErrConvertToErrno(rc);
					}
					VbglR0PhysHeapFree(pReq);
				} else
					rc = -ENOMEM;
				sf_handle_release(pHandle, sf_g, "sf_inode_revalidate");

			} else {
				/* Query via path. */
				SHFLSTRING      *pPath = sf_i->path;
				VBOXSFCREATEREQ *pReq  = (VBOXSFCREATEREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq) + pPath->u16Size);
				if (pReq) {
					RT_ZERO(*pReq);
					memcpy(&pReq->StrPath, pPath, SHFLSTRING_HEADER_SIZE + pPath->u16Size);
					pReq->CreateParms.Handle      = SHFL_HANDLE_NIL;
					pReq->CreateParms.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;

					rc = VbglR0SfHostReqCreate(sf_g->map.root, pReq);
					if (RT_SUCCESS(rc)) {
						if (pReq->CreateParms.Result == SHFL_FILE_EXISTS) {
							/*
							 * Reset the TTL and copy the info over into the inode structure.
							 */
							sf_update_inode(pInode, sf_i, &pReq->CreateParms.Info, sf_g);
							rc = 0;
						} else {
							rc = -ENOENT;
						}
					} else if (rc == VERR_INVALID_NAME) {
						rc = -ENOENT; /* this can happen for names like 'foo*' on a Windows host */
					} else {
						LogFunc(("VbglR0SfHostReqCreate failed on %s: %Rrc\n", pPath->String.ach, rc));
						rc = -EPROTO;
					}
					VbglR0PhysHeapFree(pReq);
				}
				else
					rc = -ENOMEM;
			}
		}
	} else {
		LogFunc(("no dentry(%p) or inode(%p)\n", dentry, pInode));
		rc = -EINVAL;
	}
	return rc;
}

/**
 * Similar to sf_inode_revalidate, but uses associated host file handle as that
 * is quite a bit faster.
 */
int sf_inode_revalidate_with_handle(struct dentry *dentry, SHFLHANDLE hHostFile, bool fForced)
{
	int err;
	struct inode *pInode = dentry ? dentry->d_inode : NULL;
	if (!pInode) {
		LogFunc(("no dentry(%p) or inode(%p)\n", dentry, pInode));
		err = -EINVAL;
	} else {
		struct sf_inode_info *sf_i = GET_INODE_INFO(pInode);
		struct sf_glob_info  *sf_g = GET_GLOB_INFO(pInode->i_sb);
		AssertReturn(sf_i, -EINVAL);
		AssertReturn(sf_g, -EINVAL);

		/*
		 * Can we get away without any action here?
		 */
		if (   !fForced
		    && !sf_i->force_restat
		    && jiffies - sf_i->ts_up_to_date <= sf_g->ttl)
			err = 0;
		else {
			/*
			 * No, we have to query the file info from the host.
			 */
			VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
			if (pReq) {
				RT_ZERO(*pReq);
				err = VbglR0SfHostReqQueryObjInfo(sf_g->map.root, pReq, hHostFile);
				if (RT_SUCCESS(err)) {
					/*
					 * Reset the TTL and copy the info over into the inode structure.
					 */
					sf_update_inode(pInode, sf_i, &pReq->ObjInfo, sf_g);
				} else {
					LogFunc(("VbglR0SfHostReqQueryObjInfo failed on %#RX64: %Rrc\n", hHostFile, err));
					err = -RTErrConvertToErrno(err);
				}
				VbglR0PhysHeapFree(pReq);
			} else
				err = -ENOMEM;
		}
	}
	return err;
}

/* on 2.6 this is a proxy for [sf_inode_revalidate] which (as a side
   effect) updates inode attributes for [dentry] (given that [dentry]
   has inode at all) from these new attributes we derive [kstat] via
   [generic_fillattr] */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)

# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
int sf_getattr(const struct path *path, struct kstat *kstat, u32 request_mask,
	       unsigned int flags)
# else
int sf_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *kstat)
# endif
{
	int            rc;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	struct dentry *dentry = path->dentry;
# endif

# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	SFLOGFLOW(("sf_getattr: dentry=%p request_mask=%#x flags=%#x\n", dentry, request_mask, flags));
# else
	SFLOGFLOW(("sf_getattr: dentry=%p\n", dentry));
# endif

	rc = sf_inode_revalidate(dentry);
	if (rc == 0) {
		generic_fillattr(dentry->d_inode, kstat);

		/*
		 * FsPerf shows the following numbers for sequential file access against
		 * a tmpfs folder on an AMD 1950X host running debian buster/sid:
		 *
		 * block size = r128600    ----- r128755 -----
		 *               reads      reads     writes
		 *    4096 KB = 2254 MB/s  4953 MB/s 3668 MB/s
		 *    2048 KB = 2368 MB/s  4908 MB/s 3541 MB/s
		 *    1024 KB = 2208 MB/s  4011 MB/s 3291 MB/s
		 *     512 KB = 1908 MB/s  3399 MB/s 2721 MB/s
		 *     256 KB = 1625 MB/s  2679 MB/s 2251 MB/s
		 *     128 KB = 1413 MB/s  1967 MB/s 1684 MB/s
		 *      64 KB = 1152 MB/s  1409 MB/s 1265 MB/s
		 *      32 KB =  726 MB/s   815 MB/s  783 MB/s
		 *      16 KB =             683 MB/s  475 MB/s
		 *       8 KB =             294 MB/s  286 MB/s
		 *       4 KB =  145 MB/s   156 MB/s  149 MB/s
		 *
		 */
		if (S_ISREG(kstat->mode))
			kstat->blksize = _1M;
		else if (S_ISDIR(kstat->mode))
			/** @todo this may need more tuning after we rewrite the directory handling. */
			kstat->blksize = _16K;
	}
	return rc;
}

int sf_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct sf_glob_info *sf_g;
	struct sf_inode_info *sf_i;
	union SetAttrReqs
	{
	    VBOXSFCREATEREQ         Create;
	    VBOXSFOBJINFOREQ        Info;
	    VBOXSFSETFILESIZEREQ    SetSize;
	    VBOXSFCLOSEREQ          Close;
	} *pReq;
	size_t cbReq;
	SHFLCREATEPARMS *pCreateParms;
	SHFLFSOBJINFO *pInfo;
	SHFLHANDLE hHostFile;
	int vrc;
	int err = 0;

	TRACE();

	sf_g = GET_GLOB_INFO(dentry->d_inode->i_sb);
	sf_i = GET_INODE_INFO(dentry->d_inode);
	cbReq = RT_MAX(sizeof(pReq->Info), sizeof(pReq->Create) + SHFLSTRING_HEADER_SIZE + sf_i->path->u16Size);
	pReq = (union SetAttrReqs *)VbglR0PhysHeapAlloc(cbReq);
	if (!pReq) {
		LogFunc(("Failed to allocate %#x byte request buffer!\n", cbReq));
		return -ENOMEM;
	}
	pCreateParms = &pReq->Create.CreateParms;
	pInfo = &pReq->Info.ObjInfo;

	RT_ZERO(*pCreateParms);
	pCreateParms->Handle = SHFL_HANDLE_NIL;
	pCreateParms->CreateFlags = SHFL_CF_ACT_OPEN_IF_EXISTS
				  | SHFL_CF_ACT_FAIL_IF_NEW
				  | SHFL_CF_ACCESS_ATTR_WRITE;

	/* this is at least required for Posix hosts */
	if (iattr->ia_valid & ATTR_SIZE)
		pCreateParms->CreateFlags |= SHFL_CF_ACCESS_WRITE;

	memcpy(&pReq->Create.StrPath, sf_i->path, SHFLSTRING_HEADER_SIZE + sf_i->path->u16Size);
	vrc = VbglR0SfHostReqCreate(sf_g->map.root, &pReq->Create);
	if (RT_SUCCESS(vrc)) {
		hHostFile = pCreateParms->Handle;
	} else {
		err = -RTErrConvertToErrno(vrc);
		LogFunc(("VbglR0SfCreate(%s) failed vrc=%Rrc err=%d\n", sf_i->path->String.ach, vrc, err));
		goto fail2;
	}
	if (pCreateParms->Result != SHFL_FILE_EXISTS) {
		LogFunc(("file %s does not exist\n", sf_i->path->String.utf8));
		err = -ENOENT;
		goto fail1;
	}

	/* Setting the file size and setting the other attributes has to be
	 * handled separately, see implementation of vbsfSetFSInfo() in
	 * vbsf.cpp */
	if (iattr->ia_valid & (ATTR_MODE | ATTR_ATIME | ATTR_MTIME)) {
# define mode_set(r) ((iattr->ia_mode & (S_##r)) ? RTFS_UNIX_##r : 0)

		RT_ZERO(*pInfo);
		if (iattr->ia_valid & ATTR_MODE) {
			pInfo->Attr.fMode = mode_set(IRUSR);
			pInfo->Attr.fMode |= mode_set(IWUSR);
			pInfo->Attr.fMode |= mode_set(IXUSR);
			pInfo->Attr.fMode |= mode_set(IRGRP);
			pInfo->Attr.fMode |= mode_set(IWGRP);
			pInfo->Attr.fMode |= mode_set(IXGRP);
			pInfo->Attr.fMode |= mode_set(IROTH);
			pInfo->Attr.fMode |= mode_set(IWOTH);
			pInfo->Attr.fMode |= mode_set(IXOTH);

			if (iattr->ia_mode & S_IFDIR)
				pInfo->Attr.fMode |= RTFS_TYPE_DIRECTORY;
			else
				pInfo->Attr.fMode |= RTFS_TYPE_FILE;
		}

		if (iattr->ia_valid & ATTR_ATIME)
			sf_timespec_from_ftime(&pInfo->AccessTime,
					       &iattr->ia_atime);
		if (iattr->ia_valid & ATTR_MTIME)
			sf_timespec_from_ftime(&pInfo->ModificationTime,
					       &iattr->ia_mtime);
		/* ignore ctime (inode change time) as it can't be set from userland anyway */

		vrc = VbglR0SfHostReqSetObjInfo(sf_g->map.root, &pReq->Info, hHostFile);
		if (RT_FAILURE(vrc)) {
			err = -RTErrConvertToErrno(vrc);
			LogFunc(("VbglR0SfHostReqSetObjInfo(%s) failed vrc=%Rrc err=%d\n", sf_i->path->String.ach, vrc, err));
			goto fail1;
		}
	}

	if (iattr->ia_valid & ATTR_SIZE) {
		vrc = VbglR0SfHostReqSetFileSize(sf_g->map.root, &pReq->SetSize, hHostFile, iattr->ia_size);
		/** @todo Implement fallback if host is < 6.0? */
		if (RT_FAILURE(vrc)) {
			err = -RTErrConvertToErrno(vrc);
			LogFunc(("VbglR0SfHostReqSetFileSize(%s, %#llx) failed vrc=%Rrc err=%d\n",
				 sf_i->path->String.ach, (unsigned long long)iattr->ia_size, vrc, err));
			goto fail1;
		}
	}

	vrc = VbglR0SfHostReqClose(sf_g->map.root, &pReq->Close, hHostFile);
	if (RT_FAILURE(vrc))
		LogFunc(("VbglR0SfHostReqClose(%s [%#llx]) failed vrc=%Rrc\n", sf_i->path->String.utf8, hHostFile, vrc));
	VbglR0PhysHeapFree(pReq);

	/** @todo r=bird: I guess we're calling revalidate here to update the inode
	 * info.  However, due to the TTL optimization this is not guarenteed to happen.
	 *
	 * Also, we already have accurate stat information on the file, either from the
	 * SHFL_FN_CREATE call or from SHFL_FN_INFORMATION, so there is no need to do a
	 * slow stat()-like operation to retrieve the information again.
	 *
	 * What's more, given that the SHFL_FN_CREATE call succeeded, we know that the
	 * dentry and all its parent entries are valid and could touch their timestamps
	 * extending their TTL (CIFS does that). */
	sf_i->force_restat = 1; /* temp fix */
	return sf_inode_revalidate(dentry);

 fail1:
	vrc = VbglR0SfHostReqClose(sf_g->map.root, &pReq->Close, hHostFile);
	if (RT_FAILURE(vrc))
		LogFunc(("VbglR0SfHostReqClose(%s [%#llx]) failed vrc=%Rrc; err=%d\n", sf_i->path->String.utf8, hHostFile, vrc, err));

 fail2:
	VbglR0PhysHeapFree(pReq);
	return err;
}

#endif /* >= 2.6.0 */

static int sf_make_path(const char *caller, struct sf_inode_info *sf_i,
			const char *d_name, size_t d_len, SHFLSTRING ** result)
{
	size_t path_len, shflstring_len;
	SHFLSTRING *tmp;
	uint16_t p_len;
	uint8_t *p_name;
	int fRoot = 0;

	TRACE();
	p_len = sf_i->path->u16Length;
	p_name = sf_i->path->String.utf8;

	if (p_len == 1 && *p_name == '/') {
		path_len = d_len + 1;
		fRoot = 1;
	} else {
		/* lengths of constituents plus terminating zero plus slash  */
		path_len = p_len + d_len + 2;
		if (path_len > 0xffff) {
			LogFunc(("path too long.  caller=%s, path_len=%zu\n",
				 caller, path_len));
			return -ENAMETOOLONG;
		}
	}

	shflstring_len = offsetof(SHFLSTRING, String.utf8) + path_len;
	tmp = kmalloc(shflstring_len, GFP_KERNEL);
	if (!tmp) {
		LogRelFunc(("kmalloc failed, caller=%s\n", caller));
		return -ENOMEM;
	}
	tmp->u16Length = path_len - 1;
	tmp->u16Size = path_len;

	if (fRoot)
		memcpy(&tmp->String.utf8[0], d_name, d_len + 1);
	else {
		memcpy(&tmp->String.utf8[0], p_name, p_len);
		tmp->String.utf8[p_len] = '/';
		memcpy(&tmp->String.utf8[p_len + 1], d_name, d_len);
		tmp->String.utf8[p_len + 1 + d_len] = '\0';
	}

	*result = tmp;
	return 0;
}

/**
 * [dentry] contains string encoded in coding system that corresponds
 * to [sf_g]->nls, we must convert it to UTF8 here and pass down to
 * [sf_make_path] which will allocate SHFLSTRING and fill it in
 */
int sf_path_from_dentry(const char *caller, struct sf_glob_info *sf_g,
			struct sf_inode_info *sf_i, struct dentry *dentry,
			SHFLSTRING ** result)
{
	int err;
	const char *d_name;
	size_t d_len;
	const char *name;
	size_t len = 0;

	TRACE();
	d_name = dentry->d_name.name;
	d_len = dentry->d_name.len;

	if (sf_g->nls) {
		size_t in_len, i, out_bound_len;
		const char *in;
		char *out;

		in = d_name;
		in_len = d_len;

		out_bound_len = PATH_MAX;
		out = kmalloc(out_bound_len, GFP_KERNEL);
		name = out;

		for (i = 0; i < d_len; ++i) {
			/* We renamed the linux kernel wchar_t type to linux_wchar_t in
			   the-linux-kernel.h, as it conflicts with the C++ type of that name. */
			linux_wchar_t uni;
			int nb;

			nb = sf_g->nls->char2uni(in, in_len, &uni);
			if (nb < 0) {
				LogFunc(("nls->char2uni failed %x %d\n",
					 *in, in_len));
				err = -EINVAL;
				goto fail1;
			}
			in_len -= nb;
			in += nb;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
			nb = utf32_to_utf8(uni, out, out_bound_len);
#else
			nb = utf8_wctomb(out, uni, out_bound_len);
#endif
			if (nb < 0) {
				LogFunc(("nls->uni2char failed %x %d\n",
					 uni, out_bound_len));
				err = -EINVAL;
				goto fail1;
			}
			out_bound_len -= nb;
			out += nb;
			len += nb;
		}
		if (len >= PATH_MAX - 1) {
			err = -ENAMETOOLONG;
			goto fail1;
		}

		LogFunc(("result(%d) = %.*s\n", len, len, name));
		*out = 0;
	} else {
		name = d_name;
		len = d_len;
	}

	err = sf_make_path(caller, sf_i, name, len, result);
	if (name != d_name)
		kfree(name);

	return err;

 fail1:
	kfree(name);
	return err;
}

int sf_nlscpy(struct sf_glob_info *sf_g,
	      char *name, size_t name_bound_len,
	      const unsigned char *utf8_name, size_t utf8_len)
{
	if (sf_g->nls) {
		const char *in;
		char *out;
		size_t out_len;
		size_t out_bound_len;
		size_t in_bound_len;

		in = utf8_name;
		in_bound_len = utf8_len;

		out = name;
		out_len = 0;
		out_bound_len = name_bound_len;

		while (in_bound_len) {
			int nb;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
			unicode_t uni;

			nb = utf8_to_utf32(in, in_bound_len, &uni);
#else
			linux_wchar_t uni;

			nb = utf8_mbtowc(&uni, in, in_bound_len);
#endif
			if (nb < 0) {
				LogFunc(("utf8_mbtowc failed(%s) %x:%d\n",
					 (const char *)utf8_name, *in,
					 in_bound_len));
				return -EINVAL;
			}
			in += nb;
			in_bound_len -= nb;

			nb = sf_g->nls->uni2char(uni, out, out_bound_len);
			if (nb < 0) {
				LogFunc(("nls->uni2char failed(%s) %x:%d\n",
					 utf8_name, uni, out_bound_len));
				return nb;
			}
			out += nb;
			out_bound_len -= nb;
			out_len += nb;
		}

		*out = 0;
	} else {
		if (utf8_len + 1 > name_bound_len)
			return -ENAMETOOLONG;

		memcpy(name, utf8_name, utf8_len + 1);
	}
	return 0;
}

static struct sf_dir_buf *sf_dir_buf_alloc(void)
{
	struct sf_dir_buf *b;

	TRACE();
	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (!b) {
		LogRelFunc(("could not alloc directory buffer\n"));
		return NULL;
	}
	b->buf = kmalloc(DIR_BUFFER_SIZE, GFP_KERNEL);
	if (!b->buf) {
		kfree(b);
		LogRelFunc(("could not alloc directory buffer storage\n"));
		return NULL;
	}

	INIT_LIST_HEAD(&b->head);
	b->cEntries = 0;
	b->cbUsed = 0;
	b->cbFree = DIR_BUFFER_SIZE;
	return b;
}

static void sf_dir_buf_free(struct sf_dir_buf *b)
{
	BUG_ON(!b || !b->buf);

	TRACE();
	list_del(&b->head);
	kfree(b->buf);
	kfree(b);
}

/**
 * Free the directory buffer.
 */
void sf_dir_info_free(struct sf_dir_info *p)
{
	struct list_head *list, *pos, *tmp;

	TRACE();
	list = &p->info_list;
	list_for_each_safe(pos, tmp, list) {
		struct sf_dir_buf *b;

		b = list_entry(pos, struct sf_dir_buf, head);
		sf_dir_buf_free(b);
	}
	kfree(p);
}

/**
 * Empty (but not free) the directory buffer.
 */
void sf_dir_info_empty(struct sf_dir_info *p)
{
	struct list_head *list, *pos, *tmp;
	TRACE();
	list = &p->info_list;
	list_for_each_safe(pos, tmp, list) {
		struct sf_dir_buf *b;
		b = list_entry(pos, struct sf_dir_buf, head);
		b->cEntries = 0;
		b->cbUsed = 0;
		b->cbFree = DIR_BUFFER_SIZE;
	}
}

/**
 * Create a new directory buffer descriptor.
 */
struct sf_dir_info *sf_dir_info_alloc(void)
{
	struct sf_dir_info *p;

	TRACE();
	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		LogRelFunc(("could not alloc directory info\n"));
		return NULL;
	}

	INIT_LIST_HEAD(&p->info_list);
	return p;
}

/**
 * Search for an empty directory content buffer.
 */
static struct sf_dir_buf *sf_get_empty_dir_buf(struct sf_dir_info *sf_d)
{
	struct list_head *list, *pos;

	list = &sf_d->info_list;
	list_for_each(pos, list) {
		struct sf_dir_buf *b;

		b = list_entry(pos, struct sf_dir_buf, head);
		if (!b)
			return NULL;
		else {
			if (b->cbUsed == 0)
				return b;
		}
	}

	return NULL;
}

/** @todo r=bird: Why on earth do we read in the entire directory???  This
 *        cannot be healthy for like big directory and such... */
int sf_dir_read_all(struct sf_glob_info *sf_g, struct sf_inode_info *sf_i,
		    struct sf_dir_info *sf_d, SHFLHANDLE handle)
{
	int err;
	SHFLSTRING *mask;
	VBOXSFLISTDIRREQ *pReq = NULL;

	TRACE();
	err = sf_make_path(__func__, sf_i, "*", 1, &mask);
	if (err)
		goto fail0;
	pReq = (VBOXSFLISTDIRREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
	if (!pReq)
		goto fail1;

	for (;;) {
		int rc;
		struct sf_dir_buf *b;

		b = sf_get_empty_dir_buf(sf_d);
		if (!b) {
			b = sf_dir_buf_alloc();
			if (!b) {
				err = -ENOMEM;
				LogRelFunc(("could not alloc directory buffer\n"));
				goto fail2;
			}
			list_add(&b->head, &sf_d->info_list);
		}

		rc = VbglR0SfHostReqListDirContig2x(sf_g->map.root, pReq, handle, mask, virt_to_phys(mask),
						    0 /*fFlags*/, b->buf, virt_to_phys(b->buf), b->cbFree);
		if (RT_SUCCESS(rc)) {
			b->cEntries += pReq->Parms.c32Entries.u.value32;
			b->cbFree   -= pReq->Parms.cb32Buffer.u.value32;
			b->cbUsed   += pReq->Parms.cb32Buffer.u.value32;
		} else if (rc == VERR_NO_MORE_FILES) {
			break;
		} else {
			err = -RTErrConvertToErrno(rc);
			LogFunc(("VbglR0SfHostReqListDirContig2x failed rc=%Rrc err=%d\n", rc, err));
			goto fail2;
		}
	}
	err = 0;

 fail2:
	VbglR0PhysHeapFree(pReq);
 fail1:
	kfree(mask);

 fail0:
	return err;
}

int sf_get_volume_info(struct super_block *sb, STRUCT_STATFS * stat)
{
	struct sf_glob_info *sf_g;
	VBOXSFVOLINFOREQ *pReq;
	SHFLVOLINFO *pVolInfo;
	int rc;

	sf_g = GET_GLOB_INFO(sb);
	pReq = VbglR0PhysHeapAlloc(sizeof(*pReq));
	if (pReq) {
		pVolInfo = &pReq->VolInfo;
		rc = VbglR0SfHostReqQueryVolInfo(sf_g->map.root, pReq, SHFL_HANDLE_ROOT);
		if (RT_SUCCESS(rc)) {
			stat->f_type   = NFS_SUPER_MAGIC;	/* XXX vboxsf type? */
			stat->f_bsize  = pVolInfo->ulBytesPerAllocationUnit;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 73)
			stat->f_frsize = pVolInfo->ulBytesPerAllocationUnit;
#endif
			stat->f_blocks = pVolInfo->ullTotalAllocationBytes
				       / pVolInfo->ulBytesPerAllocationUnit;
			stat->f_bfree  = pVolInfo->ullAvailableAllocationBytes
				       / pVolInfo->ulBytesPerAllocationUnit;
			stat->f_bavail = pVolInfo->ullAvailableAllocationBytes
				       / pVolInfo->ulBytesPerAllocationUnit;
			stat->f_files  = 1000;
			stat->f_ffree  = 1000; /* don't return 0 here since the guest may think
						* that it is not possible to create any more files */
			stat->f_fsid.val[0] = 0;
			stat->f_fsid.val[1] = 0;
			stat->f_namelen = 255;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
			stat->f_flags = 0; /* not valid */
#endif
			RT_ZERO(stat->f_spare);
			rc = 0;
		} else
			rc = -RTErrConvertToErrno(rc);
		VbglR0PhysHeapFree(pReq);
	} else
		rc = -ENOMEM;
	return rc;
}


/* this is called during name resolution/lookup to check if the
   [dentry] in the cache is still valid. the job is handled by
   [sf_inode_revalidate] */
static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
sf_dentry_revalidate(struct dentry *dentry, unsigned flags)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
sf_dentry_revalidate(struct dentry *dentry, struct nameidata *nd)
#else
sf_dentry_revalidate(struct dentry *dentry, int flags)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	int const flags = nd ? nd->flags : 0;
#endif

	int rc;

	Assert(dentry);
	SFLOGFLOW(("sf_dentry_revalidate: %p %#x %s\n", dentry, flags, dentry->d_inode ? GET_INODE_INFO(dentry->d_inode)->path->String.ach : "<negative>"));

	/*
	 * See Documentation/filesystems/vfs.txt why we skip LOOKUP_RCU.
	 *
	 * Also recommended: https://lwn.net/Articles/649115/
	 *      	     https://lwn.net/Articles/649729/
	 *      	     https://lwn.net/Articles/650786/
	 *
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	if (flags & LOOKUP_RCU) {
		rc = -ECHILD;
		SFLOGFLOW(("sf_dentry_revalidate: RCU -> -ECHILD\n"));
	} else
#endif
	{
		/*
		 * Do we have an inode or not?  If not it's probably a negative cache
		 * entry, otherwise most likely a positive one.
		 */
		struct inode *pInode = dentry->d_inode;
		if (pInode) {
			/*
			 * Positive entry.
			 *
			 * Note! We're more aggressive here than other remote file systems,
			 *       current (4.19) CIFS will for instance revalidate the inode
			 *       and ignore the dentry timestamp for positive entries.
			 */
			//struct sf_inode_info *sf_i = GET_INODE_INFO(pInode);
			unsigned long const  cJiffiesAge = sf_dentry_get_update_jiffies(dentry) - jiffies;
			struct sf_glob_info *sf_g        = GET_GLOB_INFO(dentry->d_sb);
			if (cJiffiesAge <= sf_g->ttl) {
				SFLOGFLOW(("sf_dentry_revalidate: age: %lu vs. TTL %lu -> 1\n", cJiffiesAge, sf_g->ttl));
				rc = 1;
			} else if (!sf_inode_revalidate(dentry /** @todo force */)) {
				sf_dentry_set_update_jiffies(dentry, jiffies); /** @todo get jiffies from inode. */
				SFLOGFLOW(("sf_dentry_revalidate: age: %lu vs. TTL %lu -> reval -> 1\n", cJiffiesAge, sf_g->ttl));
				rc = 1;
			} else {
				SFLOGFLOW(("sf_dentry_revalidate: age: %lu vs. TTL %lu -> reval -> 0\n", cJiffiesAge, sf_g->ttl));
				rc = 0;
			}
		} else {
			/*
			 * Negative entry.
			 *
			 * Invalidate dentries for open and renames here as we'll revalidate
			 * these when taking the actual action (also good for case preservation
			 * if we do case-insensitive mounts against windows + mac hosts at some
			 * later point).
			 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
			if (flags & (LOOKUP_CREATE | LOOKUP_RENAME_TARGET))
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 75)
			if (flags & LOOKUP_CREATE)
#else
			if (0)
#endif
			{
				SFLOGFLOW(("sf_dentry_revalidate: negative: create or rename target -> 0\n"));
				rc = 0;
			} else {
				/* Can we skip revalidation based on TTL? */
				unsigned long const  cJiffiesAge = sf_dentry_get_update_jiffies(dentry) - jiffies;
				struct sf_glob_info *sf_g        = GET_GLOB_INFO(dentry->d_sb);
				if (cJiffiesAge < sf_g->ttl) {
					SFLOGFLOW(("sf_dentry_revalidate: negative: age: %lu vs. TTL %lu -> 1\n", cJiffiesAge, sf_g->ttl));
					rc = 1;
				} else {
					/* We could revalidate it here, but we could instead just
					   have the caller kick it out. */
					/** @todo stat the direntry and see if it exists now. */
					SFLOGFLOW(("sf_dentry_revalidate: negative: age: %lu vs. TTL %lu -> 0\n", cJiffiesAge, sf_g->ttl));
					rc = 0;
				}
			}
		}
	}
	return rc;
}

#ifdef SFLOG_ENABLED

/** For logging purposes only. */
static int sf_dentry_delete(const struct dentry *pDirEntry)
{
	SFLOGFLOW(("sf_dentry_delete: %p\n", pDirEntry));
	return 0;
}

static int sf_dentry_init(struct dentry *pDirEntry)
{
	SFLOGFLOW(("sf_dentry_init: %p\n", pDirEntry));
	return 0;
}

#endif /* SFLOG_ENABLED */


struct dentry_operations sf_dentry_ops = {
	.d_revalidate = sf_dentry_revalidate,
#ifdef SFLOG_ENABLED
	.d_delete = sf_dentry_delete,
	.d_init = sf_dentry_init,
#endif
};

int sf_init_backing_dev(struct super_block *sb, struct sf_glob_info *sf_g)
{
	int rc = 0;
/** @todo this needs sorting out between 3.19 and 4.11   */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) //&& LINUX_VERSION_CODE <= KERNEL_VERSION(3, 19, 0)
	/* Each new shared folder map gets a new uint64_t identifier,
	 * allocated in sequence.  We ASSUME the sequence will not wrap. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
	static uint64_t s_u64Sequence = 0;
	uint64_t u64CurrentSequence = ASMAtomicIncU64(&s_u64Sequence);
#endif
	struct backing_dev_info *bdi;

#  if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	rc = super_setup_bdi_name(sb, "vboxsf-%llu", (unsigned long long)u64CurrentSequence);
	if (!rc)
		bdi = sb->s_bdi;
	else
		return rc;
#  else
	bdi = &sf_g->bdi;
#  endif

	bdi->ra_pages = 0;                      /* No readahead */

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 12)
	bdi->capabilities = 0
#  ifdef BDI_CAP_MAP_DIRECT
		          | BDI_CAP_MAP_DIRECT  /* MAP_SHARED */
#  endif
#  ifdef BDI_CAP_MAP_COPY
	                  | BDI_CAP_MAP_COPY    /* MAP_PRIVATE */
#  endif
#  ifdef BDI_CAP_READ_MAP
	                  | BDI_CAP_READ_MAP    /* can be mapped for reading */
#  endif
#  ifdef BDI_CAP_WRITE_MAP
	                  | BDI_CAP_WRITE_MAP   /* can be mapped for writing */
#  endif
#  ifdef BDI_CAP_EXEC_MAP
	                  | BDI_CAP_EXEC_MAP    /* can be mapped for execution */
#  endif
#  ifdef BDI_CAP_STRICTLIMIT
	                  | BDI_CAP_STRICTLIMIT;
#  endif
			  ;
#  ifdef BDI_CAP_STRICTLIMIT
	/* Smalles possible amount of dirty pages: %1 of RAM */
	bdi_set_max_ratio(bdi, 1);
#  endif
# endif	/* >= 2.6.12 */

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
	rc = bdi_init(&sf_g->bdi);
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
	if (!rc)
		rc = bdi_register(&sf_g->bdi, NULL, "vboxsf-%llu",
				  (unsigned long long)u64CurrentSequence);
#  endif /* >= 2.6.26 */
# endif	 /* >= 2.6.24 */
#endif   /* >= 2.6.0 */
	return rc;
}

void sf_done_backing_dev(struct super_block *sb, struct sf_glob_info *sf_g)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) && LINUX_VERSION_CODE <= KERNEL_VERSION(3, 19, 0)
	bdi_destroy(&sf_g->bdi);	/* includes bdi_unregister() */
#endif
}

