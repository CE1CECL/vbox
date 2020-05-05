/* $Id$ */
/** @file
 * IPRT - Virtual File System, File Printf.
 */

/*
 * Copyright (C) 2010-2020 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/vfs.h>

#include <iprt/errcore.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct PRINTFBUF
{
    RTVFSIOSTREAM   hVfsIos;
    int             rc;
    size_t          offBuf;
    char            szBuf[256];
} PRINTFBUF;


/** Writes the buffer to the VFS file. */
static void FlushPrintfBuffer(PRINTFBUF *pBuf)
{
    if (pBuf->offBuf)
    {
        int rc = RTVfsIoStrmWrite(pBuf->hVfsIos, pBuf->szBuf, pBuf->offBuf, true /*fBlocking*/, NULL);
        if (RT_FAILURE(rc))
            pBuf->rc = rc;
        pBuf->offBuf   = 0;
        pBuf->szBuf[0] = '\0';
    }
}


/** @callback_method_impl{FNRTSTROUTPUT} */
static DECLCALLBACK(size_t) MyPrintfOutputter(void *pvArg, const char *pachChars, size_t cbChars)
{
    PRINTFBUF *pBuf = (PRINTFBUF *)pvArg;
    if (cbChars != 0)
    {
        size_t offSrc = 0;
        while  (offSrc < cbChars)
        {
            size_t cbLeft = sizeof(pBuf->szBuf) - pBuf->offBuf - 1;
            if (cbLeft > 0)
            {
                size_t cbToCopy = RT_MIN(cbChars - offSrc, cbLeft);
                memcpy(&pBuf->szBuf[pBuf->offBuf], &pachChars[offSrc], cbToCopy);
                pBuf->offBuf += cbToCopy;
                pBuf->szBuf[pBuf->offBuf] = '\0';
                if (cbLeft > cbToCopy)
                    break;
                offSrc += cbToCopy;
            }
            FlushPrintfBuffer(pBuf);
        }
    }
    else /* Special zero byte write at the end of the formatting. */
        FlushPrintfBuffer(pBuf);
    return cbChars;
}



RTDECL(ssize_t) RTVfsIoStrmPrintfV(RTVFSIOSTREAM hVfsIos, const char *pszFormat, va_list va)
{
    PRINTFBUF Buf;
    Buf.hVfsIos  = hVfsIos;
    Buf.rc       = VINF_SUCCESS;
    Buf.offBuf   = 0;
    Buf.szBuf[0] = '\0';

    size_t cchRet = RTStrFormatV(MyPrintfOutputter, &Buf, NULL, NULL, pszFormat, va);
    if (RT_SUCCESS(Buf.rc))
        return cchRet;
    return Buf.rc;
}


RTDECL(ssize_t) RTVfsIoStrmPrintf(RTVFSIOSTREAM hVfsIos, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    ssize_t cchRet = RTVfsIoStrmPrintfV(hVfsIos, pszFormat, va);
    va_end(va);
    return cchRet;
}


RTDECL(ssize_t) RTVfsFilePrintfV(RTVFSFILE hVfsFile, const char *pszFormat, va_list va)
{
    ssize_t cchRet;
    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
    if (hVfsIos != NIL_RTVFSIOSTREAM)
    {
        cchRet = RTVfsIoStrmPrintfV(hVfsIos, pszFormat, va);
        RTVfsIoStrmRelease(hVfsIos);
    }
    else
        cchRet = VERR_INVALID_HANDLE;
    return cchRet;
}


RTDECL(ssize_t) RTVfsFilePrintf(RTVFSFILE hVfsFile, const char *pszFormat, ...)
{
    ssize_t cchRet;
    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
    if (hVfsIos != NIL_RTVFSIOSTREAM)
    {
        va_list va;
        va_start(va, pszFormat);
        cchRet = RTVfsIoStrmPrintfV(hVfsIos, pszFormat, va);
        va_end(va);
        RTVfsIoStrmRelease(hVfsIos);
    }
    else
        cchRet = VERR_INVALID_HANDLE;
    return cchRet;
}

