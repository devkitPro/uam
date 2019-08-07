#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

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

static int usage(const char* prog)
{
	fprintf(stderr,
		"Usage: %s [options] file\n"
		"Options:\n"
		"  -o, --out=<file>   Specifies the output file to generate\n"
		"  -t, --tgsi=<file>  Specifies the file to which output intermediary TGSI code\n"
		"  -s, --stage=<name> Specifies the pipeline stage of the shader\n"
		"                     (vert, tess_ctrl, tess_eval, geom, frag, comp)\n"
		"  -v, --version      Displays version information\n"
		, prog);
	return EXIT_FAILURE;
}


int main(int argc, char* argv[])
{
	const char *inFile = nullptr, *outFile = nullptr, *tgsiFile = nullptr, *stageName = nullptr;

	static struct option long_options[] =
	{
		{ "out",     required_argument, NULL, 'o' },
		{ "stage",   required_argument, NULL, 's' },
		{ "help",    no_argument,       NULL, '?' },
		{ "version", no_argument,       NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};

	int opt, optidx = 0;
	while ((opt = getopt_long(argc, argv, "o:t:s:?v", long_options, &optidx)) != -1)
	{
		switch (opt)
		{
			case 'o': outFile = optarg; break;
			case 't': tgsiFile = optarg; break;
			case 's': stageName = optarg; break;
			case '?': usage(argv[0]); return EXIT_SUCCESS;
			case 'v': printf("%s - Built on %s %s\n", PACKAGE_STRING, __DATE__, __TIME__); return EXIT_SUCCESS;
			default:  return usage(argv[0]);
		}
	}

	if ((argc-optind) != 1)
		return usage(argv[0]);
	inFile = argv[optind];

	if (!stageName)
	{
		fprintf(stderr, "Missing pipeline stage argument (--stage)\n");
		return EXIT_FAILURE;
	}

	if (!outFile)
	{
		fprintf(stderr, "Missing output file argument (--out)\n");
		return EXIT_FAILURE;
	}

	pipeline_stage stage;
	if (0) ((void)0);
#define TEST_STAGE(_str,_val) else if (strcmp(stageName,(_str))==0) stage = (_val)
	TEST_STAGE("vert", pipeline_stage_vertex);
	TEST_STAGE("tess_ctrl", pipeline_stage_tess_ctrl);
	TEST_STAGE("tess_eval", pipeline_stage_tess_eval);
	TEST_STAGE("geom", pipeline_stage_geometry);
	TEST_STAGE("frag", pipeline_stage_fragment);
	TEST_STAGE("comp", pipeline_stage_compute);
#undef TEST_STAGE
	else
	{
		fprintf(stderr, "Unrecognized pipeline stage: `%s'\n", stageName);
		return EXIT_FAILURE;
	}

	FILE* fin = fopen(inFile, "rb");
	if (!fin)
	{
		fprintf(stderr, "Could not open input file: %s\n", inFile);
		return EXIT_FAILURE;
	}

	fseek(fin, 0, SEEK_END);
	long fsize = ftell(fin);
	rewind(fin);

	char* glsl_source = new char[fsize+1];
	fread(glsl_source, 1, fsize, fin);
	fclose(fin);
	glsl_source[fsize] = 0;

	glsl_frontend_init();

	glsl_program prg = glsl_program_create(glsl_source, stage);
	delete[] glsl_source;
	if (!prg)
	{
		glsl_frontend_exit();
		return EXIT_FAILURE;
	}

	unsigned int num_tokens;
	const struct tgsi_token* tokens = glsl_program_get_tokens(prg, num_tokens);
	if (tgsiFile)
	{
		FILE *ft = fopen(tgsiFile, "w");
		if (ft)
		{
			tgsi_dump_to_file(tokens, TGSI_DUMP_FLOAT_AS_HEX, ft);
			fclose(ft);
		}
	}

	struct nv50_ir_prog_info info = {0};
	uint16_t resbase;
	switch (stage)
	{
		default:
		case pipeline_stage_vertex:
			info.type = PIPE_SHADER_VERTEX;
			resbase = 0x010;
			break;
		case pipeline_stage_tess_ctrl:
			info.type = PIPE_SHADER_TESS_CTRL;
			resbase = 0x1b0;
			break;
		case pipeline_stage_tess_eval:
			info.type = PIPE_SHADER_TESS_EVAL;
			resbase = 0x350;
			break;
		case pipeline_stage_geometry:
			info.type = PIPE_SHADER_GEOMETRY;
			resbase = 0x4f0;
			break;
		case pipeline_stage_fragment:
			info.type = PIPE_SHADER_FRAGMENT;
			resbase = 0x690;
			break;
		case pipeline_stage_compute:
			info.type = PIPE_SHADER_COMPUTE;
			resbase = 0x080;
			break;
	}
	info.target = 0x12b;
	info.bin.sourceRep = PIPE_SHADER_IR_TGSI;
	info.bin.source = tokens;

	info.optLevel = 3;

	info.bin.smemSize      = glsl_program_compute_get_shared_size(prg); // Total size of glsl shared variables. (translation process doesn't actually need this, but for the sake of consistency with nouveau, we keep this value here too)
	info.io.auxCBSlot      = 17;            // Driver constbuf c[0x0]. Note that codegen was modified to transform constbuf ids like such: final_id = (raw_id + 1) % 18
	info.io.drawInfoBase   = 0x000;         // This is used for gl_BaseVertex, gl_BaseInstance and gl_DrawID (in that order)
	info.io.bufInfoBase    = resbase+0x0a0; // This is used to load SSBO information (u64 iova / u32 size / u32 padding)
	info.io.texBindBase    = resbase+0x000; // Start of bound texture handles (32) + images (right after). 32-bit instead of 64-bit.
	info.io.fbtexBindBase  = 0x00c;         // This is used for implementing TGSI_OPCODE_FBFETCH, itself used for KHR/NV_blend_equation_advanced and EXT_shader_framebuffer_fetch.
	info.io.sampleInfoBase = 0x830;         // This is a LUT needed to implement gl_SamplePosition, it contains MSAA base sample positions.
	info.io.uboInfoBase    = 0x000;         // Similar to bufInfoBase, but for UBOs. Compute shaders need this because there aren't enough hardware constbufs.

	// The following fields are unused in our case, but are kept here for reference's sake:
	//info.io.genUserClip  = prog->vp.num_ucps;             // This is used for old-style clip plane handling (gl_ClipVertex).
	//info.io.msInfoCBSlot = 17;                            // This is used for msInfoBase (which is unused, see below)
	//info.io.ucpBase      = NVC0_CB_AUX_UCP_INFO;          // This is also for old-style clip plane handling.
	//info.io.msInfoBase   = NVC0_CB_AUX_MS_INFO;           // This points to a LUT used to calculate dx/dy from the sample id in NVC0LoweringPass::adjustCoordinatesMS. I replaced it with bitwise operations, so this is now unused.
	//info.io.suInfoBase   = NVC0_CB_AUX_SU_INFO(0);        // Surface information. On Maxwell, nouveau only uses it during NVC0LoweringPass::processSurfaceCoordsGM107 bound checking (which I disabled)
	//info.io.bindlessBase = NVC0_CB_AUX_BINDLESS_INFO(0);  // Like suInfoBase, but for bindless textures (pre-Kepler?).
	//info.prop.cp.gridInfoBase = NVC0_CB_AUX_GRID_INFO(0); // This is the work_dim parameter from clEnqueueNDRangeKernel (OpenCL).

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

	FILE* f = fopen(outFile, "wb");
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
