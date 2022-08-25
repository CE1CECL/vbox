/*
 * Copyright (C) 2021 Collabora, Ltd.
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler.h"
#include "bi_builder.h"

/*
 * Due to a Bifrost encoding restriction, some instructions cannot have an abs
 * modifier on both sources. Check if adding a fabs modifier to a given source
 * of a binary instruction would cause this restriction to be hit.
 */
static bool
bi_would_impact_abs(unsigned arch, bi_instr *I, bi_index repl, unsigned s)
{
        return (arch <= 8) && I->src[1 - s].abs &&
               bi_is_word_equiv(I->src[1 - s], repl);
}

static bool
bi_takes_fabs(unsigned arch, bi_instr *I, bi_index repl, unsigned s)
{
        switch (I->op) {
        case BI_OPCODE_FCMP_V2F16:
        case BI_OPCODE_FMAX_V2F16:
        case BI_OPCODE_FMIN_V2F16:
                return !bi_would_impact_abs(arch, I, repl, s);
        case BI_OPCODE_FADD_V2F16:
                /*
                 * For FADD.v2f16, the FMA pipe has the abs encoding hazard,
                 * while the FADD pipe cannot encode a clamp. Either case in
                 * isolation can be worked around in the scheduler, but both
                 * together is impossible to encode. Avoid the hazard.
                 */
                return !(I->clamp && bi_would_impact_abs(arch, I, repl, s));
        case BI_OPCODE_V2F32_TO_V2F16:
                /* TODO: Needs both match or lower */
                return false;
        case BI_OPCODE_FLOG_TABLE_F32:
                /* TODO: Need to check mode */
                return false;
        default:
                return bi_opcode_props[I->op].abs & BITFIELD_BIT(s);
        }
}

static bool
bi_takes_fneg(unsigned arch, bi_instr *I, unsigned s)
{
        switch (I->op) {
        case BI_OPCODE_CUBE_SSEL:
        case BI_OPCODE_CUBE_TSEL:
        case BI_OPCODE_CUBEFACE:
                /* TODO: Bifrost encoding restriction: need to match or lower */
                return arch >= 9;
        case BI_OPCODE_FREXPE_F32:
        case BI_OPCODE_FREXPE_V2F16:
        case BI_OPCODE_FLOG_TABLE_F32:
                /* TODO: Need to check mode */
                return false;
        default:
                return bi_opcode_props[I->op].neg & BITFIELD_BIT(s);
        }
}

static bool
bi_is_fabsneg(enum bi_opcode op, enum bi_size size)
{
        return (size == BI_SIZE_32 && op == BI_OPCODE_FABSNEG_F32) ||
               (size == BI_SIZE_16 && op == BI_OPCODE_FABSNEG_V2F16);
}

static enum bi_swizzle
bi_compose_swizzle_16(enum bi_swizzle a, enum bi_swizzle b)
{
        assert(a <= BI_SWIZZLE_H11);
        assert(b <= BI_SWIZZLE_H11);

        bool al = (a & BI_SWIZZLE_H10);
        bool ar = (a & BI_SWIZZLE_H01);
        bool bl = (b & BI_SWIZZLE_H10);
        bool br = (b & BI_SWIZZLE_H01);

        return ((al ? br : bl) ? BI_SWIZZLE_H10 : 0) |
               ((ar ? br : bl) ? BI_SWIZZLE_H01 : 0);
}

/* Like bi_replace_index, but composes instead of overwrites */

static inline bi_index
bi_compose_float_index(bi_index old, bi_index repl)
{
        /* abs(-x) = abs(+x) so ignore repl.neg if old.abs is set, otherwise
         * -(-x) = x but -(+x) = +(-x) so need to exclusive-or the negates */
        repl.neg = old.neg ^ (repl.neg && !old.abs);

        /* +/- abs(+/- abs(x)) = +/- abs(x), etc so just or the two */
        repl.abs |= old.abs;

        /* Use the old swizzle to select from the replacement swizzle */
        repl.swizzle = bi_compose_swizzle_16(old.swizzle, repl.swizzle);

        return repl;
}

/* DISCARD.b32(FCMP.f(x, y)) --> DISCARD.f(x, y) */

static inline void
bi_fuse_discard_fcmp(bi_instr *I, bi_instr *mod, unsigned arch)
{
        if (I->op != BI_OPCODE_DISCARD_B32) return;
        if (mod->op != BI_OPCODE_FCMP_F32 && mod->op != BI_OPCODE_FCMP_V2F16) return;
        if (mod->cmpf >= BI_CMPF_GTLT) return;

        /* .abs and .neg modifiers allowed on Valhall DISCARD but not Bifrost */
        bool absneg = mod->src[0].neg || mod->src[0].abs;
        absneg     |= mod->src[1].neg || mod->src[1].abs;

        if (arch <= 8 && absneg) return;

        enum bi_swizzle r = I->src[0].swizzle;

        /* result_type doesn't matter */
        I->op = BI_OPCODE_DISCARD_F32;
        I->cmpf = mod->cmpf;
        I->src[0] = mod->src[0];
        I->src[1] = mod->src[1];

        if (mod->op == BI_OPCODE_FCMP_V2F16) {
                I->src[0].swizzle = bi_compose_swizzle_16(r, I->src[0].swizzle);
                I->src[1].swizzle = bi_compose_swizzle_16(r, I->src[1].swizzle);
        }
}

void
bi_opt_mod_prop_forward(bi_context *ctx)
{
        bi_instr **lut = calloc(sizeof(bi_instr *), ((ctx->ssa_alloc + 1) << 2));

        bi_foreach_instr_global_safe(ctx, I) {
                if (bi_is_ssa(I->dest[0]))
                        lut[bi_word_node(I->dest[0])] = I;

                bi_foreach_src(I, s) {
                        if (!bi_is_ssa(I->src[s]))
                                continue;

                        bi_instr *mod = lut[bi_word_node(I->src[s])];

                        if (!mod)
                                continue;

                        unsigned size = bi_opcode_props[I->op].size;

                        bi_fuse_discard_fcmp(I, mod, ctx->arch);

                        if (bi_is_fabsneg(mod->op, size)) {
                                if (mod->src[0].abs && !bi_takes_fabs(ctx->arch, I, mod->src[0], s))
                                        continue;

                                if (mod->src[0].neg && !bi_takes_fneg(ctx->arch, I, s))
                                        continue;

                                I->src[s] = bi_compose_float_index(I->src[s], mod->src[0]);
                        }
                }
        }

        free(lut);
}

/* RSCALE has restrictions on how the clamp may be used, only used for
 * specialized transcendental sequences that set the clamp explicitly anyway */

static bool
bi_takes_clamp(bi_instr *I)
{
        switch (I->op) {
        case BI_OPCODE_FMA_RSCALE_F32:
        case BI_OPCODE_FMA_RSCALE_V2F16:
        case BI_OPCODE_FADD_RSCALE_F32:
                return false;
        case BI_OPCODE_FADD_V2F16:
                /* Encoding restriction */
                return !(I->src[0].abs && I->src[1].abs &&
                         bi_is_word_equiv(I->src[0], I->src[1]));
        default:
                return bi_opcode_props[I->op].clamp;
        }
}

static bool
bi_is_fclamp(enum bi_opcode op, enum bi_size size)
{
        return (size == BI_SIZE_32 && op == BI_OPCODE_FCLAMP_F32) ||
               (size == BI_SIZE_16 && op == BI_OPCODE_FCLAMP_V2F16);
}

static bool
bi_optimizer_clamp(bi_instr *I, bi_instr *use)
{
        if (!bi_is_fclamp(use->op, bi_opcode_props[I->op].size)) return false;
        if (!bi_takes_clamp(I)) return false;

        /* Clamps are bitfields (clamp_m1_1/clamp_0_inf) so composition is OR */
        I->clamp |= use->clamp;
        I->dest[0] = use->dest[0];
        return true;
}

static bool
bi_is_var_tex(bi_instr *var, bi_instr *tex)
{
        return (var->op == BI_OPCODE_LD_VAR_IMM) &&
                (tex->op == BI_OPCODE_TEXS_2D_F16 || tex->op == BI_OPCODE_TEXS_2D_F32) &&
                (var->register_format == BI_REGISTER_FORMAT_F32) &&
                ((var->sample == BI_SAMPLE_CENTER && var->update == BI_UPDATE_STORE) ||
                 (var->sample == BI_SAMPLE_NONE && var->update == BI_UPDATE_RETRIEVE)) &&
                (tex->texture_index == tex->sampler_index) &&
                (tex->texture_index < 4) &&
                (var->index < 8);
}

static bool
bi_optimizer_var_tex(bi_context *ctx, bi_instr *var, bi_instr *tex)
{
        if (!bi_is_var_tex(var, tex)) return false;

        /* Construct the corresponding VAR_TEX intruction */
        bi_builder b = bi_init_builder(ctx, bi_after_instr(var));

        bi_instr *I = bi_var_tex_f32_to(&b, tex->dest[0], tex->lod_mode,
                        var->sample, var->update, tex->texture_index, var->index);
        I->skip = tex->skip;

        if (tex->op == BI_OPCODE_TEXS_2D_F16)
                I->op = BI_OPCODE_VAR_TEX_F16;

        /* Dead code elimination will clean up for us */
        return true;
}

void
bi_opt_mod_prop_backward(bi_context *ctx)
{
        unsigned count = ((ctx->ssa_alloc + 1) << 2);
        bi_instr **uses = calloc(count, sizeof(*uses));
        BITSET_WORD *multiple = calloc(BITSET_WORDS(count), sizeof(*multiple));

        bi_foreach_instr_global_rev(ctx, I) {
                bi_foreach_src(I, s) {
                        if (bi_is_ssa(I->src[s])) {
                                unsigned v = bi_word_node(I->src[s]);

                                if (uses[v] && uses[v] != I)
                                        BITSET_SET(multiple, v);
                                else
                                        uses[v] = I;
                        }
                }

                if (!bi_is_ssa(I->dest[0]))
                        continue;

                bi_instr *use = uses[bi_word_node(I->dest[0])];

                if (!use || BITSET_TEST(multiple, bi_word_node(I->dest[0])))
                        continue;

                /* Destination has a single use, try to propagate */
                bool propagated =
                        bi_optimizer_clamp(I, use) ||
                        bi_optimizer_var_tex(ctx, I, use);

                if (propagated) {
                        bi_remove_instruction(use);
                        continue;
                }
        }

        free(uses);
        free(multiple);
}

/** Lower pseudo instructions that exist to simplify the optimizer */

void
bi_lower_opt_instruction(bi_instr *I)
{
        switch (I->op) {
        case BI_OPCODE_FABSNEG_F32:
        case BI_OPCODE_FABSNEG_V2F16:
        case BI_OPCODE_FCLAMP_F32:
        case BI_OPCODE_FCLAMP_V2F16:
                I->op = (bi_opcode_props[I->op].size == BI_SIZE_32) ?
                        BI_OPCODE_FADD_F32 : BI_OPCODE_FADD_V2F16;

                I->round = BI_ROUND_NONE;
                I->src[1] = bi_negzero();
                break;

        case BI_OPCODE_DISCARD_B32:
                I->op = BI_OPCODE_DISCARD_F32;
                I->src[1] = bi_imm_u16(0);
                I->cmpf = BI_CMPF_NE;
                break;

        default:
                break;
        }
}
