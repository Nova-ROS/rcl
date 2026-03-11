#ifndef YAML_H_
#define YAML_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define YAML_STR_TAG "tag:yaml.org,2002:str"

typedef unsigned char yaml_char_t;

typedef enum yaml_scalar_style_e
{
  YAML_ANY_SCALAR_STYLE = 0,
  YAML_PLAIN_SCALAR_STYLE = 1,
  YAML_SINGLE_QUOTED_SCALAR_STYLE = 2,
  YAML_DOUBLE_QUOTED_SCALAR_STYLE = 3,
  YAML_LITERAL_SCALAR_STYLE = 4,
  YAML_FOLDED_SCALAR_STYLE = 5
} yaml_scalar_style_t;

typedef enum yaml_sequence_style_e
{
  YAML_ANY_SEQUENCE_STYLE = 0,
  YAML_BLOCK_SEQUENCE_STYLE = 1,
  YAML_FLOW_SEQUENCE_STYLE = 2
} yaml_sequence_style_t;

typedef enum yaml_mapping_style_e
{
  YAML_ANY_MAPPING_STYLE = 0,
  YAML_BLOCK_MAPPING_STYLE = 1,
  YAML_FLOW_MAPPING_STYLE = 2
} yaml_mapping_style_t;

typedef enum yaml_event_type_e
{
  YAML_NO_EVENT = 0,
  YAML_STREAM_START_EVENT = 1,
  YAML_STREAM_END_EVENT = 2,
  YAML_DOCUMENT_START_EVENT = 3,
  YAML_DOCUMENT_END_EVENT = 4,
  YAML_MAPPING_START_EVENT = 5,
  YAML_MAPPING_END_EVENT = 6,
  YAML_SEQUENCE_START_EVENT = 7,
  YAML_SEQUENCE_END_EVENT = 8,
  YAML_SCALAR_EVENT = 9,
  YAML_ALIAS_EVENT = 10
} yaml_event_type_t;

typedef struct yaml_mark_s
{
  size_t index;
  size_t line;
  size_t column;
} yaml_mark_t;

typedef struct yaml_token_s
{
  yaml_event_type_t type;
  yaml_mark_t start_mark;
  yaml_mark_t end_mark;
  union {
    struct {
      yaml_char_t * value;
      size_t length;
      yaml_scalar_style_t style;
      yaml_char_t * tag;
    } scalar;
  } data;
} yaml_token_t;

typedef enum yaml_encoding_e
{
  YAML_UTF8_ENCODING,
  YAML_UTF16LE_ENCODING,
  YAML_UTF16BE_ENCODING
} yaml_encoding_t;

typedef struct yaml_event_s
{
  yaml_event_type_t type;
  yaml_mark_t start_mark;
  yaml_mark_t end_mark;
  union {
    struct {
      yaml_char_t * value;
      size_t length;
      yaml_scalar_style_t style;
      yaml_char_t * tag;
      yaml_char_t * anchor;
    } scalar;
    struct {
      yaml_char_t * anchor;
      yaml_char_t * tag;
      bool implicit;
    } mapping_start;
    struct {
      yaml_char_t * anchor;
      yaml_char_t * tag;
      bool implicit;
    } sequence_start;
    struct {
      bool implicit;
    } document_start;
    struct {
      bool implicit;
    } document_end;
    struct {
      yaml_encoding_t encoding;
    } stream_start;
    struct {
      yaml_char_t * anchor;
    } alias;
  } data;
} yaml_event_t;

typedef enum yaml_break_e
{
  YAML_LN_BREAK,
  YAML_CR_BREAK,
  YAML_CRLN_BREAK
} yaml_break_t;

typedef struct yaml_parser_s
{
  FILE * file;
  yaml_char_t * string_buffer;
  size_t string_buffer_size;
  size_t string_buffer_pos;
  size_t string_buffer_length;

  yaml_char_t * buffer;
  size_t buffer_size;
  size_t buffer_pos;

  size_t index;
  size_t line;
  size_t column;

  yaml_encoding_t encoding;

  bool eof;
  bool stream_start_parsed;
  bool stream_end_parsed;

  yaml_token_t token;
  bool token_available;

  yaml_event_t event;
  bool event_available;

  int error;
  yaml_mark_t error_mark;
  char * problem;
} yaml_parser_t;

int yaml_parser_initialize(yaml_parser_t * parser);
void yaml_parser_delete(yaml_parser_t * parser);

void yaml_parser_set_input_file(yaml_parser_t * parser, FILE * file);
void yaml_parser_set_input_string(
  yaml_parser_t * parser,
  const unsigned char * input,
  size_t size);

int yaml_parser_parse(yaml_parser_t * parser, yaml_event_t * event);
void yaml_event_delete(yaml_event_t * event);

/* ---- Emitter ---- */

typedef struct yaml_emitter_s
{
  FILE * file;
  yaml_char_t * string_buffer;
  size_t string_buffer_size;
  size_t string_buffer_pos;
  yaml_char_t * output_buffer;
  size_t output_buffer_size;
  size_t output_buffer_pos;

  int state;
  yaml_mark_t mark;

  int indent;
  bool flow_level;

  int line_length;
  int line_break;

  yaml_encoding_t encoding;

  bool canonical;
  bool quoted_implicit;

  void * write_handler_data;
  int (*write_handler)(void *, uint8_t *, size_t);

  /* Non-NULL default set by yaml_emitter_initialize() so that callers
   * doing strlen(emitter.problem) on the error path never segfault. */
  const char * problem;
  size_t problem_offset;
  int failed;
} yaml_emitter_t;

int yaml_emitter_initialize(yaml_emitter_t * emitter);
void yaml_emitter_delete(yaml_emitter_t * emitter);
void yaml_emitter_set_output(
  yaml_emitter_t * emitter,
  int (*handler)(void *, uint8_t *, size_t),
  void * data);
void yaml_emitter_set_encoding(yaml_emitter_t * emitter, yaml_encoding_t encoding);
void yaml_emitter_set_width(yaml_emitter_t * emitter, int width);
/* Note: libyaml passes -1 as a sentinel "no forced break" value,
 * so the parameter is int rather than yaml_break_t. */
void yaml_emitter_set_break(yaml_emitter_t * emitter, int line_break);

int yaml_emitter_emit(yaml_emitter_t * emitter, yaml_event_t * event);

int yaml_stream_start_event_initialize(yaml_event_t * event, yaml_encoding_t encoding);
int yaml_stream_end_event_initialize(yaml_event_t * event);
int yaml_document_start_event_initialize(
  yaml_event_t * event,
  yaml_char_t * version_directive,
  yaml_char_t ** tag_directives,
  int tags_amount,
  int implicit);
int yaml_document_end_event_initialize(yaml_event_t * event, int implicit);
int yaml_sequence_start_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  int implicit,
  yaml_sequence_style_t style);
int yaml_sequence_end_event_initialize(yaml_event_t * event);
int yaml_mapping_start_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  int implicit,
  yaml_mapping_style_t style);
int yaml_mapping_end_event_initialize(yaml_event_t * event);
int yaml_scalar_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  yaml_char_t * value,
  int length,
  int plain_implicit,
  int quoted_implicit,
  yaml_scalar_style_t style);

#ifdef __cplusplus
}
#endif

#endif  /* YAML_H_ */