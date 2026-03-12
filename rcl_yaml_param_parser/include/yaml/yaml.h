// Copyright 2024 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Custom YAML header replacing libyaml.
// Field layout matches the libyaml ABI exactly.

#ifndef YAML_H_
#define YAML_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned char yaml_char_t;

// ── Tags ──────────────────────────────────────────────────────────────────────
#define YAML_NULL_TAG      "tag:yaml.org,2002:null"
#define YAML_BOOL_TAG      "tag:yaml.org,2002:bool"
#define YAML_STR_TAG       "tag:yaml.org,2002:str"
#define YAML_INT_TAG       "tag:yaml.org,2002:int"
#define YAML_FLOAT_TAG     "tag:yaml.org,2002:float"
#define YAML_TIMESTAMP_TAG "tag:yaml.org,2002:timestamp"
#define YAML_SEQ_TAG       "tag:yaml.org,2002:seq"
#define YAML_MAP_TAG       "tag:yaml.org,2002:map"
#define YAML_DEFAULT_SCALAR_TAG   YAML_STR_TAG
#define YAML_DEFAULT_SEQUENCE_TAG YAML_SEQ_TAG
#define YAML_DEFAULT_MAPPING_TAG  YAML_MAP_TAG

// ── Enums ─────────────────────────────────────────────────────────────────────
typedef enum yaml_encoding_e {
  YAML_ANY_ENCODING = 0,
  YAML_UTF8_ENCODING,
  YAML_UTF16LE_ENCODING,
  YAML_UTF16BE_ENCODING
} yaml_encoding_t;

typedef enum yaml_break_e {
  YAML_ANY_BREAK = 0,
  YAML_CR_BREAK,
  YAML_LN_BREAK,
  YAML_CRLN_BREAK
} yaml_break_t;

typedef enum yaml_scalar_style_e {
  YAML_ANY_SCALAR_STYLE = 0,
  YAML_PLAIN_SCALAR_STYLE,
  YAML_SINGLE_QUOTED_SCALAR_STYLE,
  YAML_DOUBLE_QUOTED_SCALAR_STYLE,
  YAML_LITERAL_SCALAR_STYLE,
  YAML_FOLDED_SCALAR_STYLE
} yaml_scalar_style_t;

typedef enum yaml_sequence_style_e {
  YAML_ANY_SEQUENCE_STYLE = 0,
  YAML_BLOCK_SEQUENCE_STYLE,
  YAML_FLOW_SEQUENCE_STYLE
} yaml_sequence_style_t;

typedef enum yaml_mapping_style_e {
  YAML_ANY_MAPPING_STYLE = 0,
  YAML_BLOCK_MAPPING_STYLE,
  YAML_FLOW_MAPPING_STYLE
} yaml_mapping_style_t;

typedef enum yaml_event_type_e {
  YAML_NO_EVENT = 0,
  YAML_STREAM_START_EVENT,
  YAML_STREAM_END_EVENT,
  YAML_DOCUMENT_START_EVENT,
  YAML_DOCUMENT_END_EVENT,
  YAML_ALIAS_EVENT,
  YAML_SCALAR_EVENT,
  YAML_SEQUENCE_START_EVENT,
  YAML_SEQUENCE_END_EVENT,
  YAML_MAPPING_START_EVENT,
  YAML_MAPPING_END_EVENT
} yaml_event_type_t;

typedef enum yaml_token_type_e {
  YAML_NO_TOKEN = 0,
  YAML_STREAM_START_TOKEN,
  YAML_STREAM_END_TOKEN,
  YAML_DOCUMENT_START_TOKEN,
  YAML_DOCUMENT_END_TOKEN,
  YAML_KEY_TOKEN,
  YAML_VALUE_TOKEN,
  YAML_BLOCK_SEQUENCE_START_TOKEN,
  YAML_BLOCK_MAPPING_START_TOKEN,
  YAML_BLOCK_ENTRY_TOKEN,
  YAML_FLOW_SEQUENCE_START_TOKEN,
  YAML_FLOW_SEQUENCE_END_TOKEN,
  YAML_FLOW_MAPPING_START_TOKEN,
  YAML_FLOW_MAPPING_END_TOKEN,
  YAML_FLOW_ENTRY_TOKEN,
  YAML_ALIAS_TOKEN,
  YAML_ANCHOR_TOKEN,
  YAML_TAG_TOKEN,
  YAML_SCALAR_TOKEN
} yaml_token_type_t;

// ── Marks ─────────────────────────────────────────────────────────────────────
typedef struct yaml_mark_s {
  size_t index;
  size_t line;
  size_t column;
} yaml_mark_t;

// ── Version / Tag directives ──────────────────────────────────────────────────
typedef struct yaml_version_directive_s {
  int major;
  int minor;
} yaml_version_directive_t;

typedef struct yaml_tag_directive_s {
  yaml_char_t * handle;
  yaml_char_t * prefix;
} yaml_tag_directive_t;

// ─────────────────────────────────────────────────────────────────────────────
// yaml_event_t
//
// The sub-struct for scalars must have fields in libyaml ABI order:
//   anchor, tag, value, length, plain_implicit, quoted_implicit, style
//
// In C++ an anonymous struct inside a union cannot be copy-assigned from a
// brace-enclosed initializer list (the test code does exactly this).  The
// portable fix is to:
//   1. Give the scalar sub-struct a tag name (yaml_event_scalar_data_s).
//   2. Temporarily close extern "C" before the struct so C++ sees it as a
//      plain aggregate and synthesises the required copy-assignment operator.
//   3. Add a templated assignment operator (C++11) that accepts any aggregate
//      constructible from the same type, enabling `= {...}` syntax.
// ─────────────────────────────────────────────────────────────────────────────

#ifdef __cplusplus
extern "C" {
#endif

// Forward-declare the event type so token can reference it if needed.
typedef struct yaml_event_s yaml_event_t;

#ifdef __cplusplus
}  // close extern "C" temporarily — structs must be C++ aggregates
#endif

// ---------------------------------------------------------------------------
// Sub-struct definitions (outside extern "C" so C++ treats them as aggregates
// and generates implicit copy-assignment operators).
// ---------------------------------------------------------------------------

// libyaml scalar data — field order is ABI.
typedef struct yaml_event_scalar_data_s {
  yaml_char_t *       anchor;
  yaml_char_t *       tag;
  yaml_char_t *       value;
  size_t              length;
  int                 plain_implicit;
  int                 quoted_implicit;
  yaml_scalar_style_t style;

#ifdef __cplusplus
  // Allow: event.data.scalar = {anchor, tag, value, len, pi, qi, style};
  // This works because yaml_event_scalar_data_s is now a named aggregate.
  yaml_event_scalar_data_s & operator=(const yaml_event_scalar_data_s &) = default;
#endif
} yaml_event_scalar_data_t;

typedef struct yaml_event_sequence_start_data_s {
  yaml_char_t *         anchor;
  yaml_char_t *         tag;
  int                   implicit;
  yaml_sequence_style_t style;
} yaml_event_sequence_start_data_t;

typedef struct yaml_event_mapping_start_data_s {
  yaml_char_t *        anchor;
  yaml_char_t *        tag;
  int                  implicit;
  yaml_mapping_style_t style;
} yaml_event_mapping_start_data_t;

typedef struct yaml_event_document_start_data_s {
  yaml_version_directive_t * version_directive;
  struct {
    yaml_tag_directive_t * start;
    yaml_tag_directive_t * end;
  } tag_directives;
  int implicit;
} yaml_event_document_start_data_t;

// ---------------------------------------------------------------------------
// The event struct itself.
// ---------------------------------------------------------------------------
struct yaml_event_s {
  yaml_event_type_t type;
  union {
    struct { yaml_encoding_t encoding; }     stream_start;
    yaml_event_document_start_data_t         document_start;
    struct { int implicit; }                 document_end;
    struct { yaml_char_t * anchor; }         alias;
    yaml_event_scalar_data_t                 scalar;
    yaml_event_sequence_start_data_t         sequence_start;
    yaml_event_mapping_start_data_t          mapping_start;
  } data;
  yaml_mark_t start_mark;
  yaml_mark_t end_mark;
};

// ---------------------------------------------------------------------------
// Token struct.
// ---------------------------------------------------------------------------
typedef struct yaml_token_s {
  yaml_token_type_t type;
  union {
    struct { yaml_encoding_t encoding; } stream_start;
    struct { yaml_char_t * value; }      alias;
    struct { yaml_char_t * value; }      anchor;
    struct {
      yaml_char_t * handle;
      yaml_char_t * suffix;
    } tag;
    struct {
      yaml_char_t *       value;
      size_t              length;
      yaml_scalar_style_t style;
    } scalar;
    struct { int major; int minor; }     version_directive;
    struct {
      yaml_char_t * handle;
      yaml_char_t * prefix;
    } tag_directive;
  } data;
  yaml_mark_t start_mark;
  yaml_mark_t end_mark;
} yaml_token_t;

// ---------------------------------------------------------------------------
// Re-open extern "C" for all function declarations.
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif


/* ── Symbol visibility ───────────────────────────────────────────────────────
 * The rcl_yaml_param_parser library is built with -fvisibility=hidden.
 * The yaml_* functions must remain visible (default visibility) so that:
 *   1. mimick can intercept them via the PLT in unit tests.
 *   2. The linker can resolve them from test binaries that link the DSO.
 * All other internal symbols stay hidden.
 * ─────────────────────────────────────────────────────────────────────────── */
#if defined(_WIN32)
#  if defined(RCL_YAML_PARAM_PARSER_BUILDING_DLL)
#    define YAML_PUBLIC __declspec(dllexport)
#  else
#    define YAML_PUBLIC __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define YAML_PUBLIC __attribute__((visibility("default")))
#else
#  define YAML_PUBLIC
#endif

// ── Parser ────────────────────────────────────────────────────────────────────
typedef struct yaml_parser_s {
  FILE *         file;
  yaml_char_t *  string_buffer;
  size_t         string_buffer_size;
  size_t         string_buffer_pos;
  size_t         string_buffer_length;

  yaml_char_t *  buffer;
  size_t         buffer_size;
  size_t         buffer_pos;

  size_t         index;
  size_t         line;
  size_t         column;

  yaml_encoding_t encoding;

  int eof;
  int stream_start_parsed;
  int stream_end_parsed;

  yaml_token_t  token;
  int           token_available;

  yaml_event_t  event;
  int           event_available;

  int           error;
  yaml_mark_t   problem_mark;
  char *        problem;
  char *        context;
  yaml_mark_t   context_mark;
} yaml_parser_t;

YAML_PUBLIC int  yaml_parser_initialize(yaml_parser_t * parser);
YAML_PUBLIC void yaml_parser_delete(yaml_parser_t * parser);
YAML_PUBLIC void yaml_parser_set_input_file(yaml_parser_t * parser, FILE * file);
YAML_PUBLIC void yaml_parser_set_input_string(
  yaml_parser_t * parser, const unsigned char * input, size_t size);
YAML_PUBLIC int  yaml_parser_parse(yaml_parser_t * parser, yaml_event_t * event);
YAML_PUBLIC void yaml_event_delete(yaml_event_t * event);

// ── Emitter ───────────────────────────────────────────────────────────────────
typedef int (*yaml_write_handler_t)(void * data, unsigned char * buffer, size_t size);

typedef struct yaml_emitter_s {
  yaml_write_handler_t write_handler;
  void *               write_handler_data;

  yaml_char_t * output_buffer;
  size_t        output_buffer_size;
  size_t        output_buffer_pos;

  yaml_char_t * string_buffer;
  size_t        string_buffer_size;
  size_t        string_buffer_pos;

  yaml_encoding_t encoding;
  int             canonical;
  int             best_indent;
  int             best_width;
  int             unicode;
  int             line_break;   // int so -1 sentinel from libyaml works

  int             state;
  int             indent;

  // Non-NULL default set by yaml_emitter_initialize() so callers doing
  // strlen(emitter.problem) on the error path never segfault.
  const char *  problem;
  size_t        problem_offset;
  int           failed;
} yaml_emitter_t;

YAML_PUBLIC int  yaml_emitter_initialize(yaml_emitter_t * emitter);
YAML_PUBLIC void yaml_emitter_delete(yaml_emitter_t * emitter);
YAML_PUBLIC void yaml_emitter_set_output(
  yaml_emitter_t * emitter, yaml_write_handler_t handler, void * data);
YAML_PUBLIC void yaml_emitter_set_encoding(yaml_emitter_t * emitter, yaml_encoding_t encoding);
YAML_PUBLIC void yaml_emitter_set_width(yaml_emitter_t * emitter, int width);
YAML_PUBLIC void yaml_emitter_set_break(yaml_emitter_t * emitter, int line_break);
YAML_PUBLIC int  yaml_emitter_emit(yaml_emitter_t * emitter, yaml_event_t * event);

// ── Event initializers ────────────────────────────────────────────────────────
YAML_PUBLIC int yaml_stream_start_event_initialize(
  yaml_event_t * event, yaml_encoding_t encoding);
YAML_PUBLIC int yaml_stream_end_event_initialize(yaml_event_t * event);
YAML_PUBLIC int yaml_document_start_event_initialize(
  yaml_event_t * event,
  yaml_version_directive_t * version_directive,
  yaml_tag_directive_t * tag_directives_start,
  yaml_tag_directive_t * tag_directives_end,
  int implicit);
YAML_PUBLIC int yaml_document_end_event_initialize(yaml_event_t * event, int implicit);
YAML_PUBLIC int yaml_alias_event_initialize(yaml_event_t * event, yaml_char_t * anchor);
YAML_PUBLIC int yaml_scalar_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  yaml_char_t * value,
  int length,
  int plain_implicit,
  int quoted_implicit,
  yaml_scalar_style_t style);
YAML_PUBLIC int yaml_sequence_start_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  int implicit,
  yaml_sequence_style_t style);
YAML_PUBLIC int yaml_sequence_end_event_initialize(yaml_event_t * event);
YAML_PUBLIC int yaml_mapping_start_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  int implicit,
  yaml_mapping_style_t style);
YAML_PUBLIC int yaml_mapping_end_event_initialize(yaml_event_t * event);

#ifdef __cplusplus
}
#endif

#endif  /* YAML_H_ */