/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include "pipe/p_compiler.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "tgsi/tgsi_dump.h"

#include "tr_dump.h"
#include "tr_dump_defines.h"
#include "tr_dump_state.h"


void trace_dump_resource_template(const struct pipe_resource *templat)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!templat) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_resource");

   trace_dump_member(int, templat, target);
   trace_dump_member(format, templat, format);

   trace_dump_member_begin("width");
   trace_dump_uint(templat->width0);
   trace_dump_member_end();

   trace_dump_member_begin("height");
   trace_dump_uint(templat->height0);
   trace_dump_member_end();

   trace_dump_member_begin("depth");
   trace_dump_uint(templat->depth0);
   trace_dump_member_end();

   trace_dump_member_begin("array_size");
   trace_dump_uint(templat->array_size);
   trace_dump_member_end();

   trace_dump_member(uint, templat, last_level);
   trace_dump_member(uint, templat, nr_samples);
   trace_dump_member(uint, templat, usage);
   trace_dump_member(uint, templat, bind);
   trace_dump_member(uint, templat, flags);

   trace_dump_struct_end();
}


void trace_dump_box(const struct pipe_box *box)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!box) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_box");

   trace_dump_member(int, box, x);
   trace_dump_member(int, box, y);
   trace_dump_member(int, box, z);
   trace_dump_member(int, box, width);
   trace_dump_member(int, box, height);
   trace_dump_member(int, box, depth);

   trace_dump_struct_end();
}


void trace_dump_rasterizer_state(const struct pipe_rasterizer_state *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_rasterizer_state");

   trace_dump_member(bool, state, flatshade);
   trace_dump_member(bool, state, light_twoside);
   trace_dump_member(bool, state, clamp_vertex_color);
   trace_dump_member(bool, state, clamp_fragment_color);
   trace_dump_member(uint, state, front_ccw);
   trace_dump_member(uint, state, cull_face);
   trace_dump_member(uint, state, fill_front);
   trace_dump_member(uint, state, fill_back);
   trace_dump_member(bool, state, offset_point);
   trace_dump_member(bool, state, offset_line);
   trace_dump_member(bool, state, offset_tri);
   trace_dump_member(bool, state, scissor);
   trace_dump_member(bool, state, poly_smooth);
   trace_dump_member(bool, state, poly_stipple_enable);
   trace_dump_member(bool, state, point_smooth);
   trace_dump_member(bool, state, sprite_coord_mode);
   trace_dump_member(bool, state, point_quad_rasterization);
   trace_dump_member(bool, state, point_size_per_vertex);
   trace_dump_member(bool, state, multisample);
   trace_dump_member(bool, state, line_smooth);
   trace_dump_member(bool, state, line_stipple_enable);
   trace_dump_member(bool, state, line_last_pixel);

   trace_dump_member(bool, state, flatshade_first);

   trace_dump_member(bool, state, half_pixel_center);
   trace_dump_member(bool, state, bottom_edge_rule);

   trace_dump_member(bool, state, rasterizer_discard);

   trace_dump_member(bool, state, depth_clip);

   trace_dump_member(bool, state, clip_halfz);

   trace_dump_member(uint, state, clip_plane_enable);

   trace_dump_member(uint, state, line_stipple_factor);
   trace_dump_member(uint, state, line_stipple_pattern);

   trace_dump_member(uint, state, sprite_coord_enable);

   trace_dump_member(float, state, line_width);
   trace_dump_member(float, state, point_size);
   trace_dump_member(float, state, offset_units);
   trace_dump_member(float, state, offset_scale);
   trace_dump_member(float, state, offset_clamp);

   trace_dump_struct_end();
}


void trace_dump_poly_stipple(const struct pipe_poly_stipple *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_poly_stipple");

   trace_dump_member_begin("stipple");
   trace_dump_array(uint,
                    state->stipple,
                    ARRAY_SIZE(state->stipple));
   trace_dump_member_end();

   trace_dump_struct_end();
}


void trace_dump_viewport_state(const struct pipe_viewport_state *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_viewport_state");

   trace_dump_member_array(float, state, scale);
   trace_dump_member_array(float, state, translate);

   trace_dump_struct_end();
}


void trace_dump_scissor_state(const struct pipe_scissor_state *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_scissor_state");

   trace_dump_member(uint, state, minx);
   trace_dump_member(uint, state, miny);
   trace_dump_member(uint, state, maxx);
   trace_dump_member(uint, state, maxy);

   trace_dump_struct_end();
}


void trace_dump_clip_state(const struct pipe_clip_state *state)
{
   unsigned i;

   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_clip_state");

   trace_dump_member_begin("ucp");
   trace_dump_array_begin();
   for(i = 0; i < PIPE_MAX_CLIP_PLANES; ++i) {
      trace_dump_elem_begin();
      trace_dump_array(float, state->ucp[i], 4);
      trace_dump_elem_end();
   }
   trace_dump_array_end();
   trace_dump_member_end();

   trace_dump_struct_end();
}


void trace_dump_shader_state(const struct pipe_shader_state *state)
{
   unsigned i;

   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }


   trace_dump_struct_begin("pipe_shader_state");

   trace_dump_member_begin("tokens");
   if (state->tokens) {
      static char str[64 * 1024];
      tgsi_dump_str(state->tokens, 0, str, sizeof(str));
      trace_dump_string(str);
   } else {
      trace_dump_null();
   }
   trace_dump_member_end();

   trace_dump_member_begin("stream_output");
   trace_dump_struct_begin("pipe_stream_output_info");
   trace_dump_member(uint, &state->stream_output, num_outputs);
   trace_dump_member_array(uint, &state->stream_output, stride);
   trace_dump_member_begin("output");
   trace_dump_array_begin();
   for(i = 0; i < state->stream_output.num_outputs; ++i) {
      trace_dump_elem_begin();
      trace_dump_struct_begin(""); /* anonymous */
      trace_dump_member(uint, &state->stream_output.output[i], register_index);
      trace_dump_member(uint, &state->stream_output.output[i], start_component);
      trace_dump_member(uint, &state->stream_output.output[i], num_components);
      trace_dump_member(uint, &state->stream_output.output[i], output_buffer);
      trace_dump_member(uint, &state->stream_output.output[i], dst_offset);
      trace_dump_member(uint, &state->stream_output.output[i], stream);
      trace_dump_struct_end();
      trace_dump_elem_end();
   }
   trace_dump_array_end();
   trace_dump_member_end(); // output
   trace_dump_struct_end();
   trace_dump_member_end(); // stream_output

   trace_dump_struct_end();
}


void trace_dump_compute_state(const struct pipe_compute_state *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_compute_state");

   trace_dump_member(uint, state, ir_type);

   trace_dump_member_begin("prog");
   if (state->prog && state->ir_type == PIPE_SHADER_IR_TGSI) {
      static char str[64 * 1024];
      tgsi_dump_str(state->prog, 0, str, sizeof(str));
      trace_dump_string(str);
   } else {
      trace_dump_null();
   }
   trace_dump_member_end();

   trace_dump_member(uint, state, req_local_mem);
   trace_dump_member(uint, state, req_private_mem);
   trace_dump_member(uint, state, req_input_mem);

   trace_dump_struct_end();
}


void trace_dump_depth_stencil_alpha_state(const struct pipe_depth_stencil_alpha_state *state)
{
   unsigned i;

   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_depth_stencil_alpha_state");

   trace_dump_member_begin("depth");
   trace_dump_struct_begin("pipe_depth_state");
   trace_dump_member(bool, &state->depth, enabled);
   trace_dump_member(bool, &state->depth, writemask);
   trace_dump_member(uint, &state->depth, func);
   trace_dump_struct_end();
   trace_dump_member_end();

   trace_dump_member_begin("stencil");
   trace_dump_array_begin();
   for(i = 0; i < ARRAY_SIZE(state->stencil); ++i) {
      trace_dump_elem_begin();
      trace_dump_struct_begin("pipe_stencil_state");
      trace_dump_member(bool, &state->stencil[i], enabled);
      trace_dump_member(uint, &state->stencil[i], func);
      trace_dump_member(uint, &state->stencil[i], fail_op);
      trace_dump_member(uint, &state->stencil[i], zpass_op);
      trace_dump_member(uint, &state->stencil[i], zfail_op);
      trace_dump_member(uint, &state->stencil[i], valuemask);
      trace_dump_member(uint, &state->stencil[i], writemask);
      trace_dump_struct_end();
      trace_dump_elem_end();
   }
   trace_dump_array_end();
   trace_dump_member_end();

   trace_dump_member_begin("alpha");
   trace_dump_struct_begin("pipe_alpha_state");
   trace_dump_member(bool, &state->alpha, enabled);
   trace_dump_member(uint, &state->alpha, func);
   trace_dump_member(float, &state->alpha, ref_value);
   trace_dump_struct_end();
   trace_dump_member_end();

   trace_dump_struct_end();
}

static void trace_dump_rt_blend_state(const struct pipe_rt_blend_state *state)
{
   trace_dump_struct_begin("pipe_rt_blend_state");

   trace_dump_member(uint, state, blend_enable);

   trace_dump_member(uint, state, rgb_func);
   trace_dump_member(uint, state, rgb_src_factor);
   trace_dump_member(uint, state, rgb_dst_factor);

   trace_dump_member(uint, state, alpha_func);
   trace_dump_member(uint, state, alpha_src_factor);
   trace_dump_member(uint, state, alpha_dst_factor);

   trace_dump_member(uint, state, colormask);

   trace_dump_struct_end();
}

void trace_dump_blend_state(const struct pipe_blend_state *state)
{
   unsigned valid_entries = 1;

   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_blend_state");

   trace_dump_member(bool, state, dither);

   trace_dump_member(bool, state, logicop_enable);
   trace_dump_member(uint, state, logicop_func);

   trace_dump_member(bool, state, independent_blend_enable);

   trace_dump_member_begin("rt");
   if (state->independent_blend_enable)
      valid_entries = PIPE_MAX_COLOR_BUFS;
   trace_dump_struct_array(rt_blend_state, state->rt, valid_entries);
   trace_dump_member_end();

   trace_dump_struct_end();
}


void trace_dump_blend_color(const struct pipe_blend_color *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_blend_color");

   trace_dump_member_array(float, state, color);

   trace_dump_struct_end();
}

void trace_dump_stencil_ref(const struct pipe_stencil_ref *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_stencil_ref");

   trace_dump_member_array(uint, state, ref_value);

   trace_dump_struct_end();
}

void trace_dump_framebuffer_state(const struct pipe_framebuffer_state *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   trace_dump_struct_begin("pipe_framebuffer_state");

   trace_dump_member(uint, state, width);
   trace_dump_member(uint, state, height);
   trace_dump_member(uint, state, samples);
   trace_dump_member(uint, state, layers);
   trace_dump_member(uint, state, nr_cbufs);
   trace_dump_member_array(ptr, state, cbufs);
   trace_dump_member(ptr, state, zsbuf);

   trace_dump_struct_end();
}


void trace_dump_sampler_state(const struct pipe_sampler_state *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_sampler_state");

   trace_dump_member(uint, state, wrap_s);
   trace_dump_member(uint, state, wrap_t);
   trace_dump_member(uint, state, wrap_r);
   trace_dump_member(uint, state, min_img_filter);
   trace_dump_member(uint, state, min_mip_filter);
   trace_dump_member(uint, state, mag_img_filter);
   trace_dump_member(uint, state, compare_mode);
   trace_dump_member(uint, state, compare_func);
   trace_dump_member(bool, state, normalized_coords);
   trace_dump_member(uint, state, max_anisotropy);
   trace_dump_member(bool, state, seamless_cube_map);
   trace_dump_member(float, state, lod_bias);
   trace_dump_member(float, state, min_lod);
   trace_dump_member(float, state, max_lod);
   trace_dump_member_array(float, state, border_color.f);

   trace_dump_struct_end();
}


void trace_dump_sampler_view_template(const struct pipe_sampler_view *state,
                                      enum pipe_texture_target target)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_sampler_view");

   trace_dump_member(format, state, format);

   trace_dump_member_begin("u");
   trace_dump_struct_begin(""); /* anonymous */
   if (target == PIPE_BUFFER) {
      trace_dump_member_begin("buf");
      trace_dump_struct_begin(""); /* anonymous */
      trace_dump_member(uint, &state->u.buf, offset);
      trace_dump_member(uint, &state->u.buf, size);
      trace_dump_struct_end(); /* anonymous */
      trace_dump_member_end(); /* buf */
   } else {
      trace_dump_member_begin("tex");
      trace_dump_struct_begin(""); /* anonymous */
      trace_dump_member(uint, &state->u.tex, first_layer);
      trace_dump_member(uint, &state->u.tex, last_layer);
      trace_dump_member(uint, &state->u.tex, first_level);
      trace_dump_member(uint, &state->u.tex, last_level);
      trace_dump_struct_end(); /* anonymous */
      trace_dump_member_end(); /* tex */
   }
   trace_dump_struct_end(); /* anonymous */
   trace_dump_member_end(); /* u */

   trace_dump_member(uint, state, swizzle_r);
   trace_dump_member(uint, state, swizzle_g);
   trace_dump_member(uint, state, swizzle_b);
   trace_dump_member(uint, state, swizzle_a);

   trace_dump_struct_end();
}


void trace_dump_surface_template(const struct pipe_surface *state,
                                 enum pipe_texture_target target)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_surface");

   trace_dump_member(format, state, format);
   trace_dump_member(uint, state, width);
   trace_dump_member(uint, state, height);

   trace_dump_member_begin("u");
   trace_dump_struct_begin(""); /* anonymous */
   if (target == PIPE_BUFFER) {
      trace_dump_member_begin("buf");
      trace_dump_struct_begin(""); /* anonymous */
      trace_dump_member(uint, &state->u.buf, first_element);
      trace_dump_member(uint, &state->u.buf, last_element);
      trace_dump_struct_end(); /* anonymous */
      trace_dump_member_end(); /* buf */
   } else {
      trace_dump_member_begin("tex");
      trace_dump_struct_begin(""); /* anonymous */
      trace_dump_member(uint, &state->u.tex, level);
      trace_dump_member(uint, &state->u.tex, first_layer);
      trace_dump_member(uint, &state->u.tex, last_layer);
      trace_dump_struct_end(); /* anonymous */
      trace_dump_member_end(); /* tex */
   }
   trace_dump_struct_end(); /* anonymous */
   trace_dump_member_end(); /* u */

   trace_dump_struct_end();
}


void trace_dump_transfer(const struct pipe_transfer *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_transfer");

   trace_dump_member(uint, state, box.x);
   trace_dump_member(uint, state, box.y);
   trace_dump_member(uint, state, box.z);
   trace_dump_member(uint, state, box.width);
   trace_dump_member(uint, state, box.height);
   trace_dump_member(uint, state, box.depth);

   trace_dump_member(uint, state, stride);
   trace_dump_member(uint, state, layer_stride);
   trace_dump_member(uint, state, usage);

   trace_dump_member(ptr, state, resource);

   trace_dump_struct_end();
}


void trace_dump_vertex_buffer(const struct pipe_vertex_buffer *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_vertex_buffer");

   trace_dump_member(uint, state, stride);
   trace_dump_member(bool, state, is_user_buffer);
   trace_dump_member(uint, state, buffer_offset);
   trace_dump_member(ptr, state, buffer.resource);

   trace_dump_struct_end();
}


void trace_dump_vertex_element(const struct pipe_vertex_element *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_vertex_element");

   trace_dump_member(uint, state, src_offset);

   trace_dump_member(uint, state, vertex_buffer_index);

   trace_dump_member(format, state, src_format);

   trace_dump_struct_end();
}


void trace_dump_constant_buffer(const struct pipe_constant_buffer *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_constant_buffer");
   trace_dump_member(ptr, state, buffer);
   trace_dump_member(uint, state, buffer_offset);
   trace_dump_member(uint, state, buffer_size);
   trace_dump_struct_end();
}


void trace_dump_shader_buffer(const struct pipe_shader_buffer *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if(!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_shader_buffer");
   trace_dump_member(ptr, state, buffer);
   trace_dump_member(uint, state, buffer_offset);
   trace_dump_member(uint, state, buffer_size);
   trace_dump_struct_end();
}


void trace_dump_image_view(const struct pipe_image_view *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if(!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_image_view");
   trace_dump_member(ptr, state, resource);
   trace_dump_member(uint, state, format);
   trace_dump_member(uint, state, access);

   trace_dump_member_begin("u");
   trace_dump_struct_begin(""); /* anonymous */
   if (state->resource->target == PIPE_BUFFER) {
      trace_dump_member_begin("buf");
      trace_dump_struct_begin(""); /* anonymous */
      trace_dump_member(uint, &state->u.buf, offset);
      trace_dump_member(uint, &state->u.buf, size);
      trace_dump_struct_end(); /* anonymous */
      trace_dump_member_end(); /* buf */
   } else {
      trace_dump_member_begin("tex");
      trace_dump_struct_begin(""); /* anonymous */
      trace_dump_member(uint, &state->u.tex, first_layer);
      trace_dump_member(uint, &state->u.tex, last_layer);
      trace_dump_member(uint, &state->u.tex, level);
      trace_dump_struct_end(); /* anonymous */
      trace_dump_member_end(); /* tex */
   }
   trace_dump_struct_end(); /* anonymous */
   trace_dump_member_end(); /* u */

   trace_dump_struct_end();
}


void trace_dump_draw_info(const struct pipe_draw_info *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_draw_info");

   trace_dump_member(uint, state, index_size);
   trace_dump_member(uint, state, has_user_indices);

   trace_dump_member(uint, state, mode);
   trace_dump_member(uint, state, start);
   trace_dump_member(uint, state, count);

   trace_dump_member(uint, state, start_instance);
   trace_dump_member(uint, state, instance_count);

   trace_dump_member(uint, state, vertices_per_patch);

   trace_dump_member(int,  state, index_bias);
   trace_dump_member(uint, state, min_index);
   trace_dump_member(uint, state, max_index);

   trace_dump_member(bool, state, primitive_restart);
   trace_dump_member(uint, state, restart_index);

   trace_dump_member(ptr, state, index.resource);
   trace_dump_member(ptr, state, count_from_stream_output);

   if (!state->indirect) {
      trace_dump_member(ptr, state, indirect);
   } else {
      trace_dump_member(uint, state, indirect->offset);
      trace_dump_member(uint, state, indirect->stride);
      trace_dump_member(uint, state, indirect->draw_count);
      trace_dump_member(uint, state, indirect->indirect_draw_count_offset);
      trace_dump_member(ptr, state, indirect->buffer);
      trace_dump_member(ptr, state, indirect->indirect_draw_count);
   }

   trace_dump_struct_end();
}

void trace_dump_blit_info(const struct pipe_blit_info *info)
{
   char mask[7];

   if (!trace_dumping_enabled_locked())
      return;

   if (!info) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_blit_info");

   trace_dump_member_begin("dst");
   trace_dump_struct_begin("dst");
   trace_dump_member(ptr, &info->dst, resource);
   trace_dump_member(uint, &info->dst, level);
   trace_dump_member(format, &info->dst, format);
   trace_dump_member_begin("box");
   trace_dump_box(&info->dst.box);
   trace_dump_member_end();
   trace_dump_struct_end();
   trace_dump_member_end();

   trace_dump_member_begin("src");
   trace_dump_struct_begin("src");
   trace_dump_member(ptr, &info->src, resource);
   trace_dump_member(uint, &info->src, level);
   trace_dump_member(format, &info->src, format);
   trace_dump_member_begin("box");
   trace_dump_box(&info->src.box);
   trace_dump_member_end();
   trace_dump_struct_end();
   trace_dump_member_end();

   mask[0] = (info->mask & PIPE_MASK_R) ? 'R' : '-';
   mask[1] = (info->mask & PIPE_MASK_G) ? 'G' : '-';
   mask[2] = (info->mask & PIPE_MASK_B) ? 'B' : '-';
   mask[3] = (info->mask & PIPE_MASK_A) ? 'A' : '-';
   mask[4] = (info->mask & PIPE_MASK_Z) ? 'Z' : '-';
   mask[5] = (info->mask & PIPE_MASK_S) ? 'S' : '-';
   mask[6] = 0;

   trace_dump_member_begin("mask");
   trace_dump_string(mask);
   trace_dump_member_end();
   trace_dump_member(uint, info, filter);

   trace_dump_member(bool, info, scissor_enable);
   trace_dump_member_begin("scissor");
   trace_dump_scissor_state(&info->scissor);
   trace_dump_member_end();

   trace_dump_struct_end();
}

void
trace_dump_query_result(unsigned query_type,
                        const union pipe_query_result *result)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!result) {
      trace_dump_null();
      return;
   }

   switch (query_type) {
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
   case PIPE_QUERY_GPU_FINISHED:
      trace_dump_bool(result->b);
      break;

   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
   case PIPE_QUERY_PRIMITIVES_GENERATED:
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      trace_dump_uint(result->u64);
      break;

   case PIPE_QUERY_SO_STATISTICS:
      trace_dump_struct_begin("pipe_query_data_so_statistics");
      trace_dump_member(uint, &result->so_statistics, num_primitives_written);
      trace_dump_member(uint, &result->so_statistics, primitives_storage_needed);
      trace_dump_struct_end();
      break;

   case PIPE_QUERY_TIMESTAMP_DISJOINT:
      trace_dump_struct_begin("pipe_query_data_timestamp_disjoint");
      trace_dump_member(uint, &result->timestamp_disjoint, frequency);
      trace_dump_member(bool, &result->timestamp_disjoint, disjoint);
      trace_dump_struct_end();
      break;

   case PIPE_QUERY_PIPELINE_STATISTICS:
      trace_dump_struct_begin("pipe_query_data_pipeline_statistics");
      trace_dump_member(uint, &result->pipeline_statistics, ia_vertices);
      trace_dump_member(uint, &result->pipeline_statistics, ia_primitives);
      trace_dump_member(uint, &result->pipeline_statistics, vs_invocations);
      trace_dump_member(uint, &result->pipeline_statistics, gs_invocations);
      trace_dump_member(uint, &result->pipeline_statistics, gs_primitives);
      trace_dump_member(uint, &result->pipeline_statistics, c_invocations);
      trace_dump_member(uint, &result->pipeline_statistics, c_primitives);
      trace_dump_member(uint, &result->pipeline_statistics, ps_invocations);
      trace_dump_member(uint, &result->pipeline_statistics, hs_invocations);
      trace_dump_member(uint, &result->pipeline_statistics, ds_invocations);
      trace_dump_member(uint, &result->pipeline_statistics, cs_invocations);
      trace_dump_struct_end();
      break;

   default:
      assert(query_type >= PIPE_QUERY_DRIVER_SPECIFIC);
      trace_dump_uint(result->u64);
      break;
   }
}

void trace_dump_grid_info(const struct pipe_grid_info *state)
{
   if (!trace_dumping_enabled_locked())
      return;

   if (!state) {
      trace_dump_null();
      return;
   }

   trace_dump_struct_begin("pipe_grid_info");

   trace_dump_member(uint, state, pc);
   trace_dump_member(ptr, state, input);

   trace_dump_member_begin("block");
   trace_dump_array(uint, state->block, ARRAY_SIZE(state->block));
   trace_dump_member_end();

   trace_dump_member_begin("grid");
   trace_dump_array(uint, state->grid, ARRAY_SIZE(state->grid));
   trace_dump_member_end();

   trace_dump_member(ptr, state, indirect);
   trace_dump_member(uint, state, indirect_offset);

   trace_dump_struct_end();
}

