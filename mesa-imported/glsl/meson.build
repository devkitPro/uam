
uam_files += custom_target(
	'glsl_parser',
	input : 'glsl_parser.yy',
	output : ['glsl_parser.cpp', 'glsl_parser.h'],
	command : [
		prog_bison, '-o', '@OUTPUT0@', '-p', '_mesa_glsl_', '--defines=@OUTPUT1@',
		'@INPUT@',
	],
)

uam_files += custom_target(
	'glsl_lexer_cpp',
	input : 'glsl_lexer.ll',
	output : 'glsl_lexer.cpp',
	command : [prog_flex, '-o', '@OUTPUT@', '@INPUT@'],
)

uam_files += custom_target(
	'ir_expression_operation.h',
	input : 'ir_expression_operation.py',
	output : 'ir_expression_operation.h',
	command : [prog_python, '@INPUT@', 'enum'],
	capture : true,
)

uam_files += custom_target(
	'ir_expression_operation_constant.h',
	input : 'ir_expression_operation.py',
	output : 'ir_expression_operation_constant.h',
	command : [prog_python, '@INPUT@', 'constant'],
	capture : true,
)

uam_files += custom_target(
	'ir_expression_operation_strings.h',
	input : 'ir_expression_operation.py',
	output : 'ir_expression_operation_strings.h',
	command : [prog_python, '@INPUT@', 'strings'],
	capture : true,
)

uam_files += files(
	'ast_array_index.cpp',
	'ast_expr.cpp',
	'ast_function.cpp',
	'ast_to_hir.cpp',
	'ast_type.cpp',
	'builtin_functions.cpp',
	'builtin_types.cpp',
	'builtin_variables.cpp',
	'generate_ir.cpp',
	'glsl_parser_extras.cpp',
	'glsl_symbol_table.cpp',
	'hir_field_selection.cpp',
	'ir.cpp',
	'ir_array_refcount.cpp',
	'ir_basic_block.cpp',
	'ir_builder.cpp',
	'ir_clone.cpp',
	'ir_constant_expression.cpp',
	'ir_equals.cpp',
	'ir_expression_flattening.cpp',
	'ir_function.cpp',
	'ir_function_can_inline.cpp',
	'ir_function_detect_recursion.cpp',
	'ir_hierarchical_visitor.cpp',
	'ir_hv_accept.cpp',
	'ir_print_visitor.cpp',
	'ir_reader.cpp',
	'ir_rvalue_visitor.cpp',
	'ir_set_program_inouts.cpp',
	'ir_validate.cpp',
	'ir_variable_refcount.cpp',
	'link_atomics.cpp',
	'link_functions.cpp',
	'link_interface_blocks.cpp',
	'link_uniform_block_active_visitor.cpp',
	'link_uniform_blocks.cpp',
	'link_uniform_initializers.cpp',
	'link_uniforms.cpp',
	'link_varyings.cpp',
	'linker.cpp',
	'linker_util.cpp',
	'loop_analysis.cpp',
	'loop_unroll.cpp',
	'lower_blend_equation_advanced.cpp',
	'lower_buffer_access.cpp',
	'lower_const_arrays_to_uniforms.cpp',
	'lower_cs_derived.cpp',
	'lower_discard.cpp',
	'lower_discard_flow.cpp',
	'lower_distance.cpp',
	'lower_if_to_cond_assign.cpp',
	'lower_instructions.cpp',
	'lower_int64.cpp',
	'lower_jumps.cpp',
	'lower_mat_op_to_vec.cpp',
	'lower_named_interface_blocks.cpp',
	'lower_noise.cpp',
	'lower_offset_array.cpp',
	'lower_output_reads.cpp',
	'lower_packed_varyings.cpp',
	'lower_packing_builtins.cpp',
	'lower_shared_reference.cpp',
	'lower_subroutine.cpp',
	'lower_tess_level.cpp',
	'lower_texture_projection.cpp',
	'lower_ubo_reference.cpp',
	'lower_variable_index_to_cond_assign.cpp',
	'lower_vec_index_to_cond_assign.cpp',
	'lower_vec_index_to_swizzle.cpp',
	'lower_vector.cpp',
	'lower_vector_derefs.cpp',
	'lower_vector_insert.cpp',
	'lower_vertex_id.cpp',
	'opt_algebraic.cpp',
	'opt_array_splitting.cpp',
	'opt_conditional_discard.cpp',
	'opt_constant_folding.cpp',
	'opt_constant_propagation.cpp',
	'opt_constant_variable.cpp',
	'opt_copy_propagation_elements.cpp',
	'opt_dead_builtin_variables.cpp',
	'opt_dead_builtin_varyings.cpp',
	'opt_dead_code.cpp',
	'opt_dead_code_local.cpp',
	'opt_dead_functions.cpp',
	'opt_flatten_nested_if_blocks.cpp',
	'opt_flip_matrices.cpp',
	'opt_function_inlining.cpp',
	'opt_if_simplification.cpp',
	'opt_minmax.cpp',
	'opt_rebalance_tree.cpp',
	'opt_redundant_jumps.cpp',
	'opt_structure_splitting.cpp',
	'opt_swizzle.cpp',
	'opt_tree_grafting.cpp',
	'opt_vectorize.cpp',
	'propagate_invariance.cpp',
	's_expression.cpp',
	'serialize.cpp',
	'standalone_scaffolding.cpp',
	'string_to_uint_map.cpp',
)

subdir('glcpp')
