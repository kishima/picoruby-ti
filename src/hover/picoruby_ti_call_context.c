#include "picoruby_ti_call_context.h"
#include "picoruby_ti_arena.h"
#include "picoruby_ti_context.h"
#include "picoruby_ti_eval.h"
#include "picoruby_ti_suggest.h"
#include <prism.h>
#include <string.h>

typedef struct {
  const uint8_t *source;
  int source_length;
  int cursor_byte_offset;
  const pm_call_node_t *call;
} TiCallSearch;

/* Where the argument list of a call starts and ends in the source.
 *
 * A call being typed is usually unfinished -- "draw_text(10," has no closing
 * parenthesis yet -- so the end falls back to the end of the source. That is
 * what makes this useful while the arguments are still being written. */
static bool
call_argument_span(
  const TiCallSearch *search,
  const pm_call_node_t *call,
  int *out_start,
  int *out_end
) {

  if (call->opening_loc.start == NULL) return false;

  int start = (int)(call->opening_loc.end - search->source);
  int end = search->source_length;

  /* An unfinished call still gets a closing location, standing in for the
     parenthesis that has not been typed; it sits at the last thing the parser
     saw, which is before the cursor whenever a space follows the comma. Only a
     closing location that really is a ")" ends the argument list -- otherwise
     the list runs to the end of what has been written so far. */
  if (
    call->closing_loc.start != NULL &&
    call->closing_loc.start < search->source + search->source_length &&
    *call->closing_loc.start == ')'
  ) {

    end = (int)(call->closing_loc.start - search->source);
  }

  if (start < 0 || end < start) return false;

  *out_start = start;
  *out_end = end;

  return true;
}

/* Depth-first, keeping the last match: an inner call is visited after the
   outer one that contains it, so the innermost call wins. */
static bool
find_call_on_visit(const pm_node_t *node, void *data) {
  TiCallSearch *search = data;

  if (PM_NODE_TYPE(node) != PM_CALL_NODE) return true;

  const pm_call_node_t *call = (const pm_call_node_t *)node;
  int start = 0;
  int end = 0;

  if (!call_argument_span(search, call, &start, &end)) return true;

  if (
    search->cursor_byte_offset >= start &&
    search->cursor_byte_offset <= end
  ) {

    search->call = call;
  }

  return true;
}

/* Which argument the cursor sits on: the commas between the opening
   parenthesis and the cursor, ignoring anything nested inside brackets or
   quotes so that "draw_text(1, rand(3, 4" counts for the call it belongs to. */
static int
argument_index_at(
  const uint8_t *source,
  int start,
  int cursor_byte_offset
) {

  int index = 0;
  int depth = 0;
  char quote = 0;

  for (int i = start; i < cursor_byte_offset; i++) {
    char c = (char)source[i];

    if (quote != 0) {
      if (c == '\\') {
        i++;
      } else if (c == quote) {
        quote = 0;
      }
      continue;
    }

    switch (c) {
    case '"':
    case '\'':
      quote = c;
      break;
    case '(':
    case '[':
    case '{':
      depth++;
      break;
    case ')':
    case ']':
    case '}':
      if (depth > 0) depth--;
      break;
    case ',':
      if (depth == 0) index++;
      break;
    default:
      break;
    }
  }

  return index;
}

/* Copy the index-th parameter out of a rendered signature.
 *
 * Signatures read "draw_text: (Integer x, Integer y) -> FmrbGfx" for a method
 * that has declared types and "move(x, y) -> untyped" for one defined in the
 * source, so a parameter is either "<type> <name>" or a bare name. */
static void
fill_argument_from_signature(
  const char *signature,
  int argument_index,
  TiCallContext *out
) {

  if (!signature || argument_index < 0) return;

  const char *open = strchr(signature, '(');
  if (!open) return;

  const char *cursor = open + 1;
  const char *begin = cursor;
  int index = 0;
  int depth = 0;

  for (;; cursor++) {
    char c = *cursor;

    if (c == '\0') return;

    if (c == '(' || c == '[' || c == '{') {
      depth++;
      continue;
    }

    if (c == ')' && depth == 0) break;

    if (c == ')' || c == ']' || c == '}') {
      if (depth > 0) depth--;
      continue;
    }

    if (c == ',' && depth == 0) {
      if (index == argument_index) break;
      index++;
      begin = cursor + 1;
    }
  }

  if (index != argument_index) return;

  while (begin < cursor && *begin == ' ') begin++;

  const char *finish = cursor;
  while (finish > begin && finish[-1] == ' ') finish--;
  if (finish <= begin) return;

  /* "Integer x" -> type "Integer", name "x". A bare "x" is a name only. */
  const char *space = NULL;
  for (const char *p = finish - 1; p > begin; p--) {
    if (*p == ' ') {
      space = p;
      break;
    }
  }

  const char *name_start = space ? space + 1 : begin;
  size_t name_length = (size_t)(finish - name_start);

  if (name_length >= TI_CALL_ARGUMENT_NAME_CAPACITY)
    name_length = TI_CALL_ARGUMENT_NAME_CAPACITY - 1;

  memcpy(out->argument_name, name_start, name_length);
  out->argument_name[name_length] = '\0';

  if (space) {
    size_t type_length = (size_t)(space - begin);

    if (type_length >= TI_CALL_ARGUMENT_TYPE_CAPACITY)
      type_length = TI_CALL_ARGUMENT_TYPE_CAPACITY - 1;

    memcpy(out->argument_type, begin, type_length);
    out->argument_type[type_length] = '\0';
  }
}

int
ti_find_call_context(
  const TiSourceList *sources,
  int cursor_byte_offset,
  TiCallContext *out
) {

  if (out) {
    memset(out, 0, sizeof(*out));
    out->argument_index = -1;
  }

  if (!sources || !sources->items || sources->count <= 0 || !out) return 0;

  const TiSource *source = &sources->items[sources->count - 1];
  const char *source_bytes = "";

  if (source->source)
    source_bytes = source->source;

  if (
    (!source->source && source->source_byte_length > 0) ||
    cursor_byte_offset < 0 ||
    cursor_byte_offset > source->source_byte_length ||
    !ti_evaluate_sources(sources, NULL)
  ) {

    return 0;
  }

  pm_parser_t parser;

  pm_parser_init(
    &parser,
    (const uint8_t *)source_bytes,
    (size_t)source->source_byte_length,
    NULL
  );

  pm_node_t *root = pm_parse(&parser);
  if (!root) {
    pm_parser_free(&parser);
    return 0;
  }

  TiContext context = {
    .parser = &parser,
    .source = (const uint8_t *)source_bytes,
    .source_length = (size_t)source->source_byte_length,
  };

  TiCallSearch search = {
    .source = (const uint8_t *)source_bytes,
    .source_length = source->source_byte_length,
    .cursor_byte_offset = cursor_byte_offset,
  };

  if (!ti_did_arena_overflow()) {
    ti_set_enclosing_class_at_cursor(&context, root, cursor_byte_offset);
    pm_visit_node(root, find_call_on_visit, &search);
  }

  if (search.call) {
    /* The method is resolved the way hover resolves it: ask what could be
       called where its name ends, then pick the entry with that name. */
    TiSuggestionList suggestions;
    memset(&suggestions, 0, sizeof(suggestions));

    int method_name_end_byte_offset =
      (int)(search.call->message_loc.end - context.source);

    ti_collect_suggestions_at_cursor(
      &context,
      root,
      method_name_end_byte_offset,
      &suggestions
    );

    const pm_constant_t *method_name =
      ti_get_constant(&context, search.call->name);

    if (method_name) {
      for (int index = 0; index < suggestions.count; index++) {
        const TiSuggestion *suggestion = &suggestions.items[index];

        if (
          suggestion->contents_length == (int)method_name->length &&
          memcmp(
            suggestion->contents,
            method_name->start,
            method_name->length
          ) == 0
        ) {

          int start = 0;
          int end = 0;

          out->method_signature = suggestion->detail;
          out->method_document = suggestion->document;
          out->method_name_length = suggestion->contents_length;
          out->found = 1;

          if (call_argument_span(&search, search.call, &start, &end)) {
            out->argument_index = argument_index_at(
              (const uint8_t *)source_bytes,
              start,
              cursor_byte_offset
            );

            fill_argument_from_signature(
              suggestion->detail,
              out->argument_index,
              out
            );
          }

          break;
        }
      }
    }
  }

  pm_node_destroy(&parser, root);
  pm_parser_free(&parser);

  return out->found;
}
