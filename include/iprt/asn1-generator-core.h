/** @file
 * IPRT - ASN.1 Code Generator, the ASN.1 Core Code (VTable, Compare, ..).
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
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

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>

#define RTASN1TMPL_PASS                 RTASN1TMPL_PASS_XTAG
#include <iprt/asn1-generator-pass.h>
#define RTASN1TMPL_PASS                 RTASN1TMPL_PASS_VTABLE
#include <iprt/asn1-generator-pass.h>
#define RTASN1TMPL_PASS                 RTASN1TMPL_PASS_DELETE
#include <iprt/asn1-generator-pass.h>
#define RTASN1TMPL_PASS                 RTASN1TMPL_PASS_ENUM
#include <iprt/asn1-generator-pass.h>
#define RTASN1TMPL_PASS                 RTASN1TMPL_PASS_COMPARE
#include <iprt/asn1-generator-pass.h>

