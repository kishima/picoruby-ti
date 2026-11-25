#include "picoruby_ti_arena.h"
#include "picoruby_ti_builtin_database.h"
#include "picoruby_ti_diagnostic.h"
#include "picoruby_ti_hover.h"
#include "picoruby_ti_suggest.h"
#include "picoruby_ti_t.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
has_suggestion(const TiSuggestionList *suggestions, const char *contents) {
  for (int index = 0; index < suggestions->count; index++) {
    if (strcmp(suggestions->items[index].contents, contents) == 0)
      return 1;
  }

  return 0;
}

static TiSourceList
source_list_for(const char *source, TiSource *source_item) {
  source_item->source = source;
  source_item->source_byte_length = (int)strlen(source);

  TiSourceList sources = {
    .items = source_item,
    .count = 1,
  };

  return sources;
}

static int
find_hover(const char *source, int cursor_byte_offset, TiHoverInfo *hover_info) {
  TiSource source_item;
  TiSourceList sources = source_list_for(source, &source_item);

  return ti_find_hover_at_cursor(&sources, cursor_byte_offset, hover_info);
}

static TiSuggestionList
suggest_source(const char *source) {
  TiSuggestionList suggestions;
  int source_length = (int)strlen(source);
  TiSource source_item;
  TiSourceList sources = source_list_for(source, &source_item);

  ti_fill_suggestions_at_cursor(
    &sources,
    source_length,
    &suggestions
  );

  return suggestions;
}

static TiDiagnosticList
diagnose_source(const char *source) {
  TiDiagnosticList diagnostics;
  TiSource source_item;
  TiSourceList sources = source_list_for(source, &source_item);

  ti_fill_diagnostics(
    &sources,
    &diagnostics
  );

  return diagnostics;
}

static void
test_builtin_argument_type_diagnostic(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("\"x\".tr(1, \"a\")");

  assert(diagnostics.count == 1);
  assert(strcmp(
    diagnostics.items[0].message,
    "type mismatch: expected String, but got Integer for String.tr"
  ) == 0);
  assert(diagnostics.items[0].start_byte_offset == 7);
  assert(diagnostics.items[0].end_byte_offset == 8);

  diagnostics =
    diagnose_source("v = 1\nv = \"x\"\n\"x\".tr(v, \"a\")");
  assert(diagnostics.count == 0);

  diagnostics =
    diagnose_source("v = 1\nv = []\n\"x\".tr(v, \"a\")");
  assert(diagnostics.count == 1);
}

static void
test_unknown_argument_has_no_diagnostic(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("\"x\".tr(unknown, \"a\")");

  assert(diagnostics.count == 0);
}

static void
test_array_and_hash_contents_have_no_diagnostic(void) {
  TiDiagnosticList array_diagnostics =
    diagnose_source("[1] + [\"x\"]");
  TiDiagnosticList hash_diagnostics =
    diagnose_source("{a: 1}.merge({a: \"x\"})");

  assert(array_diagnostics.count == 0);
  assert(hash_diagnostics.count == 0);
}

static void
test_builtin_argument_count_diagnostic(void) {
  TiDiagnosticList diagnostics = diagnose_source("1.to_s(10, 2)");

  assert(diagnostics.count == 1);
  assert(strcmp(
    diagnostics.items[0].message,
    "too many arguments for Integer.to_s"
  ) == 0);
}

static void
test_splat_arguments_have_no_argument_count_diagnostic(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("a = [\"a\", \"b\"]\n\"x\".tr(*a)");
  assert(diagnostics.count == 0);

  diagnostics =
    diagnose_source("a = []\n\"x\".tr(\"a\", \"b\", *a)");
  assert(diagnostics.count == 0);

  diagnostics =
    diagnose_source("a = {}\n\"x\".tr(\"a\", \"b\", **a)");
  assert(diagnostics.count == 0);
}

static void
test_user_defined_method_arguments_have_no_diagnostic(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("def call(value)\nvalue\nend\ncall()\ncall(1, 2)");

  assert(diagnostics.count == 0);
}

static void
test_user_defined_method_return_type_is_scoped_by_class(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("class Hoge\n"
                    "  def test\n"
                    "    '1'\n"
                    "  end\n"
                    "end\n"
                    "h = Hoge.new\n"
                    "1 + h.test\n"
                    "def test\n"
                    "  1\n"
                    "end");

  assert(diagnostics.count == 1);
}

static void
test_user_defined_method_does_not_share_type_with_local_variable(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("def test\n"
                    "  '1'\n"
                    "end\n"
                    "test = 1\n"
                    "1 + test()");

  assert(diagnostics.count == 1);
}

static void
test_literal_bindings(void) {
  TiSuggestionList integer_suggestions = suggest_source("a = 1\na.");
  assert(has_suggestion(&integer_suggestions, "abs"));

  TiSuggestionList string_suggestions = suggest_source("s = \"x\"\ns.sp");
  assert(has_suggestion(&string_suggestions, "split"));

  TiSuggestionList hash_suggestions = suggest_source("h = {}\nh.ke");
  assert(has_suggestion(&hash_suggestions, "key"));
}

static void
test_binding_lookup(void) {
  TiSuggestionList suggestions = suggest_source("a = 1\nb = a\nb.ab");
  assert(has_suggestion(&suggestions, "abs"));
}

static void
test_method_chain(void) {
  TiSuggestionList suggestions =
    suggest_source("s = \"abc\".tr(\"a\", \"b\")\ns.le");
  assert(has_suggestion(&suggestions, "length"));
}

static void
test_instance_variable(void) {
  TiSuggestionList suggestions = suggest_source("@x = 1\n@x.ab");
  assert(has_suggestion(&suggestions, "abs"));
}

static void
test_definition_return(void) {
  TiSuggestionList suggestions =
    suggest_source("a = 1\ndef plus_one(a) = a + 1\nplus_one().ab");
  assert(has_suggestion(&suggestions, "abs"));
}

static void
test_definition_binding_return(void) {
  TiSuggestionList suggestions = suggest_source("def message\n"
                                                "  value = \"x\"\n"
                                                "  value\n"
                                                "end\n"
                                                "message().sp");
  assert(has_suggestion(&suggestions, "split"));
}

static void
test_if_return(void) {
  TiSuggestionList suggestions = suggest_source("def mixed\n"
                                                "  if condition\n"
                                                "    1\n"
                                                "  else\n"
                                                "    \"x\"\n"
                                                "  end\n"
                                                "end\n"
                                                "mixed().");
  assert(has_suggestion(&suggestions, "abs"));
  assert(has_suggestion(&suggestions, "bytes"));
}

static void
test_case_return(void) {
  TiSuggestionList suggestions = suggest_source("def mixed\n"
                                                "  case value\n"
                                                "  when 1, 2\n"
                                                "    1\n"
                                                "  else\n"
                                                "    \"x\"\n"
                                                "  end\n"
                                                "end\n"
                                                "mixed().");
  assert(has_suggestion(&suggestions, "abs"));
  assert(has_suggestion(&suggestions, "bytes"));
}

static void
test_case_without_else_return(void) {
  const char *source = "result = case value\n"
                       "when 1\n"
                       "  1\n"
                       "end\n"
                       "result\n";
  const char *target = strrchr(source, 'r');
  TiHoverInfo hover_info;

  assert(target);
  assert(find_hover(
    source,
    (int)(target - source),
    &hover_info
  ));
  assert(strcmp(hover_info.type_name, "Union<Integer NilClass>") == 0);
}

static void
test_case_condition_evaluation(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("result = case value\n"
                    "when \"x\".tr(1, \"a\")\n"
                    "  1\n"
                    "end");
  assert(diagnostics.count == 1);

  TiSuggestionList suggestions =
    suggest_source("result = case\n"
                   "when assigned = 1\n"
                   "  nil\n"
                   "end\n"
                   "assigned.");
  assert(has_suggestion(&suggestions, "abs"));
}

static void
test_case_match_return(void) {
  TiSuggestionList suggestions = suggest_source("def mixed\n"
                                                "  case value\n"
                                                "  in Integer\n"
                                                "    1\n"
                                                "  else\n"
                                                "    \"x\"\n"
                                                "  end\n"
                                                "end\n"
                                                "mixed().");
  assert(has_suggestion(&suggestions, "abs"));
  assert(has_suggestion(&suggestions, "bytes"));
}

static void
test_case_match_without_else_return(void) {
  const char *source = "result = case value\n"
                       "in Integer\n"
                       "  1\n"
                       "end\n"
                       "result\n";
  const char *target = strrchr(source, 'r');
  TiHoverInfo hover_info;

  assert(target);
  assert(find_hover(
    source,
    (int)(target - source),
    &hover_info
  ));
  assert(strcmp(hover_info.type_name, "Integer") == 0);
}

static void
test_case_match_pattern_evaluation(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("result = case value\n"
                    "in ^(\"x\".tr(1, \"a\"))\n"
                    "  1\n"
                    "else\n"
                    "  2\n"
                    "end");
  assert(diagnostics.count == 1);

  TiSuggestionList suggestions =
    suggest_source("result = case value\n"
                   "in ^(assigned = 1)\n"
                   "  nil\n"
                   "else\n"
                   "  nil\n"
                   "end\n"
                   "assigned.");
  assert(has_suggestion(&suggestions, "abs"));
}

static void
test_case_match_pattern_variable_has_no_binding(void) {
  TiSuggestionList suggestions =
    suggest_source("result = case value\n"
                   "in captured\n"
                   "  nil\n"
                   "else\n"
                   "  nil\n"
                   "end\n"
                   "captured.");
  assert(suggestions.count == 0);
}

static void
test_explicit_return(void) {
  TiSuggestionList suggestions = suggest_source("def mixed\n"
                                                "  return 1\n"
                                                "  \"x\"\n"
                                                "end\n"
                                                "mixed().");
  assert(has_suggestion(&suggestions, "abs"));
  assert(has_suggestion(&suggestions, "bytes"));
}

static void
test_type_at_cursor(void) {
  const char *source = "value = 1\nvalue\n";
  const char *target = strrchr(source, 'v');
  assert(target);

  TiHoverInfo hover_info;
  int found = find_hover(
    source,
    (int)(target - source),
    &hover_info
  );

  assert(found);
  assert(!hover_info.is_method);
  assert(strcmp(hover_info.variable_name, "value") == 0);
  assert(strcmp(hover_info.type_name, "Integer") == 0);
}

static void
test_union_type_at_cursor(void) {
  const char *source = "value = 1\nvalue = \"x\"\nvalue\n";
  const char *target = strrchr(source, 'v');
  assert(target);

  TiHoverInfo hover_info;
  assert(find_hover(
    source,
    (int)(target - source),
    &hover_info
  ));
  assert(strcmp(hover_info.type_name, "Union<Integer String>") == 0);
}

static void
test_builtin_method_at_cursor(void) {
  const char *source = "\"x\".bytes";
  const char *target = strstr(source, "bytes");
  TiHoverInfo hover_info;

  assert(target);
  assert(find_hover(
    source,
    (int)(target - source),
    &hover_info
  ));
  assert(hover_info.is_method);
  assert(hover_info.method_name_length == 5);
  assert(hover_info.method_document);
  assert(strncmp(hover_info.method_signature, "bytes:", 6) == 0);
}

static void
test_defined_method_at_cursor(void) {
  const char *source = "def answer(value) = 1\nanswer(1)";
  const char *target = strrchr(source, 'a');
  TiHoverInfo hover_info;

  assert(target);
  assert(find_hover(
    source,
    (int)(target - source),
    &hover_info
  ));
  assert(hover_info.is_method);
  assert(hover_info.method_name_length == 6);
  assert(strcmp(hover_info.method_document, "") == 0);
  assert(strcmp(hover_info.method_signature, "answer(value) -> Integer") == 0);
}

static void
test_forward_definition(void) {
  TiSuggestionList suggestions =
    suggest_source("x = my_method()\ndef my_method() = \"x\"\nx.sp");
  assert(has_suggestion(&suggestions, "split"));
}

static void
test_preload_source_definition(void) {
  const char *preload_source = "def answer() = 1";
  const char *source = "answer().";
  TiSource source_items[] = {
    {
      .source = preload_source,
      .source_byte_length = (int)strlen(preload_source),
    },
    {
      .source = source,
      .source_byte_length = (int)strlen(source),
    },
  };
  TiSourceList sources = {
    .items = source_items,
    .count = 2,
  };
  TiSuggestionList suggestions;

  ti_fill_suggestions_at_cursor(
    &sources,
    source_items[1].source_byte_length,
    &suggestions
  );

  assert(has_suggestion(&suggestions, "abs"));
}

static void
test_union_binding(void) {
  TiSuggestionList suggestions = suggest_source("v = 1\nv = \"s\"\nv.");
  assert(has_suggestion(&suggestions, "abs"));
  assert(has_suggestion(&suggestions, "bytes"));
}

static void
test_union_capacity(void) {
  uint8_t class_identifiers[TI_UNION_CAPACITY] = {
    TI_CLASS_INTEGER,
    TI_CLASS_FLOAT,
    TI_CLASS_STRING,
    TI_CLASS_SYMBOL,
    TI_CLASS_TRUE,
  };

  ti_reset_arena();
  assert(ti_initialize_t());

  uint16_t union_t_node_index = 0;

  for (int index = 0; index < TI_UNION_CAPACITY; index++) {
    uint16_t t_node_index = ti_new_t(class_identifiers[index], 0, 0);
    union_t_node_index = ti_make_union(union_t_node_index, t_node_index);
  }

  int union_member_count = 0;

  for (const T *union_t = ti_get_t(union_t_node_index);
       union_t;
       union_t = ti_get_t(union_t->union_next)) {
    union_member_count++;
  }

  assert(union_member_count == TI_UNION_CAPACITY);

  uint16_t t_node_index = ti_new_t(TI_CLASS_FALSE, 0, 0);
  union_t_node_index = ti_make_union(union_t_node_index, t_node_index);

  const T *union_t = ti_get_t(union_t_node_index);
  assert(union_t);
  assert(union_t->object_class_id == TI_CLASS_UNTYPED);
  assert(union_t->union_next == 0);
}

static void
test_unknown_return(void) {
  TiSuggestionList suggestions =
    suggest_source("def orphan(x) = x.foo\norphan().");
  assert(suggestions.count == 0);
}

static void
test_inherited_method_return_type(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("class Base\n"
                    "  def label\n"
                    "    \"x\"\n"
                    "  end\n"
                    "end\n"
                    "class Child < Base\n"
                    "end\n"
                    "child = Child.new\n"
                    "1 + child.label");

  assert(diagnostics.count == 1);
}

static void
test_inherited_builtin_argument_diagnostic(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("class MyPin < GPIO\n"
                    "end\n"
                    "pin = MyPin.new\n"
                    "pin.write(\"high\")");

  assert(diagnostics.count == 1);
  assert(strcmp(
    diagnostics.items[0].message,
    "type mismatch: expected Integer, but got String for GPIO.write"
  ) == 0);
}

static void
test_declared_constant_type(void) {
  TiDiagnosticList diagnostics = diagnose_source("x = 1 + GPIO::LOW");
  assert(diagnostics.count == 0);

  diagnostics = diagnose_source("\"x\".tr(GPIO::LOW, \"a\")");
  assert(diagnostics.count == 1);
}

static void
test_subclass_argument_satisfies_superclass_parameter(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("class MyPin < GPIO\n"
                    "end\n"
                    "a = GPIO.new(1, 2)\n"
                    "b = MyPin.new\n"
                    "a.attach(b)");

  assert(diagnostics.count == 0);

  diagnostics =
    diagnose_source("class Grand < GPIO\n"
                    "end\n"
                    "class Child < Grand\n"
                    "end\n"
                    "a = GPIO.new(1, 2)\n"
                    "a.attach(Child.new)");

  assert(diagnostics.count == 0);

  diagnostics =
    diagnose_source("a = GPIO.new(1, 2)\n"
                    "a.attach(\"nope\")");

  assert(diagnostics.count == 1);
}

static void
test_declared_instance_variable_type_at_cursor(void) {
  const char *source = "class MyPin < GPIO\n"
                       "  def use\n"
                       "    @pin\n"
                       "  end\n"
                       "end\n";
  const char *target = strstr(source, "@pin\n");
  assert(target);

  TiHoverInfo hover_info;
  int found = find_hover(
    source,
    (int)(target - source) + 1,
    &hover_info
  );

  assert(found);
  assert(!hover_info.is_method);
  assert(strcmp(hover_info.variable_name, "@pin") == 0);
  assert(strcmp(hover_info.type_name, "Integer") == 0);
}

static void
test_declared_instance_variable_diagnostic(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("class MyPin < GPIO\n"
                    "  def use\n"
                    "    1 + @label\n"
                    "  end\n"
                    "end");

  assert(diagnostics.count == 1);
}

/* The type hovering the last occurrence of `name` in source shows, or "". */
static const char *
type_of_last(const char *source, const char *name, TiHoverInfo *hover_info) {
  const char *target = NULL;
  const char *found = source;

  while ((found = strstr(found, name)) != NULL) {
    target = found;
    found++;
  }

  assert(target);

  if (!find_hover(source, (int)(target - source), hover_info))
    return "";

  return hover_info->type_name;
}

static void
test_nested_class_path(void) {
  TiHoverInfo hover_info;

  /* two segments: .new and a static method */
  assert(strcmp(type_of_last("b = TiNest::Box.new(1)\nb\n", "b", &hover_info),
                "TiNest::Box") == 0);
  assert(strcmp(type_of_last("b = TiNest::Box.open(1)\nb\n", "b", &hover_info),
                "TiNest::Box") == 0);
  /* a module function on a nested receiver */
  assert(strcmp(type_of_last("b = TiNest.make(:a)\nb\n", "b", &hover_info),
                "TiNest::Box") == 0);
  /* three segments */
  assert(strcmp(
    type_of_last("l = TiNest::Box::Lid.new\nl\n", "l", &hover_info),
    "TiNest::Box::Lid") == 0);
  /* a leading :: */
  assert(strcmp(type_of_last("b = ::TiNest::Box.new(1)\nb\n", "b", &hover_info),
                "TiNest::Box") == 0);
  /* the class held in a variable */
  assert(strcmp(
    type_of_last("k = TiNest::Box\nb = k.new(1)\nb\n", "b", &hover_info),
    "TiNest::Box") == 0);
  /* chained to an instance method */
  assert(strcmp(
    type_of_last("s = TiNest::Box.new(1).size\ns\n", "s", &hover_info),
    "Integer") == 0);
}

static void
test_nested_class_constants(void) {
  TiHoverInfo hover_info;

  /* Klass::CONST stays as it was */
  assert(strcmp(type_of_last("v = GPIO::LOW\nv\n", "v", &hover_info),
                "Integer") == 0);
  assert(strcmp(type_of_last("v = TiNest::SIZE\nv\n", "v", &hover_info),
                "Integer") == 0);
  /* a constant of a nested class, and through a leading :: */
  assert(strcmp(type_of_last("v = TiNest::Box::MAX\nv\n", "v", &hover_info),
                "Integer") == 0);
  assert(strcmp(type_of_last("v = ::GPIO::LOW\nv\n", "v", &hover_info),
                "Integer") == 0);
}

static void
test_nested_class_path_unknown(void) {
  TiHoverInfo hover_info;

  /* no such class or constant: untyped, no diagnostic */
  assert(strcmp(type_of_last("b = TiNest::Nope.new(1)\nb\n", "b", &hover_info),
                "") == 0);
  assert(strcmp(type_of_last("b = TiNest::Box::Nope\nb\n", "b", &hover_info),
                "") == 0);
  assert(strcmp(type_of_last("b = Nope::Box.new(1)\nb\n", "b", &hover_info),
                "") == 0);
  /* a prefix of a real name is not that name */
  assert(strcmp(type_of_last("b = Nest::Box.new(1)\nb\n", "b", &hover_info),
                "") == 0);
  assert(strcmp(type_of_last("b = TiNest::Bo.new(1)\nb\n", "b", &hover_info),
                "") == 0);
  /* a path whose root is not a constant */
  assert(strcmp(type_of_last("b = foo::Box.new(1)\nb\n", "b", &hover_info),
                "") == 0);
  /* longer than any database class: untyped, and nothing breaks */
  assert(strcmp(type_of_last(
    "b = A::B::C::D::E::F::G::H::I::J::K::L::TiNest::Box.new(1)\nb\n",
    "b", &hover_info), "") == 0);
  assert(strcmp(type_of_last(
    "b = ::TiNest::Box::Lid::A::B::C::D::E::F::G::H.new\nb\n",
    "b", &hover_info), "") == 0);

  TiDiagnosticList diagnostics =
    diagnose_source("TiNest::Nope.new(1, 2, 3)\n"
                    "A::B::C::D::E::F::G::H::I::J.new(1)\n");
  assert(diagnostics.count == 0);
}

static void
test_nested_class_user_class_unchanged(void) {
  TiHoverInfo hover_info;

  /* user-defined nested classes stay as they were: untyped through a path */
  assert(strcmp(type_of_last("module M\n  class C\n  end\nend\n"
                             "c = M::C.new\nc\n", "c", &hover_info),
                "") == 0);
}

static void
test_nested_class_diagnostic(void) {
  TiDiagnosticList diagnostics =
    diagnose_source("b = TiNest::Box.new(1)\nb.resize(1, 2)");
  assert(diagnostics.count == 1);
  assert(strcmp(
    diagnostics.items[0].message,
    "too many arguments for TiNest::Box.resize"
  ) == 0);

  diagnostics = diagnose_source("b = TiNest::Box.new(1)\nb.resize(1)");
  assert(diagnostics.count == 0);

  /* static methods: .new, a module function, a three-segment receiver */
  diagnostics = diagnose_source("TiNest::Box.new(1, 2)");
  assert(diagnostics.count == 1);

  diagnostics = diagnose_source("TiNest.make(:a, 1, 2)");
  assert(diagnostics.count == 1);

  diagnostics = diagnose_source("TiNest::Box.open(\"x\")");
  assert(diagnostics.count == 1);
  assert(strcmp(
    diagnostics.items[0].message,
    "type mismatch: expected Integer, but got String for TiNest::Box.open"
  ) == 0);

  diagnostics = diagnose_source("TiNest::Box::Lid.new.close(1)");
  assert(diagnostics.count == 1);

  /* a nested constant as an argument */
  diagnostics = diagnose_source("\"x\".tr(TiNest::Box::MAX, \"a\")");
  assert(diagnostics.count == 1);
  diagnostics = diagnose_source("x = 1 + TiNest::Box::MAX");
  assert(diagnostics.count == 0);
}

static int
diagnostic_count_is(const char *source, int count) {
  TiDiagnosticList diagnostics = diagnose_source(source);
  return diagnostics.count == count;
}

static const char *
hover_type_at(const char *source, const char *needle) {
  static TiHoverInfo hover_info;
  const char *target = strstr(source, needle);
  assert(target);

  /* the cursor on the needle's first non-blank */
  target += strspn(target, " ");

  if (!find_hover(source, (int)(target - source), &hover_info))
    return "(none)";

  return hover_info.type_name;
}

static void
test_rescue_variable_type(void) {
  /* one class, a bare rescue (StandardError), a nested class */
  TiDiagnosticList diagnostics = diagnose_source(
    "begin\n  x = 1\nrescue ArgumentError => e\n  e.message(1)\nend"
  );
  assert(diagnostics.count == 1);
  assert(strcmp(
    diagnostics.items[0].message,
    "too many arguments for ArgumentError.message"
  ) == 0);

  diagnostics =
    diagnose_source("begin\n  x = 1\nrescue => e\n  e.message(1)\nend");
  assert(diagnostics.count == 1);
  assert(strcmp(
    diagnostics.items[0].message,
    "too many arguments for StandardError.message"
  ) == 0);

  assert(diagnostic_count_is(
    "begin\nrescue JSON::ParserError => e\n  e.message(1)\nend", 1));
  assert(diagnostic_count_is(
    "begin\nrescue ::ArgumentError => e\n  e.message\nend", 0));

  /* the types the variable gets */
  assert(strcmp(hover_type_at(
    "begin\nrescue ArgumentError => e\n  e\nend", "  e\n"),
    "ArgumentError") == 0);
  assert(strcmp(hover_type_at(
    "begin\nrescue => e\n  e\nend", "  e\n"), "StandardError") == 0);
  assert(strcmp(hover_type_at(
    "begin\nrescue JSON::ParserError => e\n  e\nend", "  e\n"),
    "JSON::ParserError") == 0);
  /* several classes: a union (unions are not checked for arguments) */
  assert(strcmp(hover_type_at(
    "begin\nrescue ArgumentError, RuntimeError => e\n  e\nend", "  e\n"),
    "Union<ArgumentError RuntimeError>") == 0);
  assert(diagnostic_count_is(
    "begin\nrescue ArgumentError, RuntimeError => e\n  e.message(1)\nend", 0));
  /* more classes than a union holds: untyped, no diagnostic */
  assert(diagnostic_count_is(
    "begin\nrescue ArgumentError, RuntimeError, TypeError, IndexError,"
    " KeyError, RangeError => e\n  e.message(1)\nend", 0));

  /* instance and global variables take the type too */
  assert(diagnostic_count_is(
    "begin\nrescue => @err\n  @err.message(1)\nend", 1));
  assert(diagnostic_count_is(
    "begin\nrescue => $err\n  $err.message(1)\nend", 1));
}

static void
test_rescue_variable_unknown_class(void) {
  /* the user's class, one unknown among known ones, a splat, an unknown
     constant: the variable stays untyped */
  assert(diagnostic_count_is(
    "class MyErr < StandardError\nend\n"
    "begin\nrescue MyErr => e\n  e.message(1)\nend", 0));
  assert(diagnostic_count_is(
    "begin\nrescue MyErr, ArgumentError => e\n  e.message(1)\nend", 0));
  assert(diagnostic_count_is(
    "begin\nrescue *ERRS => e\n  e.message(1)\nend", 0));
  assert(diagnostic_count_is(
    "begin\nrescue NoSuch::Error => e\n  e.message(1)\nend", 0));
  assert(diagnostic_count_is(
    "begin\nrescue error_class => e\n  e.message(1)\nend", 0));
}

static void
test_rescue_forms(void) {
  /* a method body with rescue, a begin inside a method */
  assert(diagnostic_count_is(
    "def f\n  1\nrescue => e\n  e.message(1)\nend", 1));
  assert(diagnostic_count_is(
    "def h\n  begin\n    1\n  rescue => e\n    e.message(1)\n  end\nend", 1));
  /* ensure and else are evaluated too */
  assert(diagnostic_count_is(
    "begin\n  1\nrescue => e\n  e.message(1)\n"
    "ensure\n  \"a\".byteslice(1, 2, 3)\nend", 2));
  assert(diagnostic_count_is(
    "def g\n  1\nrescue => e\n  e.message(1)\n"
    "else\n  \"a\".byteslice(1, 2, 3)\nensure\n  2\nend", 2));
  assert(diagnostic_count_is("begin\n  1\nensure\n  2\nend", 0));
  /* several rescue clauses, each its own variable */
  TiDiagnosticList diagnostics = diagnose_source(
    "begin\nrescue ArgumentError => a\n  a.message(1)\n"
    "rescue => b\n  b.message(1)\nend"
  );
  assert(diagnostics.count == 2);
  assert(strcmp(
    diagnostics.items[0].message,
    "too many arguments for ArgumentError.message"
  ) == 0);
  assert(strcmp(
    diagnostics.items[1].message,
    "too many arguments for StandardError.message"
  ) == 0);
  /* no variable, a modifier rescue, an empty clause: nothing breaks */
  assert(diagnostic_count_is("begin\n  1\nrescue ArgumentError\n  2\nend", 0));
  assert(diagnostic_count_is(
    "x = foo rescue nil\n\"a\".byteslice(1, 2, 3)", 1));
  assert(diagnostic_count_is("begin\nrescue => e\nend", 0));
  /* a block's body is not evaluated (as before): no diagnostic, no crash */
  assert(diagnostic_count_is(
    "[1].each do |i|\n  i\nrescue => e\n  e.message(1)\nend", 0));
  /* still open (being typed) */
  assert(diagnostic_count_is("begin\n  1\nrescue => e\n  e.message(1)\n", 1));
}

static void
test_rescue_method_return(void) {
  /* a method with rescue returns what its body or its rescue leaves */
  TiSuggestionList suggestions = suggest_source(
    "def g\n  1\nrescue\n  \"x\"\nend\ng()."
  );
  assert(has_suggestion(&suggestions, "abs"));
  assert(has_suggestion(&suggestions, "bytes"));

  suggestions = suggest_source(
    "def k\n  1\nrescue => e\n  e\nelse\n  \"x\"\nensure\n  :s\nend\nk()."
  );
  assert(has_suggestion(&suggestions, "bytes"));
  assert(has_suggestion(&suggestions, "message"));
  assert(!has_suggestion(&suggestions, "abs"));
}

static void
test_binding_overflow(void) {
  size_t capacity = 24000;
  char *source = malloc(capacity);
  assert(source);

  size_t offset = 0;
  for (int index = 0; index < 700; index++) {
    int written = snprintf(
      source + offset,
      capacity - offset,
      "value_%d = %d\n",
      index,
      index
    );
    assert(written > 0);
    offset += (size_t)written;
    assert(offset < capacity - 16);
  }

  memcpy(source + offset, "value_0.\0", 9);
  offset += 8;

  TiSuggestionList suggestions;
  TiSource source_item;
  TiSourceList sources = source_list_for(source, &source_item);
  int count = ti_fill_suggestions_at_cursor(
    &sources,
    (int)offset,
    &suggestions
  );
  assert(count == 0);

  free(source);
}

int
main(void) {
  test_nested_class_path();
  test_nested_class_constants();
  test_nested_class_path_unknown();
  test_nested_class_user_class_unchanged();
  test_nested_class_diagnostic();
  test_rescue_variable_type();
  test_rescue_variable_unknown_class();
  test_rescue_forms();
  test_rescue_method_return();
  test_builtin_argument_type_diagnostic();
  test_unknown_argument_has_no_diagnostic();
  test_array_and_hash_contents_have_no_diagnostic();
  test_builtin_argument_count_diagnostic();
  test_splat_arguments_have_no_argument_count_diagnostic();
  test_user_defined_method_arguments_have_no_diagnostic();
  test_user_defined_method_return_type_is_scoped_by_class();
  test_user_defined_method_does_not_share_type_with_local_variable();
  test_literal_bindings();
  test_binding_lookup();
  test_method_chain();
  test_instance_variable();
  test_definition_return();
  test_definition_binding_return();
  test_if_return();
  test_case_return();
  test_case_without_else_return();
  test_case_condition_evaluation();
  test_case_match_return();
  test_case_match_without_else_return();
  test_case_match_pattern_evaluation();
  test_case_match_pattern_variable_has_no_binding();
  test_explicit_return();
  test_type_at_cursor();
  test_union_type_at_cursor();
  test_builtin_method_at_cursor();
  test_defined_method_at_cursor();
  test_forward_definition();
  test_preload_source_definition();
  test_union_binding();
  test_union_capacity();
  test_unknown_return();
  test_binding_overflow();
  test_inherited_method_return_type();
  test_inherited_builtin_argument_diagnostic();
  test_declared_instance_variable_diagnostic();
  test_declared_instance_variable_type_at_cursor();
  test_subclass_argument_satisfies_superclass_parameter();
  test_declared_constant_type();

  return 0;
}
