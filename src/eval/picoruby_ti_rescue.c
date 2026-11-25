#include "picoruby_ti_rescue.h"
#include "picoruby_ti_builtin.h"
#include "picoruby_ti_eval.h"
#include "picoruby_ti_eval_handlers.h"
#include "picoruby_ti_t.h"
#include "picoruby_ti_t_frame.h"

/* One rescue clause names at most this many classes before the variable is
   left untyped; a union cannot hold more anyway (TI_UNION_CAPACITY). */
#define TI_RESCUE_CLASS_LIMIT 8

/* A chain of "rescue" clauses longer than this is not followed further. */
#define TI_RESCUE_CHAIN_LIMIT 32

static uint16_t
rescue_reference_t(TiContext *context, const pm_rescue_node_t *rescue_node) {
  size_t count = rescue_node->exceptions.size;

  if (count == 0) {
    static const char standard_error[] = "StandardError";
    uint8_t class_id = ti_get_builtin_class_id(
      (const uint8_t *)standard_error,
      sizeof(standard_error) - 1
    );

    return ti_new_t(class_id, 0, 0);
  }

  if (count > TI_RESCUE_CLASS_LIMIT)
    return 0;

  uint16_t result_t_node_index = 0;

  for (size_t index = 0; index < count; index++) {
    uint8_t class_id = ti_resolve_constant_class_id(
      context,
      rescue_node->exceptions.nodes[index]
    );

    /* One unknown class (the user's, a splat) makes the whole list unknown:
       typing the variable as the others would flag the unknown one's
       methods. */
    if (class_id == TI_CLASS_NONE)
      return 0;

    uint16_t t_node_index = ti_new_t(class_id, 0, 0);

    if (t_node_index == 0)
      return 0;

    result_t_node_index = ti_make_union(result_t_node_index, t_node_index);
  }

  return result_t_node_index;
}

void
ti_bind_rescue_reference(
  TiContext *context,
  const pm_rescue_node_t *rescue_node
) {

  if (!rescue_node || !rescue_node->reference || context->failed)
    return;

  pm_constant_id_t constant_id;

  switch (PM_NODE_TYPE(rescue_node->reference)) {
  case PM_LOCAL_VARIABLE_TARGET_NODE:
    constant_id =
      ((const pm_local_variable_target_node_t *)rescue_node->reference)->name;
    break;

  case PM_INSTANCE_VARIABLE_TARGET_NODE:
    constant_id =
      ((const pm_instance_variable_target_node_t *)rescue_node->reference)
        ->name;
    break;

  case PM_GLOBAL_VARIABLE_TARGET_NODE:
    constant_id =
      ((const pm_global_variable_target_node_t *)rescue_node->reference)->name;
    break;

  default:
    return;
  }

  uint16_t t_node_index = rescue_reference_t(context, rescue_node);

  if (t_node_index == 0)
    return;

  uint16_t name_id;

  if (!ti_convert_constant_id(context, constant_id, &name_id)) {
    context->failed = 1;
    return;
  }

  if (!ti_set_value_t(name_id, t_node_index))
    context->failed = 1;
}

uint16_t
ti_eval_begin(TiContext *context, const pm_begin_node_t *begin_node, int depth) {
  if (!begin_node || depth > TI_EVAL_DEPTH_LIMIT || context->failed)
    return 0;

  uint16_t result_t_node_index =
    ti_eval_statements(context, begin_node->statements, depth + 1);

  /* With an else, the value when nothing was raised is the else's. */
  if (begin_node->else_clause) {
    result_t_node_index = ti_eval_statements(
      context,
      begin_node->else_clause->statements,
      depth + 1
    );
  }

  const pm_rescue_node_t *rescue_node = begin_node->rescue_clause;

  for (int chain = 0; rescue_node && chain < TI_RESCUE_CHAIN_LIMIT; chain++) {
    ti_bind_rescue_reference(context, rescue_node);

    uint16_t rescue_t_node_index =
      ti_eval_statements(context, rescue_node->statements, depth + 1);

    if (rescue_t_node_index == 0 && !rescue_node->statements)
      rescue_t_node_index = ti_new_t(TI_CLASS_NIL, 0, 0);

    result_t_node_index =
      ti_make_union(result_t_node_index, rescue_t_node_index);

    rescue_node = rescue_node->subsequent;
  }

  /* The ensure clause runs for its effect; its value is not the result. */
  if (begin_node->ensure_clause) {
    ti_eval_statements(
      context,
      begin_node->ensure_clause->statements,
      depth + 1
    );
  }

  return result_t_node_index;
}
