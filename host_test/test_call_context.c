#include "picoruby_ti_call_context.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static TiCallContext
context_at_end(const char *source) {
  TiSource item = {
    .source = source,
    .source_byte_length = (int)strlen(source),
  };
  TiSourceList sources = { .items = &item, .count = 1 };
  TiCallContext out;

  ti_find_call_context(&sources, (int)strlen(source), &out);

  return out;
}

static void
test_first_argument(void) {
  TiCallContext out = context_at_end("s = \"abc\"\ns.tr(");

  assert(out.found);
  assert(out.argument_index == 0);
  assert(strcmp(out.argument_name, "source") == 0);
  assert(strcmp(out.argument_type, "String") == 0);
  assert(out.method_signature);
}

static void
test_second_argument_after_comma(void) {
  TiCallContext out = context_at_end("s = \"abc\"\ns.tr(\"a\", ");

  assert(out.found);
  assert(out.argument_index == 1);
  assert(strcmp(out.argument_name, "replacement") == 0);
  assert(strcmp(out.argument_type, "String") == 0);
}

static void
test_innermost_call_of_a_nested_one(void) {
  /* The cursor is inside include?(...), which is inside tr(...): the inner
     call is the one being written. */
  TiCallContext out =
    context_at_end("s = \"abc\"\ns.tr(\"a\", s.include?(");

  assert(out.found);
  assert(out.argument_index == 0);
  assert(strcmp(out.argument_name, "other") == 0);
  assert(strcmp(out.argument_type, "String") == 0);
}

static void
test_user_defined_method_gives_names_only(void) {
  TiCallContext out = context_at_end(
    "class Robot\n"
    "  def step(distance, speed)\n"
    "  end\n"
    "end\n"
    "r = Robot.new\n"
    "r.step(10, "
  );

  assert(out.found);
  assert(out.argument_index == 1);
  assert(strcmp(out.argument_name, "speed") == 0);
  assert(out.argument_type[0] == '\0');
}

static void
test_cursor_outside_any_call(void) {
  TiCallContext out = context_at_end("s = \"abc\"\ns");

  assert(!out.found);
  assert(out.argument_index == -1);
}

int
main(void) {
  test_first_argument();
  test_second_argument_after_comma();
  test_innermost_call_of_a_nested_one();
  test_user_defined_method_gives_names_only();
  test_cursor_outside_any_call();

  printf("test_call_context: all tests passed\n");

  return 0;
}
