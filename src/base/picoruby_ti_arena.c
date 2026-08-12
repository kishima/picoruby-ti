#include "picoruby_ti_arena.h"
#include <stdint.h>

/*
 * The working arena is the largest thing this engine owns, and on a
 * microcontroller the memory it lands in is the host's decision, not ours: a
 * board with external RAM would rather spend that than its scarce internal
 * RAM, and only the host knows which attribute says so.
 *
 * So the host may inject one. TI_ARENA_INCLUDE names a header to pull in
 * (where the attribute is defined) and TI_ARENA_ATTR is the attribute itself.
 * Defining neither -- the host tests, and any ordinary build -- leaves an
 * ordinary static array, so nothing changes for anyone who does not care.
 */
#ifdef TI_ARENA_INCLUDE
#include TI_ARENA_INCLUDE
#endif

#ifndef TI_ARENA_ATTR
#define TI_ARENA_ATTR
#endif

TI_ARENA_ATTR static uint8_t arena_bytes[TI_ARENA_SIZE];
static size_t arena_offset;
static int arena_overflowed;

void
ti_reset_arena(void) {
  arena_offset = 0;
  arena_overflowed = 0;
}

void *
ti_allocate_from_arena(size_t size) {
  size_t aligned_size = (size + 7U) & ~(size_t)7U;

  if (aligned_size > TI_ARENA_SIZE - arena_offset) {
    arena_overflowed = 1;
    return NULL;
  }

  void *allocation = &arena_bytes[arena_offset];
  arena_offset += aligned_size;

  return allocation;
}

int
ti_did_arena_overflow(void) {
  return arena_overflowed;
}
