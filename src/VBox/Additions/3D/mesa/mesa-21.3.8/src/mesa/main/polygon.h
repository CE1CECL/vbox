/**
 * \file polygon.h
 * Polygon operations.
 */

/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2006  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef POLYGON_H
#define POLYGON_H


#include "glheader.h"

struct gl_context;

extern void GLAPIENTRY
_mesa_GetnPolygonStippleARB( GLsizei bufSize, GLubyte *dest );

void GLAPIENTRY
_mesa_CullFace_no_error(GLenum mode);

extern void GLAPIENTRY
_mesa_CullFace(GLenum mode);

void GLAPIENTRY
_mesa_FrontFace_no_error(GLenum mode);

extern void GLAPIENTRY
_mesa_FrontFace(GLenum mode);

void GLAPIENTRY
_mesa_PolygonMode_no_error(GLenum face, GLenum mode);

extern void GLAPIENTRY
_mesa_PolygonMode( GLenum face, GLenum mode );

extern void GLAPIENTRY
_mesa_PolygonOffset( GLfloat factor, GLfloat units );

extern void GLAPIENTRY
_mesa_PolygonOffsetClampEXT( GLfloat factor, GLfloat units, GLfloat clamp );

extern void GLAPIENTRY
_mesa_PolygonStipple( const GLubyte *mask );

extern void GLAPIENTRY
_mesa_GetPolygonStipple( GLubyte *mask );

extern void
_mesa_polygon_offset_clamp(struct gl_context *ctx,
                           GLfloat factor, GLfloat units, GLfloat clamp);
extern void
_mesa_init_polygon( struct gl_context * ctx );

#endif
