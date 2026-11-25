#ifndef PICORUBY_TI_RESCUE_H
#define PICORUBY_TI_RESCUE_H

#include "picoruby_ti_context.h"
#include <stdint.h>

/* Give the variable of "rescue Foo => e" the type of what it catches: an
   instance of Foo, of any of "rescue A, B" (a union), or of StandardError
   for a bare "rescue => e". A class the database does not know (the user's
   own) leaves the variable untyped, as does a splat of classes. */
void ti_bind_rescue_reference(
  TiContext *context,
  const pm_rescue_node_t *rescue_node
);

/* "begin ... rescue ... else ... ensure ... end", and the body of a method
   with rescue/ensure: evaluates every clause (binding each rescue variable
   first) and returns the union of what the body (or its else) and each
   rescue clause leave. */
uint16_t
ti_eval_begin(TiContext *context, const pm_begin_node_t *begin_node, int depth);

#endif
