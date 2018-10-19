#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_shader_tokens.h"

extern "C"
{
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_emulate.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_ureg.h"
#include "tgsi/tgsi_from_mesa.h"
//#include "tgsi/tgsi_dump.h"
}

#include "state_tracker/st_glsl_to_tgsi.h"

#include "glsl_frontend.h"

#define ST_DOUBLE_ATTRIB_PLACEHOLDER 0xff

// Defined in glsl_frontend.cpp
struct glsl_to_tgsi_visitor*
_glsl_program_get_tgsi_visitor(struct gl_program *prog);

// Defined in glsl_frontend.cpp
void
_glsl_program_attach_tgsi_tokens(struct gl_program *prog, const tgsi_token *tokens, unsigned int num);

// Based off st_translate_vertex_program
bool tgsi_translate_vertex(struct gl_context *ctx, struct gl_program *prog)
{
	unsigned num_outputs = 0;
	unsigned attr;
	ubyte output_semantic_name[VARYING_SLOT_MAX] = {0};
	ubyte output_semantic_index[VARYING_SLOT_MAX] = {0};

	// maps a TGSI input index back to a Mesa VERT_ATTRIB_x
	ubyte index_to_input[PIPE_MAX_ATTRIBS];
	ubyte num_inputs = 0;
	// Reverse mapping of the above
	ubyte input_to_index[VERT_ATTRIB_MAX];

	// Maps VARYING_SLOT_x to slot
	ubyte result_to_output[VARYING_SLOT_MAX];

	memset(input_to_index, ~0, sizeof(input_to_index));

	// Determine number of inputs, the mappings between VERT_ATTRIB_x
	// and TGSI generic input indexes, plus input attrib semantic info.
	for (attr = 0; attr < VERT_ATTRIB_MAX; attr++) {
		if ((prog->info.inputs_read & BITFIELD64_BIT(attr)) != 0) {
			input_to_index[attr] = num_inputs;
			index_to_input[num_inputs] = attr;
			num_inputs++;
			if ((prog->DualSlotInputs & BITFIELD64_BIT(attr)) != 0) {
				// add placeholder for second part of a double attribute
				index_to_input[num_inputs] = ST_DOUBLE_ATTRIB_PLACEHOLDER;
				num_inputs++;
			}
		}
	}
	// bit of a hack, presetup potentially unused edgeflag input
	input_to_index[VERT_ATTRIB_EDGEFLAG] = num_inputs;
	index_to_input[num_inputs] = VERT_ATTRIB_EDGEFLAG;

	// Compute mapping of vertex program outputs to slots.
	for (attr = 0; attr < VARYING_SLOT_MAX; attr++) {
		if ((prog->info.outputs_written & BITFIELD64_BIT(attr)) == 0) {
			result_to_output[attr] = ~0;
		}
		else {
			unsigned slot = num_outputs++;

			result_to_output[attr] = slot;

			unsigned semantic_name, semantic_index;
			tgsi_get_gl_varying_semantic(gl_varying_slot(attr), true, &semantic_name, &semantic_index);
			output_semantic_name[slot] = semantic_name;
			output_semantic_index[slot] = semantic_index;
		}
	}
	// similar hack to above, presetup potentially unused edgeflag output
	result_to_output[VARYING_SLOT_EDGE] = num_outputs;
	output_semantic_name[num_outputs] = TGSI_SEMANTIC_EDGEFLAG;
	output_semantic_index[num_outputs] = 0;

	struct ureg_program *ureg = ureg_create(PIPE_SHADER_VERTEX);
	if (!ureg)
		return false;

	if (prog->info.clip_distance_array_size)
		ureg_property(ureg, TGSI_PROPERTY_NUM_CLIPDIST_ENABLED, prog->info.clip_distance_array_size);
	if (prog->info.cull_distance_array_size)
		ureg_property(ureg, TGSI_PROPERTY_NUM_CULLDIST_ENABLED, prog->info.cull_distance_array_size);

	enum pipe_error error = st_translate_program(ctx,
		PIPE_SHADER_VERTEX,
		ureg,
		_glsl_program_get_tgsi_visitor(prog),
		prog,
		num_inputs, input_to_index, NULL, NULL, NULL, NULL,
		num_outputs, result_to_output, output_semantic_name, output_semantic_index);

	if (error != PIPE_OK)
	{
		ureg_destroy(ureg);
		return NULL;
	}

	// We get back the tgsi!!
	unsigned int num_tokens = 0;
	const struct tgsi_token *tokens = ureg_get_tokens(ureg, &num_tokens);
	ureg_destroy(ureg);

	//tgsi_dump_to_file(tokens, TGSI_DUMP_FLOAT_AS_HEX, stdout);
	_glsl_program_attach_tgsi_tokens(prog, tokens, num_tokens);

	return true;
}
