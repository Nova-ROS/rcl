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

// Custom YAML parser/emitter implementation replacing libyaml dependency.
// Supports the subset of YAML used by rcl_yaml_param_parser and rcl.

#include "yaml.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#define YAML_BUFFER_SIZE 4096

/* ======================================================================
 * Internal helpers
 * ====================================================================== */

static void yaml_parser_internal_init(yaml_parser_t * parser)
{
  memset(parser, 0, sizeof(yaml_parser_t));
  parser->buffer = (yaml_char_t *)malloc(YAML_BUFFER_SIZE);
  parser->buffer_size = YAML_BUFFER_SIZE;
  parser->encoding = YAML_UTF8_ENCODING;
}

/* ======================================================================
 * Parser lifecycle
 * ====================================================================== */

int yaml_parser_initialize(yaml_parser_t * parser)
{
  if (parser == NULL) {
    return 0;
  }
  yaml_parser_internal_init(parser);
  return 1;
}

void yaml_parser_delete(yaml_parser_t * parser)
{
  if (parser == NULL) {
    return;
  }
  if (parser->buffer != NULL) {
    free(parser->buffer);
    parser->buffer = NULL;
  }
  if (parser->string_buffer != NULL) {
    free(parser->string_buffer);
    parser->string_buffer = NULL;
  }
  memset(parser, 0, sizeof(yaml_parser_t));
}

void yaml_parser_set_input_file(yaml_parser_t * parser, FILE * file)
{
  if (parser == NULL || file == NULL) {
    return;
  }
  parser->file = file;
  parser->string_buffer = NULL;
  parser->string_buffer_size = 0;
  parser->string_buffer_pos = 0;
  parser->string_buffer_length = 0;
  parser->eof = false;
  parser->stream_start_parsed = false;
  parser->stream_end_parsed = false;
  // Reset position tracking
  parser->buffer_pos = 0;
  if (parser->buffer) {
    parser->buffer[0] = '\0';
  }
}

void yaml_parser_set_input_string(
  yaml_parser_t * parser,
  const unsigned char * input,
  size_t size)
{
  if (parser == NULL || input == NULL) {
    return;
  }
  parser->file = NULL;
  if (parser->string_buffer != NULL) {
    free(parser->string_buffer);
  }
  parser->string_buffer = (yaml_char_t *)malloc(size + 1);
  memcpy(parser->string_buffer, input, size);
  parser->string_buffer[size] = '\0';
  parser->string_buffer_size = size + 1;
  parser->string_buffer_pos = 0;
  parser->string_buffer_length = size;
  parser->eof = false;
  parser->stream_start_parsed = false;
  parser->stream_end_parsed = false;
  parser->buffer_pos = 0;
  if (parser->buffer) {
    parser->buffer[0] = '\0';
  }
}

/* ======================================================================
 * Internal I/O
 * ====================================================================== */

static int yaml_parser_fill_buffer(yaml_parser_t * parser)
{
  if (parser->eof) {
    return 0;
  }

  size_t bytes_read = 0;

  if (parser->file != NULL) {
    bytes_read = fread(parser->buffer, 1, parser->buffer_size - 1, parser->file);
  } else if (parser->string_buffer != NULL) {
    if (parser->string_buffer_pos >= parser->string_buffer_length) {
      parser->eof = true;
      return 0;
    }
    size_t remaining = parser->string_buffer_length - parser->string_buffer_pos;
    size_t to_read =
      (remaining < parser->buffer_size - 1) ? remaining : parser->buffer_size - 1;
    memcpy(parser->buffer, parser->string_buffer + parser->string_buffer_pos, to_read);
    bytes_read = to_read;
    parser->string_buffer_pos += to_read;
  }

  if (bytes_read == 0) {
    parser->eof = true;
    parser->buffer[0] = '\0';
    return 0;
  }

  parser->buffer[bytes_read] = '\0';
  parser->buffer_pos = 0;
  return (int)bytes_read;
}

static yaml_char_t yaml_parser_peek(yaml_parser_t * parser)
{
  size_t buf_len = strlen((char *)parser->buffer);
  if (parser->buffer_pos >= buf_len) {
    if (!parser->eof) {
      yaml_parser_fill_buffer(parser);
    }
    if (parser->eof) {
      return '\0';
    }
  }
  return parser->buffer[parser->buffer_pos];
}

static yaml_char_t yaml_parser_advance(yaml_parser_t * parser)
{
  yaml_char_t ch = yaml_parser_peek(parser);
  if (ch != '\0') {
    parser->buffer_pos++;
    parser->index++;
    if (ch == '\n') {
      parser->line++;
      parser->column = 0;
    } else {
      parser->column++;
    }
  }
  return ch;
}

// Skip spaces/tabs/carriage-returns (but NOT newlines)
static void yaml_parser_skip_spaces(yaml_parser_t * parser)
{
  yaml_char_t ch;
  while ((ch = yaml_parser_peek(parser)) != '\0') {
    if (ch == ' ' || ch == '\t' || ch == '\r') {
      yaml_parser_advance(parser);
    } else {
      break;
    }
  }
}

// Skip whitespace including newlines and comments
static void yaml_parser_skip_blank_lines_and_comments(yaml_parser_t * parser)
{
  yaml_char_t ch;
  while ((ch = yaml_parser_peek(parser)) != '\0') {
    if (ch == '#') {
      // Skip to end of line
      while ((ch = yaml_parser_peek(parser)) != '\0' && ch != '\n') {
        yaml_parser_advance(parser);
      }
    } else if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') {
      yaml_parser_advance(parser);
    } else {
      break;
    }
  }
}

static bool is_flow_indicator(yaml_char_t ch)
{
  return ch == ',' || ch == '[' || ch == ']' || ch == '{' || ch == '}';
}

/* ======================================================================
 * Scalar reading
 * ====================================================================== */

static yaml_char_t * yaml_read_quoted_scalar(
  yaml_parser_t * parser,
  yaml_char_t quote_char,
  size_t * out_length,
  yaml_scalar_style_t * out_style)
{
  *out_style = (quote_char == '\'') ?
    YAML_SINGLE_QUOTED_SCALAR_STYLE : YAML_DOUBLE_QUOTED_SCALAR_STYLE;

  size_t cap = 64;
  size_t len = 0;
  yaml_char_t * value = (yaml_char_t *)malloc(cap);
  if (value == NULL) {
    return NULL;
  }

  yaml_char_t ch;
  while ((ch = yaml_parser_peek(parser)) != '\0') {
    if (ch == quote_char) {
      yaml_parser_advance(parser);
      // Check for escaped quote (doubled)
      if (yaml_parser_peek(parser) == quote_char) {
        value[len++] = quote_char;
        yaml_parser_advance(parser);
      } else {
        break;
      }
    } else if (ch == '\\' && quote_char == '"') {
      yaml_parser_advance(parser);
      ch = yaml_parser_peek(parser);
      switch (ch) {
        case 'n': value[len++] = '\n'; break;
        case 't': value[len++] = '\t'; break;
        case 'r': value[len++] = '\r'; break;
        case '\\': value[len++] = '\\'; break;
        case '"': value[len++] = '"'; break;
        default: value[len++] = ch; break;
      }
      yaml_parser_advance(parser);
    } else if (ch == '\n' && quote_char == '\'') {
      // Single-quoted scalar ends at newline
      break;
    } else {
      value[len++] = ch;
      yaml_parser_advance(parser);
    }

    if (len >= cap - 1) {
      cap *= 2;
      yaml_char_t * tmp = (yaml_char_t *)realloc(value, cap);
      if (tmp == NULL) {
        free(value);
        return NULL;
      }
      value = tmp;
    }
  }

  value[len] = '\0';
  *out_length = len;
  return value;
}

static yaml_char_t * yaml_read_block_scalar(
  yaml_parser_t * parser,
  yaml_char_t indicator,
  size_t * out_length,
  yaml_scalar_style_t * out_style)
{
  *out_style = (indicator == '|') ? YAML_LITERAL_SCALAR_STYLE : YAML_FOLDED_SCALAR_STYLE;

  // Skip rest of the header line
  yaml_char_t ch;
  while ((ch = yaml_parser_peek(parser)) != '\0' && ch != '\n') {
    yaml_parser_advance(parser);
  }
  if (ch == '\n') {
    yaml_parser_advance(parser);
  }

  size_t cap = 256;
  size_t len = 0;
  yaml_char_t * value = (yaml_char_t *)malloc(cap);
  if (value == NULL) {
    return NULL;
  }

  while ((ch = yaml_parser_peek(parser)) != '\0') {
    if (ch == '\n') {
      yaml_parser_advance(parser);
      if (yaml_parser_peek(parser) == '\n' || parser->column < 2) {
        break;
      }
      value[len++] = '\n';
    } else {
      value[len++] = ch;
      yaml_parser_advance(parser);
    }

    if (len >= cap - 1) {
      cap *= 2;
      yaml_char_t * tmp = (yaml_char_t *)realloc(value, cap);
      if (tmp == NULL) {
        free(value);
        return NULL;
      }
      value = tmp;
    }
  }

  value[len] = '\0';
  *out_length = len;
  return value;
}

static yaml_char_t * yaml_read_plain_scalar(
  yaml_parser_t * parser,
  size_t * out_length,
  yaml_scalar_style_t * out_style)
{
  *out_style = YAML_PLAIN_SCALAR_STYLE;

  size_t cap = 64;
  size_t len = 0;
  yaml_char_t * value = (yaml_char_t *)malloc(cap);
  if (value == NULL) {
    return NULL;
  }

  yaml_char_t ch;
  while ((ch = yaml_parser_peek(parser)) != '\0') {
    if (ch == '#' || ch == '\n' || ch == '\r') {
      break;
    }
    // Stop at ':' followed by whitespace/end (key separator)
    if (ch == ':') {
      size_t next_pos = parser->buffer_pos + 1;
      yaml_char_t next = '\0';
      if (next_pos < strlen((char *)parser->buffer)) {
        next = parser->buffer[next_pos];
      }
      if (next == ' ' || next == '\n' || next == '\t' || next == '\0' || next == '#') {
        break;
      }
    }
    if (is_flow_indicator(ch)) {
      break;
    }

    value[len++] = ch;
    yaml_parser_advance(parser);

    if (len >= cap - 1) {
      cap *= 2;
      yaml_char_t * tmp = (yaml_char_t *)realloc(value, cap);
      if (tmp == NULL) {
        free(value);
        return NULL;
      }
      value = tmp;
    }
  }

  // Trim trailing spaces
  while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t')) {
    len--;
  }
  value[len] = '\0';
  *out_length = len;
  return value;
}

/* Read a scalar value from the current position. */
static yaml_char_t * yaml_read_scalar(
  yaml_parser_t * parser,
  size_t * out_length,
  yaml_scalar_style_t * out_style)
{
  yaml_parser_skip_spaces(parser);

  yaml_char_t ch = yaml_parser_peek(parser);
  if (ch == '\0') {
    return NULL;
  }

  if (ch == '\'' || ch == '"') {
    yaml_parser_advance(parser);
    return yaml_read_quoted_scalar(parser, ch, out_length, out_style);
  }

  if (ch == '|' || ch == '>') {
    yaml_parser_advance(parser);
    return yaml_read_block_scalar(parser, ch, out_length, out_style);
  }

  return yaml_read_plain_scalar(parser, out_length, out_style);
}

/* Optionally scan a YAML tag (!str, etc.) at the current position. */
static yaml_char_t * yaml_scan_tag(yaml_parser_t * parser)
{
  yaml_parser_skip_spaces(parser);
  yaml_char_t ch = yaml_parser_peek(parser);
  if (ch != '!') {
    return NULL;
  }

  yaml_parser_advance(parser);

  size_t cap = 32;
  size_t len = 0;
  yaml_char_t * tag = (yaml_char_t *)malloc(cap);
  if (tag == NULL) {
    return NULL;
  }

  while ((ch = yaml_parser_peek(parser)) != '\0' && !isspace(ch)) {
    tag[len++] = ch;
    yaml_parser_advance(parser);
    if (len >= cap - 1) {
      cap *= 2;
      yaml_char_t * tmp = (yaml_char_t *)realloc(tag, cap);
      if (tmp == NULL) {
        free(tag);
        return NULL;
      }
      tag = tmp;
    }
  }
  tag[len] = '\0';
  return tag;
}

/* ======================================================================
 * Event helpers
 * ====================================================================== */

static void fill_mark(yaml_parser_t * parser, yaml_mark_t * mark)
{
  mark->index = parser->index;
  mark->line = parser->line;
  mark->column = parser->column;
}

static void emit_empty_scalar(yaml_parser_t * parser, yaml_event_t * event)
{
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_SCALAR_EVENT;
  fill_mark(parser, &event->start_mark);
  event->end_mark = event->start_mark;
  event->data.scalar.value = (yaml_char_t *)malloc(1);
  event->data.scalar.value[0] = '\0';
  event->data.scalar.length = 0;
  event->data.scalar.style = YAML_PLAIN_SCALAR_STYLE;
  event->data.scalar.tag = NULL;
}

/* ======================================================================
 * Main parser: yaml_parser_parse
 * ====================================================================== */

int yaml_parser_parse(yaml_parser_t * parser, yaml_event_t * event)
{
  if (parser == NULL || event == NULL) {
    return 0;
  }

  memset(event, 0, sizeof(yaml_event_t));

  if (parser->stream_start_parsed && parser->stream_end_parsed) {
    return 0;
  }

  /* First call: emit STREAM_START */
  if (!parser->stream_start_parsed) {
    // Ensure buffer is populated
    if (parser->buffer == NULL || parser->buffer[0] == '\0') {
      yaml_parser_fill_buffer(parser);
    }
    parser->stream_start_parsed = true;
    event->type = YAML_STREAM_START_EVENT;
    fill_mark(parser, &event->start_mark);
    event->end_mark = event->start_mark;
    return 1;
  }

  yaml_parser_skip_blank_lines_and_comments(parser);

  /* EOF -> STREAM_END */
  if (parser->eof || yaml_parser_peek(parser) == '\0') {
    parser->stream_end_parsed = true;
    event->type = YAML_STREAM_END_EVENT;
    fill_mark(parser, &event->start_mark);
    event->end_mark = event->start_mark;
    return 1;
  }

  yaml_char_t ch = yaml_parser_peek(parser);

  /* --- Document start marker: "---" --- */
  if (ch == '-') {
    size_t next1_pos = parser->buffer_pos + 1;
    size_t next2_pos = parser->buffer_pos + 2;
    size_t buf_len = strlen((char *)parser->buffer);
    yaml_char_t n1 = (next1_pos < buf_len) ? parser->buffer[next1_pos] : '\0';
    yaml_char_t n2 = (next2_pos < buf_len) ? parser->buffer[next2_pos] : '\0';

    if (n1 == '-' && n2 == '-') {
      // Consume "---"
      yaml_parser_advance(parser);
      yaml_parser_advance(parser);
      yaml_parser_advance(parser);
      yaml_parser_skip_spaces(parser);
      // Skip rest of line if comment or newline
      yaml_char_t after = yaml_parser_peek(parser);
      if (after == '\n' || after == '#' || after == '\0') {
        while ((after = yaml_parser_peek(parser)) != '\0' && after != '\n') {
          yaml_parser_advance(parser);
        }
      }
      event->type = YAML_DOCUMENT_START_EVENT;
      fill_mark(parser, &event->start_mark);
      event->end_mark = event->start_mark;
      event->data.document_start.implicit = false;
      return 1;
    }

    /* Sequence item: "- " */
    if (n1 == ' ' || n1 == '\n' || n1 == '\t' || n1 == '\0') {
      yaml_parser_advance(parser);  // consume '-'
      yaml_parser_skip_spaces(parser);
      event->type = YAML_SEQUENCE_START_EVENT;
      fill_mark(parser, &event->start_mark);
      event->end_mark = event->start_mark;
      event->data.sequence_start.implicit = true;
      return 1;
    }
  }

  /* --- Alias: "*anchor" --- */
  if (ch == '*') {
    yaml_parser_advance(parser);
    event->type = YAML_ALIAS_EVENT;
    fill_mark(parser, &event->start_mark);
    event->end_mark = event->start_mark;
    return 1;
  }

  /* --- Mapping end via ':' alone (empty value) --- */
  if (ch == ':') {
    size_t next_pos = parser->buffer_pos + 1;
    size_t buf_len = strlen((char *)parser->buffer);
    yaml_char_t next = (next_pos < buf_len) ? parser->buffer[next_pos] : '\0';
    if (next == ' ' || next == '\n' || next == '\t' || next == '\0' || next == '#') {
      yaml_parser_advance(parser);
      yaml_parser_skip_spaces(parser);
      yaml_char_t after = yaml_parser_peek(parser);
      if (after == '\n' || after == '#' || after == '\0') {
        emit_empty_scalar(parser, event);
        return 1;
      }
      event->type = YAML_MAPPING_START_EVENT;
      fill_mark(parser, &event->start_mark);
      event->end_mark = event->start_mark;
      event->data.mapping_start.implicit = true;
      return 1;
    }
  }

  /* --- Read a scalar --- */
  size_t length = 0;
  yaml_scalar_style_t style = YAML_PLAIN_SCALAR_STYLE;
  yaml_char_t * value = yaml_read_scalar(parser, &length, &style);

  if (value == NULL || length == 0) {
    if (value != NULL) {
      free(value);
    }
    // Try to advance past stuck character
    if (yaml_parser_peek(parser) != '\0') {
      yaml_parser_advance(parser);
    }
    return 0;
  }

  /* Optionally scan tag after value */
  yaml_parser_skip_spaces(parser);
  yaml_char_t * tag = yaml_scan_tag(parser);

  yaml_parser_skip_spaces(parser);
  ch = yaml_parser_peek(parser);

  /* Check whether this scalar is followed by ':' (making it a mapping key) */
  if (ch == ':') {
    size_t next_pos = parser->buffer_pos + 1;
    size_t buf_len = strlen((char *)parser->buffer);
    yaml_char_t next = (next_pos < buf_len) ? parser->buffer[next_pos] : '\0';
    if (next == ' ' || next == '\n' || next == '\t' || next == '\0' || next == '#') {
      yaml_parser_advance(parser);  // consume ':'
      yaml_parser_skip_spaces(parser);
      yaml_char_t after = yaml_parser_peek(parser);
      if (after == '\n' || after == '#' || after == '\0') {
        // Value will be empty — return the key scalar first
        event->type = YAML_SCALAR_EVENT;
        fill_mark(parser, &event->start_mark);
        event->end_mark = event->start_mark;
        event->data.scalar.value = value;
        event->data.scalar.length = length;
        event->data.scalar.style = style;
        event->data.scalar.tag = tag;
        return 1;
      }
      // Non-empty value follows — the scalar is a mapping start key
      event->type = YAML_MAPPING_START_EVENT;
      fill_mark(parser, &event->start_mark);
      event->end_mark = event->start_mark;
      event->data.mapping_start.implicit = true;
      free(value);
      if (tag != NULL) {
        free(tag);
      }
      return 1;
    }
  }

  /* Plain scalar */
  event->type = YAML_SCALAR_EVENT;
  fill_mark(parser, &event->start_mark);
  event->end_mark = event->start_mark;
  event->data.scalar.value = value;
  event->data.scalar.length = length;
  event->data.scalar.style = style;
  event->data.scalar.tag = tag;
  return 1;
}

/* ======================================================================
 * yaml_event_delete
 * ====================================================================== */

void yaml_event_delete(yaml_event_t * event)
{
  if (event == NULL) {
    return;
  }

  if (event->type == YAML_SCALAR_EVENT) {
    if (event->data.scalar.value != NULL) {
      free(event->data.scalar.value);
      event->data.scalar.value = NULL;
    }
    if (event->data.scalar.tag != NULL) {
      free(event->data.scalar.tag);
      event->data.scalar.tag = NULL;
    }
    if (event->data.scalar.anchor != NULL) {
      free(event->data.scalar.anchor);
      event->data.scalar.anchor = NULL;
    }
  } else if (event->type == YAML_MAPPING_START_EVENT) {
    if (event->data.mapping_start.anchor != NULL) {
      free(event->data.mapping_start.anchor);
      event->data.mapping_start.anchor = NULL;
    }
    if (event->data.mapping_start.tag != NULL) {
      free(event->data.mapping_start.tag);
      event->data.mapping_start.tag = NULL;
    }
  } else if (event->type == YAML_SEQUENCE_START_EVENT) {
    if (event->data.sequence_start.anchor != NULL) {
      free(event->data.sequence_start.anchor);
      event->data.sequence_start.anchor = NULL;
    }
    if (event->data.sequence_start.tag != NULL) {
      free(event->data.sequence_start.tag);
      event->data.sequence_start.tag = NULL;
    }
  } else if (event->type == YAML_ALIAS_EVENT) {
    if (event->data.alias.anchor != NULL) {
      free(event->data.alias.anchor);
      event->data.alias.anchor = NULL;
    }
  }

  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_NO_EVENT;
}

/* ======================================================================
 * Emitter — internal write helper
 * ====================================================================== */

static int yaml_emitter_write(
  yaml_emitter_t * emitter,
  const yaml_char_t * data,
  size_t length)
{
  if (emitter->write_handler) {
    return emitter->write_handler(emitter->write_handler_data, (uint8_t *)data, length);
  }
  return 0;
}

/* ======================================================================
 * Emitter lifecycle
 * ====================================================================== */

int yaml_emitter_initialize(yaml_emitter_t * emitter)
{
  if (emitter == NULL) {
    return 0;
  }
  memset(emitter, 0, sizeof(yaml_emitter_t));

  emitter->output_buffer = (yaml_char_t *)malloc(YAML_BUFFER_SIZE);
  if (emitter->output_buffer == NULL) {
    return 0;
  }
  emitter->output_buffer_size = YAML_BUFFER_SIZE;

  emitter->string_buffer = (yaml_char_t *)malloc(YAML_BUFFER_SIZE);
  if (emitter->string_buffer == NULL) {
    free(emitter->output_buffer);
    emitter->output_buffer = NULL;
    return 0;
  }
  emitter->string_buffer_size = YAML_BUFFER_SIZE;

  emitter->line_break = YAML_LN_BREAK;
  emitter->encoding = YAML_UTF8_ENCODING;
  /* Provide a non-NULL default so callers doing strlen(emitter.problem)
   * on error-path do not segfault when we never set a real problem string. */
  emitter->problem = "unknown emitter error";
  return 1;
}

void yaml_emitter_delete(yaml_emitter_t * emitter)
{
  if (emitter == NULL) {
    return;
  }
  if (emitter->output_buffer != NULL) {
    free(emitter->output_buffer);
    emitter->output_buffer = NULL;
  }
  if (emitter->string_buffer != NULL) {
    free(emitter->string_buffer);
    emitter->string_buffer = NULL;
  }
  memset(emitter, 0, sizeof(yaml_emitter_t));
}

void yaml_emitter_set_output(
  yaml_emitter_t * emitter,
  int (*handler)(void *, uint8_t *, size_t),
  void * data)
{
  if (emitter == NULL) {
    return;
  }
  emitter->write_handler = handler;
  emitter->write_handler_data = data;
}

void yaml_emitter_set_encoding(yaml_emitter_t * emitter, yaml_encoding_t encoding)
{
  if (emitter != NULL) {
    emitter->encoding = encoding;
  }
}

void yaml_emitter_set_width(yaml_emitter_t * emitter, int width)
{
  if (emitter != NULL) {
    emitter->line_length = width;
  }
}

void yaml_emitter_set_break(yaml_emitter_t * emitter, int line_break)
{
  if (emitter != NULL) {
    emitter->line_break = line_break;
  }
}

/* ======================================================================
 * Emitter: per-event dispatch
 * ====================================================================== */

static int yaml_emitter_emit_scalar(yaml_emitter_t * emitter, yaml_event_t * event)
{
  /* NULL value is treated as an empty scalar — do not fail. */
  const yaml_char_t * val =
    (event->data.scalar.value != NULL) ? event->data.scalar.value :
    (const yaml_char_t *)"";

  size_t len = event->data.scalar.length;
  if (len == 0 && event->data.scalar.value != NULL) {
    len = strlen((const char *)event->data.scalar.value);
  }

  if (event->data.scalar.style == YAML_DOUBLE_QUOTED_SCALAR_STYLE) {
    yaml_emitter_write(emitter, (const yaml_char_t *)"\"", 1);
    if (len > 0) {
      yaml_emitter_write(emitter, val, len);
    }
    yaml_emitter_write(emitter, (const yaml_char_t *)"\"", 1);
  } else if (event->data.scalar.style == YAML_SINGLE_QUOTED_SCALAR_STYLE) {
    yaml_emitter_write(emitter, (const yaml_char_t *)"'", 1);
    if (len > 0) {
      yaml_emitter_write(emitter, val, len);
    }
    yaml_emitter_write(emitter, (const yaml_char_t *)"'", 1);
  } else {
    if (len > 0) {
      yaml_emitter_write(emitter, val, len);
    }
  }
  return 1;
}

int yaml_emitter_emit(yaml_emitter_t * emitter, yaml_event_t * event)
{
  if (emitter == NULL || event == NULL) {
    return 0;
  }

  switch (event->type) {
    case YAML_STREAM_START_EVENT:
      return 1;  // nothing to write
    case YAML_STREAM_END_EVENT:
      return 1;
    case YAML_DOCUMENT_START_EVENT:
      return 1;
    case YAML_DOCUMENT_END_EVENT:
      return 1;
    case YAML_MAPPING_START_EVENT:
      yaml_emitter_write(emitter, (const yaml_char_t *)"{", 1);
      yaml_emitter_write(emitter, (const yaml_char_t *)" ", 1);
      return 1;
    case YAML_MAPPING_END_EVENT:
      yaml_emitter_write(emitter, (const yaml_char_t *)"}", 1);
      yaml_emitter_write(emitter, (const yaml_char_t *)"\n", 1);
      return 1;
    case YAML_SEQUENCE_START_EVENT:
      yaml_emitter_write(emitter, (const yaml_char_t *)"[", 1);
      yaml_emitter_write(emitter, (const yaml_char_t *)" ", 1);
      return 1;
    case YAML_SEQUENCE_END_EVENT:
      yaml_emitter_write(emitter, (const yaml_char_t *)"]", 1);
      yaml_emitter_write(emitter, (const yaml_char_t *)"\n", 1);
      return 1;
    case YAML_SCALAR_EVENT:
      return yaml_emitter_emit_scalar(emitter, event);
    default:
      return 0;
  }
}

/* ======================================================================
 * Event initializers (used by emitter callers)
 * ====================================================================== */

int yaml_stream_start_event_initialize(yaml_event_t * event, yaml_encoding_t encoding)
{
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_STREAM_START_EVENT;
  event->data.stream_start.encoding = encoding;
  return 1;
}

int yaml_stream_end_event_initialize(yaml_event_t * event)
{
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_STREAM_END_EVENT;
  return 1;
}

int yaml_document_start_event_initialize(
  yaml_event_t * event,
  yaml_char_t * version_directive,
  yaml_char_t ** tag_directives,
  int tags_amount,
  int implicit)
{
  (void)version_directive;
  (void)tag_directives;
  (void)tags_amount;
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_DOCUMENT_START_EVENT;
  event->data.document_start.implicit = (bool)implicit;
  return 1;
}

int yaml_document_end_event_initialize(yaml_event_t * event, int implicit)
{
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_DOCUMENT_END_EVENT;
  event->data.document_end.implicit = (bool)implicit;
  return 1;
}

int yaml_sequence_start_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  int implicit,
  yaml_sequence_style_t style)
{
  (void)style;
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_SEQUENCE_START_EVENT;
  event->data.sequence_start.anchor = anchor;
  event->data.sequence_start.tag = tag;
  event->data.sequence_start.implicit = (bool)implicit;
  return 1;
}

int yaml_sequence_end_event_initialize(yaml_event_t * event)
{
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_SEQUENCE_END_EVENT;
  return 1;
}

int yaml_mapping_start_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  int implicit,
  yaml_mapping_style_t style)
{
  (void)style;
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_MAPPING_START_EVENT;
  event->data.mapping_start.anchor = anchor;
  event->data.mapping_start.tag = tag;
  event->data.mapping_start.implicit = (bool)implicit;
  return 1;
}

int yaml_mapping_end_event_initialize(yaml_event_t * event)
{
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_MAPPING_END_EVENT;
  return 1;
}

int yaml_scalar_event_initialize(
  yaml_event_t * event,
  yaml_char_t * anchor,
  yaml_char_t * tag,
  yaml_char_t * value,
  int length,
  int plain_implicit,
  int quoted_implicit,
  yaml_scalar_style_t style)
{
  (void)plain_implicit;
  (void)quoted_implicit;
  if (event == NULL) {
    return 0;
  }
  memset(event, 0, sizeof(yaml_event_t));
  event->type = YAML_SCALAR_EVENT;
  event->data.scalar.anchor = anchor;
  event->data.scalar.tag = tag;
  event->data.scalar.value = value;
  if (length > 0) {
    event->data.scalar.length = (size_t)length;
  } else if (value != NULL) {
    event->data.scalar.length = strlen((const char *)value);
  } else {
    event->data.scalar.length = 0;
  }
  event->data.scalar.style = style;
  return 1;
}