/** @file
 *
 * Simple VBox HDD container test utility. Only fast tests.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
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

#include <VBox/err.h>
#include <VBox/VBoxHDD-new.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/initterm.h>
#include <iprt/rand.h>
#include "stdio.h"
#include "stdlib.h"

/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The error count. */
unsigned g_cErrors = 0;

static struct KeyValuePair {
    const char *key;
    const char *value;
} aCfgNode[] = {
    { "TargetName", "test" },
    { "LUN", "1" },
    { "TargetAddress", "address" },
    { NULL, NULL }
};

static bool tstAreKeysValid(void *pvUser, const char *pszzValid)
{
    return true;
}

static const char *tstGetValueByKey(const char *pszKey)
{
    for (int i = 0; aCfgNode[i].key; i++)
        if (!strcmp(aCfgNode[i].key, pszKey))
            return aCfgNode[i].value;
    return NULL;
}

static int tstQuerySize(void *pvUser, const char *pszName, size_t *pcbValue)
{
    const char *pszValue = tstGetValueByKey(pszName);
    if (!pszValue)
        return VERR_CFGM_VALUE_NOT_FOUND;
    *pcbValue = strlen(pszValue) + 1;
    return VINF_SUCCESS;
}

static int tstQuery(void *pvUser, const char *pszName, char *pszValue, size_t cchValue)
{
    const char *pszTmp = tstGetValueByKey(pszName);
    if (!pszValue)
        return VERR_CFGM_VALUE_NOT_FOUND;
    size_t cchTmp = strlen(pszTmp) + 1;
    if (cchValue < cchTmp)
        return VERR_CFGM_NOT_ENOUGH_SPACE;
    memcpy(pszValue, pszTmp, cchTmp);
    return VINF_SUCCESS;
}


VDINTERFACECONFIG icc = {
    sizeof(VDINTERFACECONFIG),
    VDINTERFACETYPE_CONFIG,
    tstAreKeysValid,
    tstQuerySize,
    tstQuery
};

static int tstVDBackendInfo(void)
{
    int rc;
#define MAX_BACKENDS 100
    VDBACKENDINFO aVDInfo[MAX_BACKENDS];
    unsigned cEntries;

#define CHECK(str) \
    do \
    { \
        RTPrintf("%s rc=%Rrc\n", str, rc); \
        if (RT_FAILURE(rc)) \
            return rc; \
    } while (0)

    rc = VDBackendInfo(MAX_BACKENDS, aVDInfo, &cEntries);
    CHECK("VDBackendInfo()");

    for (unsigned i=0; i < cEntries; i++)
    {
        RTPrintf("Backend %u: name=%s capabilities=%#06x extensions=",
                 i, aVDInfo[i].pszBackend, aVDInfo[i].uBackendCaps);
        if (aVDInfo[i].papszFileExtensions)
        {
            const char *const *papsz = aVDInfo[i].papszFileExtensions;
            while (*papsz != NULL)
            {
                if (papsz != aVDInfo[i].papszFileExtensions)
                    RTPrintf(",");
                RTPrintf("%s", *papsz);
                papsz++;
            }
            if (papsz == aVDInfo[i].papszFileExtensions)
                RTPrintf("<EMPTY>");
        }
        else
            RTPrintf("<NONE>");
        RTPrintf(" config=");
        if (aVDInfo[i].paConfigInfo)
        {
            PCVDCONFIGINFO pa = aVDInfo[i].paConfigInfo;
            while (pa->pszKey != NULL)
            {
                if (pa != aVDInfo[i].paConfigInfo)
                    RTPrintf(",");
                RTPrintf("(key=%s type=", pa->pszKey);
                switch (pa->enmValueType)
                {
                    case VDCFGVALUETYPE_INTEGER:
                        RTPrintf("integer");
                        break;
                    case VDCFGVALUETYPE_STRING:
                        RTPrintf("string");
                        break;
                    case VDCFGVALUETYPE_BYTES:
                        RTPrintf("bytes");
                        break;
                    default:
                        RTPrintf("INVALID!");
                }
                RTPrintf(" default=");
                if (pa->pszDefaultValue)
                    RTPrintf("%s", pa->pszDefaultValue);
                else
                    RTPrintf("<NONE>");
                RTPrintf(" flags=");
                if (!pa->uKeyFlags)
                    RTPrintf("none");
                unsigned cFlags = 0;
                if (pa->uKeyFlags & VD_CFGKEY_MANDATORY)
                {
                    if (cFlags)
                        RTPrintf(",");
                    RTPrintf("mandatory");
                    cFlags++;
                }
                if (pa->uKeyFlags & VD_CFGKEY_EXPERT)
                {
                    if (cFlags)
                        RTPrintf(",");
                    RTPrintf("expert");
                    cFlags++;
                }
                RTPrintf(")");
                pa++;
            }
            if (pa == aVDInfo[i].paConfigInfo)
                RTPrintf("<EMPTY>");
        }
        else
            RTPrintf("<NONE>");
        RTPrintf("\n");

        VDINTERFACE ic;
        ic.cbSize = sizeof(ic);
        ic.enmInterface = VDINTERFACETYPE_CONFIG;
        ic.pCallbacks = &icc;
        char *pszLocation, *pszName;
        rc = aVDInfo[i].pfnComposeLocation(&ic, &pszLocation);
        CHECK("pfnComposeLocation()");
        if (pszLocation)
        {
            RTMemFree(pszLocation);
            if (aVDInfo[i].uBackendCaps & VD_CAP_FILE)
            {
                RTPrintf("Non-NULL location returned for file-based backend!\n");
                return VERR_INTERNAL_ERROR;
            }
        }
        rc = aVDInfo[i].pfnComposeName(&ic, &pszName);
        CHECK("pfnComposeName()");
        if (pszName)
        {
            RTMemFree(pszName);
            if (aVDInfo[i].uBackendCaps & VD_CAP_FILE)
            {
                RTPrintf("Non-NULL name returned for file-based backend!\n");
                return VERR_INTERNAL_ERROR;
            }
        }
    }

#undef CHECK
    return 0;
}


int main(int argc, char *argv[])
{
    int rc;

    RTR3Init();
    RTPrintf("tstVD-2: TESTING...\n");

    rc = tstVDBackendInfo();
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstVD-2: getting backend info test failed! rc=%Rrc\n", rc);
        g_cErrors++;
    }

    rc = VDShutdown();
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstVD-2: unloading backends failed! rc=%Rrc\n", rc);
        g_cErrors++;
    }
    /*
     * Summary
     */
    if (!g_cErrors)
        RTPrintf("tstVD-2: SUCCESS\n");
    else
        RTPrintf("tstVD-2: FAILURE - %d errors\n", g_cErrors);

    return !!g_cErrors;
}

