/* $Id$ */

/** @file
 * VBox D3D8/9 dll switcher
 */

/*
 * Copyright (C) 2009 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include <windows.h>
#include "switcher.h"

static char* gsBlackList[] = {"Dwm.exe", "java.exe", "javaw.exe", "javaws.exe"/*, "taskeng.exe"*/, NULL};

/* Checks if 3D is enabled for VM and it works on host machine */
BOOL isVBox3DEnabled(void)
{
    DrvValidateVersionProc pDrvValidateVersion;
    HANDLE hDLL;
    BOOL result = FALSE;

    hDLL = LoadLibrary("VBoxOGL.dll");

    /* note: this isn't really needed as our library will refuse to load if it can't connect to host.
       so it's in case we'd change it one day.
    */
    pDrvValidateVersion = (DrvValidateVersionProc) GetProcAddress(hDLL, "DrvValidateVersion");
    if (pDrvValidateVersion)
    {
        result = pDrvValidateVersion(0);
    }
    FreeLibrary(hDLL);
    return result;
}

BOOL checkOptions(void)
{
    char name[1000];
    char *filename = name, *pName;
    int i;

	if (!GetModuleFileName(NULL, name, 1000))
		return TRUE;

    /*Extract filename*/
    for (pName=name; *pName; ++pName)
    {
        switch (*pName)
        {
            case ':':
            case '\\':
            case '/':
                filename = pName + 1;
                break;
        }
    }

    for (i=0; gsBlackList[i]; ++i)
    {
        if (!stricmp(filename, gsBlackList[i]))
            return FALSE;
    }

    return TRUE;
}

void InitD3DExports(const char *vboxName, const char *msName)
{
    const char *dllName;
    HANDLE hDLL;

    if (isVBox3DEnabled() && checkOptions())
    {
        dllName = vboxName;
    } else
    {
        dllName = msName;
    }

    hDLL = LoadLibrary(dllName);
    FillD3DExports(hDLL); 
}
