#ifndef PICORUBY_TI_BUILTIN_H
#define PICORUBY_TI_BUILTIN_H

#include "picoruby_ti_builtin_database.h"
#include <stddef.h>
#include <stdint.h>

uint8_t ti_get_builtin_class_id(const uint8_t *name, size_t length);
/* Type declared for an instance variable in the RBS signatures ("@gfx" form,
   leading '@' included). TI_CLASS_NONE when the class declares none. */
uint8_t ti_get_builtin_instance_variable_class(
  uint8_t class_id,
  const uint8_t *instance_variable_name,
  size_t instance_variable_name_length
);
/* Class constants declared in the RBS signatures ("BLACK: Integer"). */
const TiBuiltinConstant *ti_get_builtin_constant(
  uint8_t class_id,
  const uint8_t *constant_name,
  size_t constant_name_length
);
int ti_collect_builtin_constants_matching_prefix(
  uint8_t class_id,
  const uint8_t *prefix,
  size_t prefix_length,
  const TiBuiltinConstant **output_constants,
  int output_capacity
);
const char *ti_get_builtin_constant_name(const TiBuiltinConstant *builtin_constant);
const char *ti_get_builtin_constant_signature(const TiBuiltinConstant *builtin_constant);
const char *ti_get_builtin_constant_document(const TiBuiltinConstant *builtin_constant);
const TiBuiltinMethod *ti_get_builtin_instance_method(
  uint8_t class_id,
  const uint8_t *name,
  size_t length
);
const TiBuiltinMethod *ti_get_builtin_static_method(
  uint8_t class_id,
  const uint8_t *name,
  size_t length
);
const TiBuiltinArgument *
ti_get_builtin_argument(const TiBuiltinMethod *method, int argument_index);
const char *
ti_get_builtin_argument_name(const TiBuiltinArgument *argument);
int ti_get_builtin_argument_classes(
  const TiBuiltinArgument *argument,
  uint8_t out_class_ids[4]
);
int ti_collect_builtin_methods_matching_partial_method_name(
  uint8_t class_id,
  int use_static_methods,
  const uint8_t *partial_method_name,
  size_t partial_method_name_length,
  const TiBuiltinMethod **out,
  int out_capacity
);
const char *ti_get_builtin_method_name(const TiBuiltinMethod *method);
const char *ti_get_builtin_signature(const TiBuiltinMethod *method);
const char *ti_get_builtin_document(const TiBuiltinMethod *method);
const char *ti_get_builtin_class_name(uint8_t class_id);
int ti_get_builtin_return_classes(
  const TiBuiltinMethod *method,
  uint8_t out_class_ids[4]
);
int ti_get_builtin_return_array_variant_classes(
  const TiBuiltinMethod *method,
  uint8_t out_class_ids[4]
);

#endif
