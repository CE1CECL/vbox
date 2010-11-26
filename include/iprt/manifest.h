/** @file
 * IPRT - Manifest file handling.
 */

/*
 * Copyright (C) 2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___iprt_manifest_h
#define ___iprt_manifest_h

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_manifest    RTManifest - Manifest file creation and checking
 * @ingroup grp_rt
 * @{
 */

/** @name Manifest attribute types.
 * The types can be ORed together to form a set.
 * @{ */
/** For use with other attributes. Representation unknown. */
#define RTMANIFEST_ATTR_UNKNOWN     0
/** The size of the content.    Represented as a decimal number. */
#define RTMANIFEST_ATTR_SIZE        RT_BIT_32(0)
/** The MD5 of the content.     Represented as a hex string. */
#define RTMANIFEST_ATTR_MD5         RT_BIT_32(1)
/** The SHA-1 of the content.   Represented as a hex string. */
#define RTMANIFEST_ATTR_SHA1        RT_BIT_32(2)
/** The SHA-256 of the content. Represented as a hex string. */
#define RTMANIFEST_ATTR_SHA256      RT_BIT_32(3)
/** The SHA-512 of the content. Represented as a hex string. */
#define RTMANIFEST_ATTR_SHA512      RT_BIT_32(4)
/** The end of the valid values. */
#define RTMANIFEST_ATTR_END         RT_BIT_32(5)
/** @} */


/**
 * Creates an empty manifest.
 *
 * @returns IPRT status code.
 * @param   fFlags              Flags, MBZ.
 * @param   phManifest          Where to return the handle to the manifest.
 */
RTDECL(int) RTManifestCreate(uint32_t fFlags, PRTMANIFEST phManifest);

/**
 * Retains a reference to the manifest handle.
 *
 * @returns The new reference count, UINT32_MAX if the handle is invalid.
 * @param   hManifest           The handle to retain.
 */
RTDECL(uint32_t) RTManifestRetain(RTMANIFEST hManifest);

/**
 * Releases a reference to the manifest handle.
 *
 * @returns The new reference count, 0 if free. UINT32_MAX is returned if the
 *          handle is invalid.
 * @param   hManifest           The handle to release.
 *                              NIL is quietly ignored (returns 0).
 */
RTDECL(uint32_t) RTManifestRelease(RTMANIFEST hManifest);

/**
 * Creates a duplicate of the specified manifest.
 *
 * @returns IPRT status code
 * @param   hManifestSrc        The manifest to clone.
 * @param   phManifestDst       Where to store the handle to the duplicate.
 */
RTDECL(int) RTManifestDup(RTMANIFEST hManifestSrc, PRTMANIFEST phManifestDst);

/**
 * Compares two manifests for equality.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if equal.
 * @retval  VERR_NOT_EQUAL if not equal.
 *
 * @param   hManifest1          The first manifest.
 * @param   hManifest2          The second manifest.
 * @param   papszIgnoreEntries  Entries to ignore.  Ends with a NULL entry.
 * @param   papszIgnoreAttrs    Attributes to ignore.  Ends with a NULL entry.
 * @param   pszEntry            Where to store the name of the mismatching
 *                              entry, or as much of the name as there is room
 *                              for.  This is always set.  Optional.
 * @param   cbEntry             The size of the buffer pointed to by @a
 *                              pszEntry.
 */
RTDECL(int) RTManifestEqualsEx(RTMANIFEST hManifest1, RTMANIFEST hManifest2, const char * const *papszIgnoreEntries,
                               const char * const *papszIgnoreAttr, char *pszEntry, size_t cbEntry);

/**
 * Compares two manifests for equality.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if equal.
 * @retval  VERR_NOT_EQUAL if not equal.
 *
 * @param   hManifest1          The first manifest.
 * @param   hManifest2          The second manifest.
 */
RTDECL(int) RTManifestEquals(RTMANIFEST hManifest1, RTMANIFEST hManifest2);

/**
 * Sets a manifest attribute.
 *
 * @returns IPRT status code.
 * @param   hManifest           The manifest handle.
 * @param   pszAttr             The attribute name.  If this already exists,
 *                              its value will be replaced.
 * @param   pszValue            The value string.
 * @param   fType               The attribute type, pass
 *                              RTMANIFEST_ATTR_UNKNOWN if not known.
 */
RTDECL(int) RTManifestSetAttr(RTMANIFEST hManifest, const char *pszAttr, const char *pszValue, uint32_t fType);

/**
 * Unsets (removes) a manifest attribute if it exists.
 *
 * @returns IPRT status code.
 * @retval  VWRN_NOT_FOUND if not found.
 *
 * @param   hManifest           The manifest handle.
 * @param   pszAttr             The attribute name.
 */
RTDECL(int) RTManifestUnsetAttr(RTMANIFEST hManifest, const char *pszAttr);

/**
 * Sets an attribute of a manifest entry.
 *
 * @returns IPRT status code.
 * @param   hManifest           The manifest handle.
 * @param   pszEntry            The entry name.  This will automatically be
 *                              added if there was no previous call to
 *                              RTManifestEntryAdd for this name.  See
 *                              RTManifestEntryAdd for the entry name rules.
 * @param   pszAttr             The attribute name.  If this already exists,
 *                              its value will be replaced.
 * @param   pszValue            The value string.
 * @param   fType               The attribute type, pass
 *                              RTMANIFEST_ATTR_UNKNOWN if not known.
 */
RTDECL(int) RTManifestEntrySetAttr(RTMANIFEST hManifest, const char *pszEntry, const char *pszAttr,
                                   const char *pszValue, uint32_t fType);

/**
 * Unsets (removes) an attribute of a manifest entry if they both exist.
 *
 * @returns IPRT status code.
 * @retval  VWRN_NOT_FOUND if not found.
 *
 * @param   hManifest           The manifest handle.
 * @param   pszEntry            The entry name.
 * @param   pszAttr             The attribute name.
 */
RTDECL(int) RTManifestEntryUnsetAttr(RTMANIFEST hManifest, const char *pszEntry, const char *pszAttr);

/**
 * Adds a new entry to a manifest.
 *
 * The entry name rules:
 *     - The entry name can contain any character defined by unicode, except
 *       control characters, ':', '(' and ')'.  The exceptions are mainly there
 *       because of uncertainty around how various formats handles these.
 *     - It is considered case sensitive.
 *     - Forward (unix) and backward (dos) slashes are considered path
 *       separators and converted to forward slashes.
 *
 * @returns IPRT status code.
 * @retval  VWRN_ALREADY_EXISTS if the entry already exists.
 *
 * @param   hManifest           The manifest handle.
 * @param   pszEntry            The entry name (UTF-8).
 *
 * @remarks Some manifest formats will not be able to store an entry without
 *          any attributes.  So, this is just here in case it comes in handy
 *          when dealing with formats which can.
 */
RTDECL(int) RTManifestEntryAdd(RTMANIFEST hManifest, const char *pszEntry);

/**
 * Removes an entry.
 *
 * @returns IPRT status code.
 * @param   hManifest           The manifest handle.
 * @param   pszEntry            The entry name.
 */
RTDECL(int) RTManifestEntryRemove(RTMANIFEST hManifest, const char *pszEntry);


/**
 * Adds an entry for a file with the specified set of attributes.
 *
 * @returns IPRT status code.
 *
 * @param   hManifest           The manifest handle.
 * @param   hVfsIos             The I/O stream handle of the entry.  This will
 *                              be processed to its end on successful return.
 *                              (Must be positioned at the start to get
 *                              the expected results.)
 * @param   pszEntry            The entry name.
 * @param   fAttrs              The attributes to create for this stream.  See
 *                              RTMANIFEST_ATTR_XXX.
 */
RTDECL(int) RTManifestEntryAddIoStream(RTMANIFEST hManifest, RTVFSIOSTREAM hVfsIos, const char *pszEntry, uint32_t fAttrs);

/**
 * Reads in a "standard" manifest.
 *
 * This reads the format used by OVF, the distinfo in FreeBSD ports, and
 * others.
 *
 * @returns IPRT status code.
 * @param   hManifest           The handle to the manifest where to add the
 *                              manifest that's read in.
 * @param   hVfsIos             The I/O stream to read the manifest from.
 */
RTDECL(int) RTManifestReadStandard(RTMANIFEST hManifest, RTVFSIOSTREAM hVfsIos);

/**
 * Writes a "standard" manifest.
 *
 * This writes the format used by OVF, the distinfo in FreeBSD ports, and
 * others.
 *
 * @returns IPRT status code.
 * @param   hManifest           The handle to the manifest where to add the
 *                              manifest that's read in.
 * @param   hVfsIos             The I/O stream to read the manifest from.
 */
RTDECL(int) RTManifestWriteStandard(RTMANIFEST hManifest, RTVFSIOSTREAM hVfsIos);





/**
 * Input structure for RTManifestVerify() which contains the filename & the
 * SHA1 digest.
 */
typedef struct RTMANIFESTTEST
{
    /** The filename. */
    const char *pszTestFile;
    /** The SHA1 digest of the file. */
    const char *pszTestDigest;
} RTMANIFESTTEST;
/** Pointer to the input structure. */
typedef RTMANIFESTTEST* PRTMANIFESTTEST;


/**
 * Verify the given SHA1 digests against the entries in the manifest file.
 *
 * Please note that not only the various digest have to match, but the
 * filenames as well. If there are more or even less files listed in the
 * manifest file than provided by paTests, VERR_MANIFEST_FILE_MISMATCH will be
 * returned.
 *
 * @returns iprt status code.
 *
 * @param   pszManifestFile      Filename of the manifest file to verify.
 * @param   paTests              Array of files & SHA1 sums.
 * @param   cTests               Number of entries in paTests.
 * @param   piFailed             A index to paTests in the
 *                               VERR_MANIFEST_DIGEST_MISMATCH error case
 *                               (optional).
 */
RTR3DECL(int) RTManifestVerify(const char *pszManifestFile, PRTMANIFESTTEST paTests, size_t cTests, size_t *piFailed);

/**
 * This is analogous to function RTManifestVerify(), but calculates the SHA1
 * sums of the given files itself.
 *
 * @returns iprt status code.
 *
 * @param   pszManifestFile      Filename of the manifest file to verify.
 * @param   papszFiles           Array of files to check SHA1 sums.
 * @param   cFiles               Number of entries in papszFiles.
 * @param   piFailed             A index to papszFiles in the
 *                               VERR_MANIFEST_DIGEST_MISMATCH error case
 *                               (optional).
 * @param   pfnProgressCallback  optional callback for the progress indication
 * @param   pvUser               user defined pointer for the callback
 */
RTR3DECL(int) RTManifestVerifyFiles(const char *pszManifestFile, const char * const *papszFiles, size_t cFiles, size_t *piFailed,
                                    PFNRTPROGRESS pfnProgressCallback, void *pvUser);

/**
 * Creates a manifest file for a set of files. The manifest file contains SHA1
 * sums of every provided file and could be used to verify the data integrity
 * of them.
 *
 * @returns iprt status code.
 *
 * @param   pszManifestFile      Filename of the manifest file to create.
 * @param   papszFiles           Array of files to create SHA1 sums for.
 * @param   cFiles               Number of entries in papszFiles.
 * @param   pfnProgressCallback  optional callback for the progress indication
 * @param   pvUser               user defined pointer for the callback
 */
RTR3DECL(int) RTManifestWriteFiles(const char *pszManifestFile, const char * const *papszFiles, size_t cFiles,
                                   PFNRTPROGRESS pfnProgressCallback, void *pvUser);

/**
 * Verify the given SHA1 digests against the entries in the manifest file in
 * memory.
 *
 * @returns iprt status code.
 *
 * @param   pvBuf                Pointer to memory buffer of the manifest file.
 * @param   cbSize               Size of the memory buffer.
 * @param   paTests              Array of file names and digests.
 * @param   cTest                Number of entries in paTests.
 * @param   piFailed             A index to paTests in the
 *                               VERR_MANIFEST_DIGEST_MISMATCH error case
 *                               (optional).
 */
RTR3DECL(int) RTManifestVerifyFilesBuf(void *pvBuf, size_t cbSize, PRTMANIFESTTEST paTests, size_t cTests, size_t *piFailed);

/**
 * Creates a manifest file in memory for a set of files. The manifest file
 * contains SHA1 sums of every provided file and could be used to verify the
 * data integrity of them.
 *
 * @returns iprt status code.
 *
 * @param   ppvBuf               Pointer to resulting memory buffer.
 * @param   pcbSize              Pointer for the size of the memory buffer.
 * @param   paFiles              Array of file names and digests.
 * @param   cFiles               Number of entries in paFiles.
 */
RTR3DECL(int) RTManifestWriteFilesBuf(void **ppvBuf, size_t *pcbSize, PRTMANIFESTTEST paFiles, size_t cFiles);

/** @} */

RT_C_DECLS_END

#endif

