#include "picoruby_ti_eval_handlers.h"
#include "picoruby_ti_builtin.h"
#include "picoruby_ti_define_info.h"
#include "picoruby_ti_t.h"
#include "picoruby_ti_t_frame.h"
#include <string.h>

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

/* A constant path names at most this many segments ("A::B::C" is three).
   Longer paths stay untyped; no database class is that deep. */
#define TI_CONSTANT_PATH_DEPTH_LIMIT 8

/* Does the constant path spelled by node ("A::B::C", "::A::B", or a bare
   "A") equal the database class name "A::B::C"? Walks the path from the leaf
   to the root, matching each segment against the tail of the name, so no
   buffer is needed. A leading "::" matches like the unqualified path. */
static int
constant_path_matches_name(
  TiContext *context,
  const pm_node_t *node,
  const char *name,
  size_t name_length
) {

  size_t end = name_length;

  for (int depth = 0; depth < TI_CONSTANT_PATH_DEPTH_LIMIT; depth++) {
    pm_constant_id_t segment_id;
    const pm_node_t *parent = NULL;

    if (PM_NODE_TYPE(node) == PM_CONSTANT_READ_NODE) {
      segment_id = ((const pm_constant_read_node_t *)node)->name;
    } else if (PM_NODE_TYPE(node) == PM_CONSTANT_PATH_NODE) {
      const pm_constant_path_node_t *path =
        (const pm_constant_path_node_t *)node;

      segment_id = path->name;
      parent = path->parent;
    } else {
      return 0;
    }

    if (segment_id == 0)
      return 0;

    const pm_constant_t *segment = ti_get_constant(context, segment_id);

    if (
      !segment ||
      segment->length == 0 ||
      segment->length > end ||
      memcmp(name + end - segment->length, segment->start, segment->length) != 0
    ) {

      return 0;
    }

    end -= segment->length;

    /* The root: a constant read, or a path with no parent ("::A"). */
    if (!parent)
      return end == 0;

    if (end < 2 || name[end - 1] != ':' || name[end - 2] != ':')
      return 0;

    end -= 2;
    node = parent;
  }

  return 0;
}

/* The database class the constant path (or a bare constant) names, or
   TI_CLASS_NONE: for anything else too (a splat, an expression). */
uint8_t
ti_resolve_constant_class_id(TiContext *context, const pm_node_t *node) {
  if (
    !node ||
    (PM_NODE_TYPE(node) != PM_CONSTANT_READ_NODE &&
     PM_NODE_TYPE(node) != PM_CONSTANT_PATH_NODE)
  ) {

    return TI_CLASS_NONE;
  }

  for (uint8_t class_id = 1; class_id < ti_builtin_class_count; class_id++) {
    const char *class_name = ti_get_builtin_class_name(class_id);

    if (
      class_name &&
      constant_path_matches_name(context, node, class_name, strlen(class_name))
    ) {

      return class_id;
    }
  }

  return TI_CLASS_NONE;
}

uint16_t
ti_handle_constant_path(
  TiContext *context,
  const pm_constant_path_node_t *constant_path
) {

  if (constant_path->name == 0)
    return 0;

  /* "A::B" (or "A::B::C", "::A::B") naming a database class is that class,
     static -- the same as reading a top-level "Foo". */
  uint8_t path_class_id =
    ti_resolve_constant_class_id(context, (const pm_node_t *)constant_path);

  if (path_class_id != TI_CLASS_NONE)
    return ti_new_t(path_class_id, TI_T_FLAG_STATIC, 0);

  /* Otherwise "Klass::CONST" where Klass is a database class (itself possibly
     nested). User-class constants stay untyped. */
  uint8_t class_id =
    ti_resolve_constant_class_id(context, constant_path->parent);

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
