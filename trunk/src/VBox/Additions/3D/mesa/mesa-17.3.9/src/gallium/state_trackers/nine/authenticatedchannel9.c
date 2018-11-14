/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include "authenticatedchannel9.h"

#define DBG_CHANNEL DBG_AUTHENTICATEDCHANNEL

HRESULT NINE_WINAPI
NineAuthenticatedChannel9_GetCertificateSize( struct NineAuthenticatedChannel9 *This,
                                              UINT *pCertificateSize )
{
    STUB(D3DERR_INVALIDCALL);
}

HRESULT NINE_WINAPI
NineAuthenticatedChannel9_GetCertificate( struct NineAuthenticatedChannel9 *This,
                                          UINT CertifacteSize,
                                          BYTE *ppCertificate )
{
    STUB(D3DERR_INVALIDCALL);
}

HRESULT NINE_WINAPI
NineAuthenticatedChannel9_NegotiateKeyExchange( struct NineAuthenticatedChannel9 *This,
                                                UINT DataSize,
                                                void *pData )
{
    STUB(D3DERR_INVALIDCALL);
}

HRESULT NINE_WINAPI
NineAuthenticatedChannel9_Query( struct NineAuthenticatedChannel9 *This,
                                 UINT InputSize,
                                 const void *pInput,
                                 UINT OutputSize,
                                 void *pOutput )
{
    STUB(D3DERR_INVALIDCALL);
}

HRESULT NINE_WINAPI
NineAuthenticatedChannel9_Configure( struct NineAuthenticatedChannel9 *This,
                                     UINT InputSize,
                                     const void *pInput,
                                     D3DAUTHENTICATEDCHANNEL_CONFIGURE_OUTPUT *pOutput )
{
    STUB(D3DERR_INVALIDCALL);
}

IDirect3DAuthenticatedChannel9Vtbl NineAuthenticatedChannel9_vtable = {
    (void *)NineUnknown_QueryInterface,
    (void *)NineUnknown_AddRef,
    (void *)NineUnknown_Release,
    (void *)NineAuthenticatedChannel9_GetCertificateSize,
    (void *)NineAuthenticatedChannel9_GetCertificate,
    (void *)NineAuthenticatedChannel9_NegotiateKeyExchange,
    (void *)NineAuthenticatedChannel9_Query,
    (void *)NineAuthenticatedChannel9_Configure
};
