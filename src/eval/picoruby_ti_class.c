#include "picoruby_ti_class.h"
#include "picoruby_ti_builtin_database.h"
#include "picoruby_ti_define_info.h"
#include "picoruby_ti_t.h"
#include "picoruby_ti_t_frame.h"
#include <stdint.h>

void
ti_eval_class(TiContext *context, const pm_class_node_t *class_node) {
  uint16_t name_id;

  if (!ti_convert_constant_id(context, class_node->name, &name_id)) {
    context->failed = 1;
    return;
  }

  uint16_t define_row =
    ti_calculate_row(context, class_node->base.location.start);

  TiDefineInfo *define_info = ti_set_define_info(name_id, 0, define_row, 1);

  if (!define_info)
    return;

  if (
    class_node->superclass &&
    PM_NODE_TYPE(class_node->superclass) == PM_CONSTANT_READ_NODE
  ) {

    uint16_t superclass_name_id;

    if (
      ti_convert_constant_id(
        context,
        ((const pm_constant_read_node_t *)class_node->superclass)->name,
        &superclass_name_id
      )
    ) {

      define_info->superclass_name_id = superclass_name_id;
    }
  }

  uint16_t class_t_node_index = ti_new_t(TI_CLASS_CLASS, TI_T_FLAG_STATIC, 0);

  if (class_t_node_index == 0 || !ti_set_value_t(name_id, class_t_node_index))
    context->failed = 1;
}
