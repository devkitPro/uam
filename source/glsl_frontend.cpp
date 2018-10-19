#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glsl/ast.h"
#include "glsl/glsl_parser_extras.h"
#include "glsl/ir_optimization.h"
#include "glsl/program.h"
#include "glsl/loop_analysis.h"
#include "glsl/standalone_scaffolding.h"
#include "glsl/string_to_uint_map.h"
#include "util/set.h"
#include "glsl/linker.h"
#include "glsl/glsl_parser_extras.h"
#include "glsl/builtin_functions.h"
#include "glsl/opt_add_neg_to_sub.h"
#include "main/mtypes.h"
#include "program/program.h"

class dead_variable_visitor : public ir_hierarchical_visitor {
public:
	dead_variable_visitor()
	{
		variables = _mesa_set_create(NULL,
									_mesa_hash_pointer,
									_mesa_key_pointer_equal);
	}

	virtual ~dead_variable_visitor()
	{
		_mesa_set_destroy(variables, NULL);
	}

	virtual ir_visitor_status visit(ir_variable *ir)
	{
		/* If the variable is auto or temp, add it to the set of variables that
		* are candidates for removal.
		*/
		if (ir->data.mode != ir_var_auto && ir->data.mode != ir_var_temporary)
			return visit_continue;

		_mesa_set_add(variables, ir);

		return visit_continue;
	}

	virtual ir_visitor_status visit(ir_dereference_variable *ir)
	{
		struct set_entry *entry = _mesa_set_search(variables, ir->var);

		/* If a variable is dereferenced at all, remove it from the set of
		* variables that are candidates for removal.
		*/
		if (entry != NULL)
			_mesa_set_remove(variables, entry);

		return visit_continue;
	}

	void remove_dead_variables()
	{
		struct set_entry *entry;

		set_foreach(variables, entry) {
			ir_variable *ir = (ir_variable *) entry->key;

			assert(ir->ir_type == ir_type_variable);
			ir->remove();
		}
	}

private:
	set *variables;
};

static void
init_gl_program(struct gl_program *prog, bool is_arb_asm)
{
	prog->RefCount = 1;
	prog->Format = GL_PROGRAM_FORMAT_ASCII_ARB;
	prog->is_arb_asm = is_arb_asm;
}

static struct gl_program *
new_program(UNUSED struct gl_context *ctx, GLenum target,
            UNUSED GLuint id, bool is_arb_asm)
{
	switch (target) {
	case GL_VERTEX_PROGRAM_ARB: /* == GL_VERTEX_PROGRAM_NV */
	case GL_GEOMETRY_PROGRAM_NV:
	case GL_TESS_CONTROL_PROGRAM_NV:
	case GL_TESS_EVALUATION_PROGRAM_NV:
	case GL_FRAGMENT_PROGRAM_ARB:
	case GL_COMPUTE_PROGRAM_NV: {
		struct gl_program *prog = rzalloc(NULL, struct gl_program);
		init_gl_program(prog, is_arb_asm);
		return prog;
	}
	default:
		printf("bad target in new_program\n");
		return NULL;
	}
}

static void
initialize_context(struct gl_context *ctx, gl_api api)
{
	initialize_context_to_defaults(ctx, api);

	/* The standalone compiler needs to claim support for almost
		* everything in order to compile the built-in functions.
		*/
	ctx->Const.GLSLVersion = 450;
	ctx->Extensions.ARB_ES3_compatibility = true;
	ctx->Const.MaxComputeWorkGroupCount[0] = 65535;
	ctx->Const.MaxComputeWorkGroupCount[1] = 65535;
	ctx->Const.MaxComputeWorkGroupCount[2] = 65535;
	ctx->Const.MaxComputeWorkGroupSize[0] = 1024;
	ctx->Const.MaxComputeWorkGroupSize[1] = 1024;
	ctx->Const.MaxComputeWorkGroupSize[2] = 64;
	ctx->Const.MaxComputeWorkGroupInvocations = 1024;
	ctx->Const.MaxComputeSharedMemorySize = 32768;
	ctx->Const.MaxComputeVariableGroupSize[0] = 512;
	ctx->Const.MaxComputeVariableGroupSize[1] = 512;
	ctx->Const.MaxComputeVariableGroupSize[2] = 64;
	ctx->Const.MaxComputeVariableGroupInvocations = 512;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxTextureImageUnits = 16;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxCombinedUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxInputComponents = 0; /* not used */
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxOutputComponents = 0; /* not used */
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxAtomicBuffers = 8;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxAtomicCounters = 8;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxImageUniforms = 8;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxUniformBlocks = 12;

	ctx->Const.MaxClipPlanes = 8;
	ctx->Const.MaxDrawBuffers = 8;
	ctx->Const.MinProgramTexelOffset = -8;
	ctx->Const.MaxProgramTexelOffset = 7;
	ctx->Const.MaxLights = 8;
	ctx->Const.MaxTextureCoordUnits = 8;
	ctx->Const.MaxTextureUnits = 2;
	ctx->Const.MaxUniformBufferBindings = 84;
	ctx->Const.MaxVertexStreams = 4;
	ctx->Const.MaxTransformFeedbackBuffers = 4;

	ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = 16;
	ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits = 16;
	ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_VERTEX].MaxCombinedUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_VERTEX].MaxInputComponents = 0; /* not used */
	ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents = 64;

	ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxTextureImageUnits = 16;
	ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxCombinedUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxInputComponents =
		ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents;
	ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxOutputComponents = 128;

	ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits = 16;
	ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxCombinedUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents =
		ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxOutputComponents;
	ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxOutputComponents = 0; /* not used */

	ctx->Const.MaxCombinedTextureImageUnits =
		ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits
		+ ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxTextureImageUnits
		+ ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits;

	ctx->Const.MaxGeometryOutputVertices = 256;
	ctx->Const.MaxGeometryTotalOutputComponents = 1024;

	ctx->Const.MaxVarying = 60 / 4;

	ctx->Const.GenerateTemporaryNames = true;
	ctx->Const.MaxPatchVertices = 32;

	/* GL_ARB_explicit_uniform_location, GL_MAX_UNIFORM_LOCATIONS */
	ctx->Const.MaxUserAssignableUniformLocations =
		4 * MESA_SHADER_STAGES * MAX_UNIFORMS;

	ctx->Driver.NewProgram = new_program;
}

void another_test(const char* glsl_source)
{
	static struct gl_context gl_ctx;
	initialize_context(&gl_ctx, API_OPENGL_CORE);

	struct gl_shader_program *prg;

	prg = rzalloc (NULL, struct gl_shader_program);
	assert(prg != NULL);
	prg->data = rzalloc(prg, struct gl_shader_program_data);
	assert(prg->data != NULL);
	prg->data->InfoLog = ralloc_strdup(prg->data, "");
	prg->SeparateShader = true;

	/* Created just to avoid segmentation faults */
	prg->AttributeBindings = new string_to_uint_map;
	prg->FragDataBindings = new string_to_uint_map;
	prg->FragDataIndexBindings = new string_to_uint_map;

	// Allocate a shader list
	prg->Shaders = reralloc(prg, prg->Shaders, struct gl_shader *, 1);

	// Allocate a shader and add it to the list
	struct gl_shader *shader = rzalloc(prg, gl_shader);
	prg->Shaders[prg->NumShaders] = shader;
	prg->NumShaders++;

	// TODO: this is hardcoded
	shader->Type = GL_VERTEX_SHADER; //GL_FRAGMENT_SHADER;
	shader->Stage = _mesa_shader_enum_to_shader_stage(shader->Type);
	shader->Source = glsl_source;

	// "Compile" the shader
	_mesa_glsl_compile_shader(&gl_ctx, shader, false, false, true);
	if (shader->CompileStatus)
	{
		printf("Shader successfully compiled\n");
	} else
	{
		printf("Shader failed to compile.\n");
		if (shader->InfoLog && shader->InfoLog[0] != 0)
			fprintf(stderr, "%s\n", shader->InfoLog);
		return;
	}
	_mesa_clear_shader_program_data(&gl_ctx, prg);

	// Link the shader
	link_shaders(&gl_ctx, prg);
	if (prg->data->LinkStatus != LINKING_SUCCESS)
	{
		fprintf(stderr, "Shader failed to link.\n");
		fprintf(stderr, "%s\n", prg->data->InfoLog);
	}
	struct gl_linked_shader *linked_shader = prg->_LinkedShaders[shader->Stage];

	// Do more optimizations
	{
		add_neg_to_sub_visitor v;
		visit_list_elements(&v, linked_shader->ir);

		dead_variable_visitor dv;
		visit_list_elements(&dv, linked_shader->ir);
		dv.remove_dead_variables();
	}

	_mesa_print_ir(stdout, linked_shader->ir, NULL);
}
