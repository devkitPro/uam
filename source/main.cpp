#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_dump.h"

#include "codegen/nv50_ir_driver.h"

#include "glsl_frontend.h"

/* NOTE: Using a[0x270] in FP may cause an error even if we're using less than
 * 124 scalar varying values.
 */
static uint32_t
nvc0_shader_input_address(unsigned sn, unsigned si)
{
	switch (sn) {
	case TGSI_SEMANTIC_TESSOUTER:    return 0x000 + si * 0x4;
	case TGSI_SEMANTIC_TESSINNER:    return 0x010 + si * 0x4;
	case TGSI_SEMANTIC_PATCH:        return 0x020 + si * 0x10;
	case TGSI_SEMANTIC_PRIMID:       return 0x060;
	case TGSI_SEMANTIC_LAYER:        return 0x064;
	case TGSI_SEMANTIC_VIEWPORT_INDEX:return 0x068;
	case TGSI_SEMANTIC_PSIZE:        return 0x06c;
	case TGSI_SEMANTIC_POSITION:     return 0x070;
	case TGSI_SEMANTIC_GENERIC:      return 0x080 + si * 0x10;
	case TGSI_SEMANTIC_FOG:          return 0x2e8;
	case TGSI_SEMANTIC_COLOR:        return 0x280 + si * 0x10;
	case TGSI_SEMANTIC_BCOLOR:       return 0x2a0 + si * 0x10;
	case TGSI_SEMANTIC_CLIPDIST:     return 0x2c0 + si * 0x10;
	case TGSI_SEMANTIC_CLIPVERTEX:   return 0x270;
	case TGSI_SEMANTIC_PCOORD:       return 0x2e0;
	case TGSI_SEMANTIC_TESSCOORD:    return 0x2f0;
	case TGSI_SEMANTIC_INSTANCEID:   return 0x2f8;
	case TGSI_SEMANTIC_VERTEXID:     return 0x2fc;
	case TGSI_SEMANTIC_TEXCOORD:     return 0x300 + si * 0x10;
	default:
		assert(!"invalid TGSI input semantic");
		return ~0;
	}
}

static uint32_t
nvc0_shader_output_address(unsigned sn, unsigned si)
{
	switch (sn) {
	case TGSI_SEMANTIC_TESSOUTER:     return 0x000 + si * 0x4;
	case TGSI_SEMANTIC_TESSINNER:     return 0x010 + si * 0x4;
	case TGSI_SEMANTIC_PATCH:         return 0x020 + si * 0x10;
	case TGSI_SEMANTIC_PRIMID:        return 0x060;
	case TGSI_SEMANTIC_LAYER:         return 0x064;
	case TGSI_SEMANTIC_VIEWPORT_INDEX:return 0x068;
	case TGSI_SEMANTIC_PSIZE:         return 0x06c;
	case TGSI_SEMANTIC_POSITION:      return 0x070;
	case TGSI_SEMANTIC_GENERIC:       return 0x080 + si * 0x10;
	case TGSI_SEMANTIC_FOG:           return 0x2e8;
	case TGSI_SEMANTIC_COLOR:         return 0x280 + si * 0x10;
	case TGSI_SEMANTIC_BCOLOR:        return 0x2a0 + si * 0x10;
	case TGSI_SEMANTIC_CLIPDIST:      return 0x2c0 + si * 0x10;
	case TGSI_SEMANTIC_CLIPVERTEX:    return 0x270;
	case TGSI_SEMANTIC_TEXCOORD:      return 0x300 + si * 0x10;
	/* case TGSI_SEMANTIC_VIEWPORT_MASK: return 0x3a0; */
	case TGSI_SEMANTIC_EDGEFLAG:      return ~0;
	default:
		assert(!"invalid TGSI output semantic");
		return ~0;
	}
}

static int
nvc0_vp_assign_input_slots(struct nv50_ir_prog_info *info)
{
	unsigned i, c, n;

	for (n = 0, i = 0; i < info->numInputs; ++i) {
		switch (info->in[i].sn) {
		case TGSI_SEMANTIC_INSTANCEID: /* for SM4 only, in TGSI they're SVs */
		case TGSI_SEMANTIC_VERTEXID:
			info->in[i].mask = 0x1;
			info->in[i].slot[0] =
				nvc0_shader_input_address(info->in[i].sn, 0) / 4;
			continue;
		default:
			break;
		}
		for (c = 0; c < 4; ++c)
			info->in[i].slot[c] = (0x80 + n * 0x10 + c * 0x4) / 4;
		++n;
	}

	return 0;
}

static int
nvc0_sp_assign_input_slots(struct nv50_ir_prog_info *info)
{
	unsigned offset;
	unsigned i, c;

	for (i = 0; i < info->numInputs; ++i) {
		offset = nvc0_shader_input_address(info->in[i].sn, info->in[i].si);

		for (c = 0; c < 4; ++c)
			info->in[i].slot[c] = (offset + c * 0x4) / 4;
	}

	return 0;
}

static int
nvc0_fp_assign_output_slots(struct nv50_ir_prog_info *info)
{
	unsigned count = info->prop.fp.numColourResults * 4;
	unsigned i, c;

	/* Compute the relative position of each color output, since skipped MRT
		* positions will not have registers allocated to them.
		*/
	unsigned colors[8] = {0};
	for (i = 0; i < info->numOutputs; ++i)
		if (info->out[i].sn == TGSI_SEMANTIC_COLOR)
			colors[info->out[i].si] = 1;
	for (i = 0, c = 0; i < 8; i++)
		if (colors[i])
			colors[i] = c++;
	for (i = 0; i < info->numOutputs; ++i)
		if (info->out[i].sn == TGSI_SEMANTIC_COLOR)
			for (c = 0; c < 4; ++c)
				info->out[i].slot[c] = colors[info->out[i].si] * 4 + c;

	if (info->io.sampleMask < PIPE_MAX_SHADER_OUTPUTS)
		info->out[info->io.sampleMask].slot[0] = count++;
	else
	if (info->target >= 0xe0)
		count++; /* on Kepler, depth is always last colour reg + 2 */

	if (info->io.fragDepth < PIPE_MAX_SHADER_OUTPUTS)
		info->out[info->io.fragDepth].slot[2] = count;

	return 0;
}

static int
nvc0_sp_assign_output_slots(struct nv50_ir_prog_info *info)
{
	unsigned offset;
	unsigned i, c;

	for (i = 0; i < info->numOutputs; ++i) {
		offset = nvc0_shader_output_address(info->out[i].sn, info->out[i].si);

		for (c = 0; c < 4; ++c)
			info->out[i].slot[c] = (offset + c * 0x4) / 4;
	}

	return 0;
}

static int
nvc0_program_assign_varying_slots(struct nv50_ir_prog_info *info)
{
	int ret;

	if (info->type == PIPE_SHADER_VERTEX)
		ret = nvc0_vp_assign_input_slots(info);
	else
		ret = nvc0_sp_assign_input_slots(info);
	if (ret)
		return ret;

	if (info->type == PIPE_SHADER_FRAGMENT)
		ret = nvc0_fp_assign_output_slots(info);
	else
		ret = nvc0_sp_assign_output_slots(info);
	return ret;
}

void another_test(const char* glsl_source);

int main(int argc, char* argv[])
{
	glsl_frontend_init();

	printf("Hello, world!\n");

	const char* glsl_source = R"(
#version 330 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

out gl_PerVertex
{
	vec4 gl_Position;
};

layout (location = 0) in int inAttr;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec3 outUV;

uniform ivec2 dimensions;
uniform vec4 palettes[16] = vec4[](
	vec4(0.0, 0.0, 0.0, 1.0),
	vec4(0.5, 0.0, 0.0, 1.0),
	vec4(0.0, 0.5, 0.0, 1.0),
	vec4(0.5, 0.5, 0.0, 1.0),
	vec4(0.0, 0.0, 0.5, 1.0),
	vec4(0.5, 0.0, 0.5, 1.0),
	vec4(0.0, 0.5, 0.5, 1.0),
	vec4(0.75, 0.75, 0.75, 1.0),
	vec4(0.5, 0.5, 0.5, 1.0),
	vec4(1.0, 0.0, 0.0, 1.0),
	vec4(0.0, 1.0, 0.0, 1.0),
	vec4(1.0, 1.0, 0.0, 1.0),
	vec4(0.0, 0.0, 1.0, 1.0),
	vec4(1.0, 0.0, 1.0, 1.0),
	vec4(0.0, 1.0, 1.0, 1.0),
	vec4(1.0, 1.0, 1.0, 1.0)
);

const vec2 builtin_vertices[] = vec2[](
	vec2(0.0, 0.0),
	vec2(0.0, -1.0),
	vec2(1.0, -1.0),
	vec2(0.0, 0.0),
	vec2(1.0, -1.0),
	vec2(1.0, 0.0)
);

void main()
{
	// Extract data from the attribute
	float tileId = float(inAttr & 0x3FF);
	bool hFlip = ((inAttr >> 10) & 1) != 0;
	bool vFlip = ((inAttr >> 11) & 1) != 0;
	int palId = (inAttr >> 12) & 0xF;

	vec2 vtxData = builtin_vertices[gl_VertexID];

	// Position
	float tileRow = floor(float(gl_InstanceID) / dimensions.x);
	float tileCol = float(gl_InstanceID) - tileRow*dimensions.x;
	vec2 basePos;
	basePos.x = 2.0 * tileCol / dimensions.x - 1.0;
	basePos.y = 2.0 * (1.0 - tileRow / dimensions.y) - 1.0;

	vec2 offsetPos = vec2(2.0) / vec2(dimensions.xy);
	gl_Position.xy = basePos + offsetPos * vtxData;
	gl_Position.zw = vec2(0.0, 1.0);

	// Color
	outColor = palettes[palId];

	// UVs
	if (hFlip)
		vtxData.x = 1.0 - vtxData.x;
	if (vFlip)
		vtxData.y = -1.0 - vtxData.y;
	outUV.xy = vec2(0.0,1.0) + vtxData;
	outUV.z  = tileId;
}
)";

	glsl_program prg = glsl_program_create(glsl_source, pipeline_stage_vertex);
	if (!prg)
	{
		glsl_frontend_exit();
		return EXIT_FAILURE;
	}

	unsigned int num_tokens;
	const struct tgsi_token* tokens = glsl_program_get_tokens(prg, num_tokens);
	tgsi_dump_to_file(tokens, TGSI_DUMP_FLOAT_AS_HEX, stdout);

	struct nv50_ir_prog_info info = {0};
	info.type = PIPE_SHADER_VERTEX;
	info.target = 0x12b;
	info.bin.sourceRep = PIPE_SHADER_IR_TGSI;
	info.bin.source = tokens;

	info.optLevel = 3;

	//info.bin.smemSize = prog->cp.smem_size;
	//info.io.genUserClip = prog->vp.num_ucps;
	info.io.auxCBSlot = 15;
	info.io.msInfoCBSlot = 15;
	info.io.ucpBase = 0x120; //NVC0_CB_AUX_UCP_INFO;
	info.io.drawInfoBase = 0x1a0; //NVC0_CB_AUX_DRAW_INFO;
	info.io.msInfoBase = 0x0c0; //NVC0_CB_AUX_MS_INFO;
	info.io.bufInfoBase = 0x2a0; //NVC0_CB_AUX_BUF_INFO(0);
	info.io.suInfoBase = 0x4a0; //NVC0_CB_AUX_SU_INFO(0);
	info.io.texBindBase = 0x020; //NVC0_CB_AUX_TEX_INFO(0);
	info.io.fbtexBindBase = 0x100; //NVC0_CB_AUX_FB_TEX_INFO;
	info.io.bindlessBase = 0x6b0; //NVC0_CB_AUX_BINDLESS_INFO(0);
	info.io.sampleInfoBase = 0x1a0; //NVC0_CB_AUX_SAMPLE_INFO;

	info.assignSlots = nvc0_program_assign_varying_slots;

	int ret = nv50_ir_generate_code(&info);
	glsl_program_free(prg);
	if (ret) {
		fprintf(stderr, "Error compiling program: %d\n", ret);
		return ret;
	}

	/*
	printf("program binary (%d bytes)\n", info.bin.codeSize);
	uint32_t i;
	for (i = 0; i < info.bin.codeSize; i += 4) {
		printf("%08x ", info.bin.code[i / 4]);
		if (i % (8 * 4) == (7 * 4))
			printf("\n");
		}
	if (i % (8 * 4) != 0)
		printf("\n");
	*/

	FILE* f = fopen("program.bin", "wb");
	if (f)
	{
		fwrite(info.bin.code, 1, info.bin.codeSize, f);
		uint32_t numThings = info.bin.codeSize/8;
		uint32_t remaining = ((numThings+7)&~7) - numThings;
		//bool didBra = false;
		for (uint32_t i = 0; i < remaining; i ++)
		{
			uint64_t dummy = 0;
			/* TODO: figure out why this makes nvdisasm crap its pants
			if (!((numThings+i)&3))
				dummy = UINT64_C(0x001f8000fc0007e0); // (sched)
			else if (!didBra)
			{
				dummy = UINT64_C(0xe2400fffff87000f); // BRA $;
				didBra = true;
			}
			else
				dummy = UINT64_C(0x50b0000000070f00); // NOP;
			*/
			fwrite(&dummy, sizeof(uint64_t), 1, f);
		}
		fclose(f);
	}

	glsl_frontend_exit();
	return EXIT_SUCCESS;
}
