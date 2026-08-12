#ifndef PICORUBY_TI_CALL_CONTEXT_H
#define PICORUBY_TI_CALL_CONTEXT_H

#include "picoruby_ti_source.h"

#define TI_CALL_ARGUMENT_NAME_CAPACITY 48
#define TI_CALL_ARGUMENT_TYPE_CAPACITY 48

/*
 * What call the cursor is inside, and which argument it is on.
 *
 * This is what an editor needs while the arguments are being typed: the
 * method has already been chosen, so the question is no longer "what can I
 * call" but "what goes here". method_signature is the same string hover and
 * completion return, and argument_index says which parameter of it the cursor
 * has reached, counting from zero.
 *
 * The name and the type are taken from that signature, so a method declared
 * in the signatures gives both ("Integer y") and one defined in the source
 * gives the name alone.
 *
 * Strings point into the working arena and stay valid until the next request,
 * exactly like the suggestion and hover results.
 */
typedef struct {
  const char *method_signature;
  const char *method_document;
  int method_name_length;
  /* 0-based position in the argument list, or -1 when the cursor is inside
     the call but not in its arguments. */
  int argument_index;
  char argument_name[TI_CALL_ARGUMENT_NAME_CAPACITY];
  char argument_type[TI_CALL_ARGUMENT_TYPE_CAPACITY];
  int found;
} TiCallContext;

int ti_find_call_context(
  const TiSourceList *sources,
  int cursor_byte_offset,
  TiCallContext *out
);

#endif
