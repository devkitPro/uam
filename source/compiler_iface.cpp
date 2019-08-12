#include "compiler_iface.h"

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

DekoCompiler::DekoCompiler(pipeline_stage stage, int optLevel) :
	m_stage{stage}, m_glsl{}, m_tgsi{}, m_tgsiNumTokens{}, m_info{}, m_code{}, m_codeSize{}
{
	uint16_t resbase;
	switch (stage)
	{
		default:
		case pipeline_stage_vertex:
			m_info.type = PIPE_SHADER_VERTEX;
			resbase = 0x010;
			break;
		case pipeline_stage_tess_ctrl:
			m_info.type = PIPE_SHADER_TESS_CTRL;
			resbase = 0x1b0;
			break;
		case pipeline_stage_tess_eval:
			m_info.type = PIPE_SHADER_TESS_EVAL;
			resbase = 0x350;
			break;
		case pipeline_stage_geometry:
			m_info.type = PIPE_SHADER_GEOMETRY;
			resbase = 0x4f0;
			break;
		case pipeline_stage_fragment:
			m_info.type = PIPE_SHADER_FRAGMENT;
			resbase = 0x690;
			break;
		case pipeline_stage_compute:
			m_info.type = PIPE_SHADER_COMPUTE;
			resbase = 0x080;
			break;
	}
	m_info.target = 0x12b;
	m_info.bin.sourceRep = PIPE_SHADER_IR_TGSI;

	m_info.optLevel = optLevel;

	m_info.io.auxCBSlot      = 17;            // Driver constbuf c[0x0]. Note that codegen was modified to transform constbuf ids like such: final_id = (raw_id + 1) % 18
	m_info.io.drawInfoBase   = 0x000;         // This is used for gl_BaseVertex, gl_BaseInstance and gl_DrawID (in that order)
	m_info.io.bufInfoBase    = resbase+0x0a0; // This is used to load SSBO information (u64 iova / u32 size / u32 padding)
	m_info.io.texBindBase    = resbase+0x000; // Start of bound texture handles (32) + images (right after). 32-bit instead of 64-bit.
	m_info.io.fbtexBindBase  = 0x00c;         // This is used for implementing TGSI_OPCODE_FBFETCH, itself used for KHR/NV_blend_equation_advanced and EXT_shader_framebuffer_fetch.
	m_info.io.sampleInfoBase = 0x830;         // This is a LUT needed to implement gl_SamplePosition, it contains MSAA base sample positions.
	m_info.io.uboInfoBase    = 0x000;         // Similar to bufInfoBase, but for UBOs. Compute shaders need this because there aren't enough hardware constbufs.

	// The following fields are unused in our case, but are kept here for reference's sake:
	//m_info.io.genUserClip  = prog->vp.num_ucps;             // This is used for old-style clip plane handling (gl_ClipVertex).
	//m_info.io.msInfoCBSlot = 17;                            // This is used for msInfoBase (which is unused, see below)
	//m_info.io.ucpBase      = NVC0_CB_AUX_UCP_INFO;          // This is also for old-style clip plane handling.
	//m_info.io.msInfoBase   = NVC0_CB_AUX_MS_INFO;           // This points to a LUT used to calculate dx/dy from the sample id in NVC0LoweringPass::adjustCoordinatesMS. I replaced it with bitwise operations, so this is now unused.
	//m_info.io.suInfoBase   = NVC0_CB_AUX_SU_INFO(0);        // Surface information. On Maxwell, nouveau only uses it during NVC0LoweringPass::processSurfaceCoordsGM107 bound checking (which I disabled)
	//m_info.io.bindlessBase = NVC0_CB_AUX_BINDLESS_INFO(0);  // Like suInfoBase, but for bindless textures (pre-Kepler?).
	//m_info.prop.cp.gridInfoBase = NVC0_CB_AUX_GRID_INFO(0); // This is the work_dim parameter from clEnqueueNDRangeKernel (OpenCL).

	m_info.assignSlots = nvc0_program_assign_varying_slots;

	glsl_frontend_init();
}

DekoCompiler::~DekoCompiler()
{
	if (m_glsl)
		glsl_program_free(m_glsl);

	glsl_frontend_exit();
}

bool DekoCompiler::CompileGlsl(const char* glsl)
{
	m_glsl = glsl_program_create(glsl, m_stage);
	if (!m_glsl) return false;

	m_tgsi = glsl_program_get_tokens(m_glsl, m_tgsiNumTokens);
	m_info.bin.source = m_tgsi;
	m_info.bin.smemSize = glsl_program_compute_get_shared_size(m_glsl); // Total size of glsl shared variables. (translation process doesn't actually need this, but for the sake of consistency with nouveau, we keep this value here too)
	int ret = nv50_ir_generate_code(&m_info);
	if (ret < 0)
	{
		fprintf(stderr, "Error compiling program: %d\n", ret);
		return false;
	}

	uint32_t numInsns = m_info.bin.codeSize/8;
	uint64_t* insns = (uint64_t*)m_info.bin.code;
	uint32_t totalNumInsns = (numInsns + 8) &~ 7;

	bool emittedBRA = false;
	for (uint32_t i = numInsns; i < totalNumInsns; i ++)
	{
		uint64_t& schedInsn = insns[i &~ 3];
		uint32_t ipos = i & 3;
		if (ipos == 0)
		{
			schedInsn = 0;
			continue;
		}
		uint64_t insn = UINT64_C(0x50b0000000070f00); // NOP
		uint32_t sched = 0x7e0;
		if (!emittedBRA)
		{
			emittedBRA = true;
			insn = ipos==1 ? UINT64_C(0xe2400fffff07000f) : UINT64_C(0xe2400fffff87000f); // BRA $;
			sched = 0x7ff;
		}

		insns[i] = insn;
		schedInsn &= ~(((UINT64_C(1)<<21)-1) << (21*(ipos-1)));
		schedInsn |= uint64_t(sched) << (21*(ipos-1));
	}

	m_code = insns;
	m_codeSize = 8*totalNumInsns;
	return true;
}

void DekoCompiler::OutputDksh(const char* dkshFile)
{
	// temp
	OutputRawCode(dkshFile);
}

void DekoCompiler::OutputRawCode(const char* rawFile)
{
	FILE* f = fopen(rawFile, "wb");
	if (f)
	{
		fwrite(m_code, 1, m_codeSize, f);
		fclose(f);
	}
}

void DekoCompiler::OutputTgsi(const char* tgsiFile)
{
	FILE* f = fopen(tgsiFile, "w");
	if (f)
	{
		tgsi_dump_to_file(m_tgsi, TGSI_DUMP_FLOAT_AS_HEX, f);
		fclose(f);
	}
}
