#include "picoruby_ti_eval_handlers.h"
#include "picoruby_ti_builtin.h"
#include "picoruby_ti_define_info.h"
#include "picoruby_ti_t.h"
#include "picoruby_ti_t_frame.h"

uint16_t
ti_handle_identifier(TiContext *context, pm_constant_id_t constant_id) {
  uint16_t name_id;

  if (!ti_convert_constant_id(context, constant_id, &name_id))
    return 0;

  uint16_t value_t_node_index = ti_get_value_t(name_id);

  if (value_t_node_index != 0)
    return value_t_node_index;

  /* Instance variables assigned outside the analyzed sources can still be
     declared in the RBS signatures ("@gfx: Canvas"). Walk the enclosing
     class chain to the first database class and ask it. Assignments seen in
     the sources win (checked above). */
  const pm_constant_t *constant = ti_get_constant(context, constant_id);

  if (
    !constant ||
    constant->length < 2 ||
    constant->start[0] != '@' ||
    constant->start[1] == '@'
  ) {

    return 0;
  }

  uint8_t lookup_class_id = context->current_class_id;

  for (int chain_depth = 0;
       chain_depth < TI_SUPERCLASS_CHAIN_LIMIT &&
       lookup_class_id != TI_CLASS_NONE;
       chain_depth++) {

    if (lookup_class_id < TI_CLASS_USER_BASE) {
      uint8_t declared_class_id =
        ti_get_builtin_instance_variable_class(
          lookup_class_id,
          constant->start,
          constant->length
        );

      if (declared_class_id != TI_CLASS_NONE)
        return ti_new_t(declared_class_id, 0, 0);

      return 0;
    }

    lookup_class_id = ti_resolve_superclass_id(lookup_class_id);
  }

  return 0;
}

uint16_t
ti_handle_constant_path(
  TiContext *context,
  const pm_constant_path_node_t *constant_path
) {

  /* Only the plain "Klass::CONST" shape where Klass is a database class;
     deeper paths and user-class constants stay untyped. */
  if (
    !constant_path->parent ||
    PM_NODE_TYPE(constant_path->parent) != PM_CONSTANT_READ_NODE ||
    constant_path->name == 0
  ) {

    return 0;
  }

  const pm_constant_t *parent_constant =
    ti_get_constant(
      context,
      ((const pm_constant_read_node_t *)constant_path->parent)->name
    );

  if (!parent_constant)
    return 0;

  uint8_t class_id =
    ti_get_builtin_class_id(parent_constant->start, parent_constant->length);

  if (class_id == TI_CLASS_NONE)
    return 0;

  const pm_constant_t *constant_name =
    ti_get_constant(context, constant_path->name);

  if (!constant_name)
    return 0;

  const TiBuiltinConstant *builtin_constant =
    ti_get_builtin_constant(
      class_id,
      constant_name->start,
      constant_name->length
    );

  if (!builtin_constant || builtin_constant->class_identifier == 0)
    return 0;

  return ti_new_t(builtin_constant->class_identifier, 0, 0);
}

uint16_t
ti_handle_const_evaluation(
  TiContext *context,
  const pm_constant_read_node_t *constant_read
) {

  const pm_constant_t *constant = ti_get_constant(context, constant_read->name);

  if (constant) {
    uint8_t class_id =
      ti_get_builtin_class_id(constant->start, constant->length);

    if (class_id != TI_CLASS_NONE) {
      return ti_new_t(class_id, TI_T_FLAG_STATIC, 0);
    }
  }

  uint16_t name_id;
  if (!ti_convert_constant_id(context, constant_read->name, &name_id))
    return 0;

  uint8_t user_class_id = ti_get_defined_class_id(name_id);
  if (user_class_id != TI_CLASS_NONE) {
    return ti_new_t(
      user_class_id,
      TI_T_FLAG_DEFINED_CLASS | TI_T_FLAG_STATIC,
      0
    );
  }

  return ti_get_value_t(name_id);
}
