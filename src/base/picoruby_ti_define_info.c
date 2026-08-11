#include "picoruby_ti_define_info.h"
#include "picoruby_ti_arena.h"
#include "picoruby_ti_builtin.h"
#include "picoruby_ti_builtin_database.h"
#include "picoruby_ti_name.h"
#include <stddef.h>
#include <string.h>

static TiDefineInfo *define_infos;
static int define_info_count;

int
ti_initialize_define_infos(void) {
  define_infos =
    ti_allocate_from_arena(sizeof(TiDefineInfo) * TI_DEFINE_INFO_CAPACITY);

  if (!define_infos)
    return 0;

  define_info_count = 0;

  return 1;
}

TiDefineInfo *
ti_set_define_info(
  uint16_t name_id,
  uint16_t owner_class_name_id,
  uint16_t define_row,
  int is_class
) {

  if (name_id == 0)
    return NULL;

  for (int index = 0; index < define_info_count; index++) {
    TiDefineInfo *define_info = &define_infos[index];

    if (define_info->name_id == name_id &&
        define_info->owner_class_name_id == owner_class_name_id &&
        define_info->is_class == is_class) {

      return define_info;
    }
  }

  if (define_info_count >= TI_DEFINE_INFO_CAPACITY)
    return NULL;

  TiDefineInfo *define_info = &define_infos[define_info_count++];

  memset(define_info, 0, sizeof(*define_info));

  define_info->name_id = name_id;
  define_info->owner_class_name_id = owner_class_name_id;
  define_info->define_row = define_row;
  define_info->is_class = is_class ? 1U : 0U;

  return define_info;
}

TiDefineInfo *
ti_get_define_info(int index) {
  if (index < 0 || index >= define_info_count)
    return NULL;

  return &define_infos[index];
}

int
ti_get_define_info_count(void) {
  return define_info_count;
}

uint8_t
ti_get_defined_class_id(uint16_t name_id) {
  int user_class_index = 0;

  for (int index = 0; index < define_info_count; index++) {
    TiDefineInfo *define_info = &define_infos[index];

    if (!define_info->is_class)
      continue;

    if (define_info->name_id == name_id) {
      return (uint8_t)(TI_CLASS_USER_BASE + user_class_index);
    }

    user_class_index++;
  }

  return TI_CLASS_NONE;
}

TiDefineInfo *
ti_get_class_define_info(uint8_t class_id) {
  if (class_id < TI_CLASS_USER_BASE)
    return NULL;

  int user_class_index = class_id - TI_CLASS_USER_BASE;
  int current_class_index = 0;

  for (int index = 0; index < define_info_count; index++) {
    TiDefineInfo *define_info = &define_infos[index];

    if (!define_info->is_class)
      continue;

    if (current_class_index == user_class_index)
      return define_info;

    current_class_index++;
  }

  return NULL;
}

uint8_t
ti_resolve_superclass_id(uint8_t class_id) {
  const TiDefineInfo *define_info = ti_get_class_define_info(class_id);

  if (!define_info || define_info->superclass_name_id == 0)
    return TI_CLASS_NONE;

  uint8_t user_class_id =
    ti_get_defined_class_id(define_info->superclass_name_id);

  if (user_class_id != TI_CLASS_NONE && user_class_id != class_id)
    return user_class_id;

  const TiName *superclass_name =
    ti_get_name(define_info->superclass_name_id);

  if (!superclass_name)
    return TI_CLASS_NONE;

  const uint8_t *superclass_name_bytes = ti_get_name_bytes(superclass_name);

  if (!superclass_name_bytes)
    return TI_CLASS_NONE;

  return ti_get_builtin_class_id(
    superclass_name_bytes,
    superclass_name->byte_length
  );
}
