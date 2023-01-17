; $Id$
;; @file
; IPRT - ASMGetXcr0().
;

;
; Copyright (C) 2006-2023 Oracle and/or its affiliates.
;
; This file is part of VirtualBox base platform packages, as
; available from https://www.virtualbox.org.
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation, in version 3 of the
; License.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, see <https://www.gnu.org/licenses>.
;
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
; in the VirtualBox distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;
; SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
;

;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"

BEGINCODE

;;
; Gets the content of the XCR0 CPU register.
; @returns XCR0 value in rax (amd64) / edx:eax (x86).
;
RT_BEGINPROC ASMGetXcr0
SEH64_END_PROLOGUE
        xor     ecx, ecx                ; XCR0
        xgetbv
%ifdef RT_ARCH_AMD64
        shl     rdx, 32
        or      rax, rdx
%endif
%if ARCH_BITS == 16 ; DX:CX:BX:AX - DX=15:0, CX=31:16, BX=47:32, AX=63:48
        mov     ebx, edx
        mov     ecx, eax
        shr     ecx, 16
        xchg    eax, edx
        shr     eax, 16
%endif
        ret
ENDPROC ASMGetXcr0

