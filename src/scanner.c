
/*
 * Introduction
 * ************
 *
 * The following notes assume that you are familiar with the YAML specification
 * (http://yaml.org/spec/cvs/current.html).  We mostly follow it, although in
 * some cases we are less restrictive that it requires.
 *
 * The process of transforming a YAML stream into a sequence of events is
 * divided on two steps: Scanning and Parsing.
 *
 * The Scanner transforms the input stream into a sequence of tokens, while the
 * parser transform the sequence of tokens produced by the Scanner into a
 * sequence of parsing events.
 *
 * The Scanner is rather clever and complicated. The Parser, on the contrary,
 * is a straightforward implementation of a recursive-descendant parser (or,
 * LL(1) parser, as it is usually called).
 *
 * Actually there are two issues of Scanning that might be called "clever", the
 * rest is quite straightforward.  The issues are "block collection start" and
 * "simple keys".  Both issues are explained below in details.
 *
 * Here the Scanning step is explained and implemented.  We start with the list
 * of all the tokens produced by the Scanner together with short descriptions.
 *
 * Now, tokens:
 *
 *      STREAM-START(encoding)          # The stream start.
 *      STREAM-END                      # The stream end.
 *      VERSION-DIRECTIVE(major,minor)  # The '%YAML' directive.
 *      TAG-DIRECTIVE(handle,prefix)    # The '%TAG' directive.
 *      DOCUMENT-START                  # '---'
 *      DOCUMENT-END                    # '...'
 *      BLOCK-SEQUENCE-START            # Indentation increase denoting a block
 *      BLOCK-MAPPING-START             # sequence or a block mapping.
 *      BLOCK-END                       # Indentation decrease.
 *      FLOW-SEQUENCE-START             # '['
 *      FLOW-SEQUENCE-END               # ']'
 *      BLOCK-SEQUENCE-START            # '{'
 *      BLOCK-SEQUENCE-END              # '}'
 *      BLOCK-ENTRY                     # '-'
 *      FLOW-ENTRY                      # ','
 *      KEY                             # '?' or nothing (simple keys).
 *      VALUE                           # ':'
 *      ALIAS(anchor)                   # '*anchor'
 *      ANCHOR(anchor)                  # '&anchor'
 *      TAG(handle,suffix)              # '!handle!suffix'
 *      SCALAR(value,style)             # A scalar.
 *
 * The following two tokens are "virtual" tokens denoting the beginning and the
 * end of the stream:
 *
 *      STREAM-START(encoding)
 *      STREAM-END
 *
 * We pass the information about the input stream encoding with the
 * STREAM-START token.
 *
 * The next two tokens are responsible for tags:
 *
 *      VERSION-DIRECTIVE(major,minor)
 *      TAG-DIRECTIVE(handle,prefix)
 *
 * Example:
 *
 *      %YAML   1.1
 *      %TAG    !   !foo
 *      %TAG    !yaml!  tag:yaml.org,2002:
 *      ---
 *
 * The correspoding sequence of tokens:
 *
 *      STREAM-START(utf-8)
 *      VERSION-DIRECTIVE(1,1)
 *      TAG-DIRECTIVE("!","!foo")
 *      TAG-DIRECTIVE("!yaml","tag:yaml.org,2002:")
 *      DOCUMENT-START
 *      STREAM-END
 *
 * Note that the VERSION-DIRECTIVE and TAG-DIRECTIVE tokens occupy a whole
 * line.
 *
 * The document start and end indicators are represented by:
 *
 *      DOCUMENT-START
 *      DOCUMENT-END
 *
 * Note that if a YAML stream contains an implicit document (without '---'
 * and '...' indicators), no DOCUMENT-START and DOCUMENT-END tokens will be
 * produced.
 *
 * In the following examples, we present whole documents together with the
 * produced tokens.
 *
 *      1. An implicit document:
 *
 *          'a scalar'
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          SCALAR("a scalar",single-quoted)
 *          STREAM-END
 *
 *      2. An explicit document:
 *
 *          ---
 *          'a scalar'
 *          ...
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          DOCUMENT-START
 *          SCALAR("a scalar",single-quoted)
 *          DOCUMENT-END
 *          STREAM-END
 *
 *      3. Several documents in a stream:
 *
 *          'a scalar'
 *          ---
 *          'another scalar'
 *          ---
 *          'yet another scalar'
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          SCALAR("a scalar",single-quoted)
 *          DOCUMENT-START
 *          SCALAR("another scalar",single-quoted)
 *          DOCUMENT-START
 *          SCALAR("yet another scalar",single-quoted)
 *          STREAM-END
 *
 * We have already introduced the SCALAR token above.  The following tokens are
 * used to describe aliases, anchors, tag, and scalars:
 *
 *      ALIAS(anchor)
 *      ANCHOR(anchor)
 *      TAG(handle,suffix)
 *      SCALAR(value,style)
 *
 * The following series of examples illustrate the usage of these tokens:
 *
 *      1. A recursive sequence:
 *
 *          &A [ *A ]
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          ANCHOR("A")
 *          FLOW-SEQUENCE-START
 *          ALIAS("A")
 *          FLOW-SEQUENCE-END
 *          STREAM-END
 *
 *      2. A tagged scalar:
 *
 *          !!float "3.14"  # A good approximation.
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          TAG("!!","float")
 *          SCALAR("3.14",double-quoted)
 *          STREAM-END
 *
 *      3. Various scalar styles:
 *
 *          --- # Implicit empty plain scalars do not produce tokens.
 *          --- a plain scalar
 *          --- 'a single-quoted scalar'
 *          --- "a double-quoted scalar"
 *          --- |-
 *            a literal scalar
 *          --- >-
 *            a folded
 *            scalar
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          DOCUMENT-START
 *          DOCUMENT-START
 *          SCALAR("a plain scalar",plain)
 *          DOCUMENT-START
 *          SCALAR("a single-quoted scalar",single-quoted)
 *          DOCUMENT-START
 *          SCALAR("a double-quoted scalar",double-quoted)
 *          DOCUMENT-START
 *          SCALAR("a literal scalar",literal)
 *          DOCUMENT-START
 *          SCALAR("a folded scalar",folded)
 *          STREAM-END
 *
 * Now it's time to review collection-related tokens. We will start with
 * flow collections:
 *
 *      FLOW-SEQUENCE-START
 *      FLOW-SEQUENCE-END
 *      FLOW-MAPPING-START
 *      FLOW-MAPPING-END
 *      FLOW-ENTRY
 *      KEY
 *      VALUE
 *
 * The tokens FLOW-SEQUENCE-START, FLOW-SEQUENCE-END, FLOW-MAPPING-START, and
 * FLOW-MAPPING-END represent the indicators '[', ']', '{', and '}'
 * correspondingly.  FLOW-ENTRY represent the ',' indicator.  Finally the
 * indicators '?' and ':', which are used for denoting mapping keys and values,
 * are represented by the KEY and VALUE tokens.
 *
 * The following examples show flow collections:
 *
 *      1. A flow sequence:
 *
 *          [item 1, item 2, item 3]
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          FLOW-SEQUENCE-START
 *          SCALAR("item 1",plain)
 *          FLOW-ENTRY
 *          SCALAR("item 2",plain)
 *          FLOW-ENTRY
 *          SCALAR("item 3",plain)
 *          FLOW-SEQUENCE-END
 *          STREAM-END
 *
 *      2. A flow mapping:
 *
 *          {
 *              a simple key: a value,  # Note that the KEY token is produced.
 *              ? a complex key: another value,
 *          }
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          FLOW-MAPPING-START
 *          KEY
 *          SCALAR("a simple key",plain)
 *          VALUE
 *          SCALAR("a value",plain)
 *          FLOW-ENTRY
 *          KEY
 *          SCALAR("a complex key",plain)
 *          VALUE
 *          SCALAR("another value",plain)
 *          FLOW-ENTRY
 *          FLOW-MAPPING-END
 *          STREAM-END
 *
 * A simple key is a key which is not denoted by the '?' indicator.  Note that
 * the Scanner still produce the KEY token whenever it encounters a simple key.
 *
 * For scanning block collections, the following tokens are used (note that we
 * repeat KEY and VALUE here):
 *
 *      BLOCK-SEQUENCE-START
 *      BLOCK-MAPPING-START
 *      BLOCK-END
 *      BLOCK-ENTRY
 *      KEY
 *      VALUE
 *
 * The tokens BLOCK-SEQUENCE-START and BLOCK-MAPPING-START denote indentation
 * increase that precedes a block collection (cf. the INDENT token in Python).
 * The token BLOCK-END denote indentation decrease that ends a block collection
 * (cf. the DEDENT token in Python).  However YAML has some syntax pecularities
 * that makes detections of these tokens more complex.
 *
 * The tokens BLOCK-ENTRY, KEY, and VALUE are used to represent the indicators
 * '-', '?', and ':' correspondingly.
 *
 * The following examples show how the tokens BLOCK-SEQUENCE-START,
 * BLOCK-MAPPING-START, and BLOCK-END are emitted by the Scanner:
 *
 *      1. Block sequences:
 *
 *          - item 1
 *          - item 2
 *          -
 *            - item 3.1
 *            - item 3.2
 *          -
 *            key 1: value 1
 *            key 2: value 2
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          BLOCK-SEQUENCE-START
 *          BLOCK-ENTRY
 *          SCALAR("item 1",plain)
 *          BLOCK-ENTRY
 *          SCALAR("item 2",plain)
 *          BLOCK-ENTRY
 *          BLOCK-SEQUENCE-START
 *          BLOCK-ENTRY
 *          SCALAR("item 3.1",plain)
 *          BLOCK-ENTRY
 *          SCALAR("item 3.2",plain)
 *          BLOCK-END
 *          BLOCK-ENTRY
 *          BLOCK-MAPPING-START
 *          KEY
 *          SCALAR("key 1",plain)
 *          VALUE
 *          SCALAR("value 1",plain)
 *          KEY
 *          SCALAR("key 2",plain)
 *          VALUE
 *          SCALAR("value 2",plain)
 *          BLOCK-END
 *          BLOCK-END
 *          STREAM-END
 *
 *      2. Block mappings:
 *
 *          a simple key: a value   # The KEY token is produced here.
 *          ? a complex key
 *          : another value
 *          a mapping:
 *            key 1: value 1
 *            key 2: value 2
 *          a sequence:
 *            - item 1
 *            - item 2
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          BLOCK-MAPPING-START
 *          KEY
 *          SCALAR("a simple key",plain)
 *          VALUE
 *          SCALAR("a value",plain)
 *          KEY
 *          SCALAR("a complex key",plain)
 *          VALUE
 *          SCALAR("another value",plain)
 *          KEY
 *          SCALAR("a mapping",plain)
 *          BLOCK-MAPPING-START
 *          KEY
 *          SCALAR("key 1",plain)
 *          VALUE
 *          SCALAR("value 1",plain)
 *          KEY
 *          SCALAR("key 2",plain)
 *          VALUE
 *          SCALAR("value 2",plain)
 *          BLOCK-END
 *          KEY
 *          SCALAR("a sequence",plain)
 *          VALUE
 *          BLOCK-SEQUENCE-START
 *          BLOCK-ENTRY
 *          SCALAR("item 1",plain)
 *          BLOCK-ENTRY
 *          SCALAR("item 2",plain)
 *          BLOCK-END
 *          BLOCK-END
 *          STREAM-END
 *
 * YAML does not always require to start a new block collection from a new
 * line.  If the current line contains only '-', '?', and ':' indicators, a new
 * block collection may start at the current line.  The following examples
 * illustrate this case:
 *
 *      1. Collections in a sequence:
 *
 *          - - item 1
 *            - item 2
 *          - key 1: value 1
 *            key 2: value 2
 *          - ? complex key
 *            : complex value
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          BLOCK-SEQUENCE-START
 *          BLOCK-ENTRY
 *          BLOCK-SEQUENCE-START
 *          BLOCK-ENTRY
 *          SCALAR("item 1",plain)
 *          BLOCK-ENTRY
 *          SCALAR("item 2",plain)
 *          BLOCK-END
 *          BLOCK-ENTRY
 *          BLOCK-MAPPING-START
 *          KEY
 *          SCALAR("key 1",plain)
 *          VALUE
 *          SCALAR("value 1",plain)
 *          KEY
 *          SCALAR("key 2",plain)
 *          VALUE
 *          SCALAR("value 2",plain)
 *          BLOCK-END
 *          BLOCK-ENTRY
 *          BLOCK-MAPPING-START
 *          KEY
 *          SCALAR("complex key")
 *          VALUE
 *          SCALAR("complex value")
 *          BLOCK-END
 *          BLOCK-END
 *          STREAM-END
 *
 *      2. Collections in a mapping:
 *
 *          ? a sequence
 *          : - item 1
 *            - item 2
 *          ? a mapping
 *          : key 1: value 1
 *            key 2: value 2
 *
 *      Tokens:
 *
 *          STREAM-START(utf-8)
 *          BLOCK-MAPPING-START
 *          KEY
 *          SCALAR("a sequence",plain)
 *          VALUE
 *          BLOCK-SEQUENCE-START
 *          BLOCK-ENTRY
 *          SCALAR("item 1",plain)
 *          BLOCK-ENTRY
 *          SCALAR("item 2",plain)
 *          BLOCK-END
 *          KEY
 *          SCALAR("a mapping",plain)
 *          VALUE
 *          BLOCK-MAPPING-START
 *          KEY
 *          SCALAR("key 1",plain)
 *          VALUE
 *          SCALAR("value 1",plain)
 *          KEY
 *          SCALAR("key 2",plain)
 *          VALUE
 *          SCALAR("value 2",plain)
 *          BLOCK-END
 *          BLOCK-END
 *          STREAM-END
 *
 * YAML also permits non-indented sequences if they are included into a block
 * mapping.  In this case, the token BLOCK-SEQUENCE-START is not produced:
 *
 *      key:
 *      - item 1    # BLOCK-SEQUENCE-START is NOT produced here.
 *      - item 2
 *
 * Tokens:
 *
 *      STREAM-START(utf-8)
 *      BLOCK-MAPPING-START
 *      KEY
 *      SCALAR("key",plain)
 *      VALUE
 *      BLOCK-ENTRY
 *      SCALAR("item 1",plain)
 *      BLOCK-ENTRY
 *      SCALAR("item 2",plain)
 *      BLOCK-END
 */

#include "yaml_private.h"

/*
 * Ensure that the buffer contains the required number of characters.
 * Return 1 on success, 0 on failure (reader error or memory error).
 */

#define CACHE(parser,length)                                                    \
    (parser->unread >= (length)                                                 \
        ? 1                                                                     \
        : yaml_parser_update_buffer(parser, (length)))

/*
 * Advance the buffer pointer.
 */

#define SKIP(parser)                                                            \
     (parser->mark.index ++,                                                    \
      parser->mark.column ++,                                                   \
      parser->unread --,                                                        \
      parser->input.pointer += WIDTH(parser->input))

#define SKIP_LINE(parser)                                                       \
     (IS_CRLF(parser->input) ?                                                  \
      (parser->mark.index += 2,                                                 \
       parser->mark.column = 0,                                                 \
       parser->mark.line ++,                                                    \
       parser->unread -= 2,                                                     \
       parser->input.pointer += 2) :                                            \
      IS_BREAK(parser->input) ?                                                 \
      (parser->mark.index ++,                                                   \
       parser->mark.column = 0,                                                 \
       parser->mark.line ++,                                                    \
       parser->unread --,                                                       \
       parser->input.pointer += WIDTH(parser->input)) : 0)

/*
 * Copy a character to a string buffer and advance pointers.
 */

#define READ(parser, string)                                                    \
     (OSTRING_EXTEND(parser, string) ?                                          \
         (COPY(string, parser->input),                                          \
          parser->mark.index ++,                                                \
          parser->mark.column ++,                                               \
          parser->unread --,                                                    \
          1) : 0)

/*
 * Copy a line break character to a string buffer and advance pointers.
 */

#define READ_LINE(parser, string)                                               \
    (OSTRING_EXTEND(parser, string) ?                                           \
    (((CHECK_AT(parser->input, '\r', 0)                                         \
       && CHECK_AT(parser->input, '\n', 1)) ?       /* CR LF -> LF */           \
     (JOIN_OCTET(string, (yaml_char_t) '\n'),                                   \
      parser->input.pointer += 2,                                               \
      parser->mark.index += 2,                                                  \
      parser->mark.column = 0,                                                  \
      parser->mark.line ++,                                                     \
      parser->unread -= 2) :                                                    \
     (CHECK_AT(parser->input, '\r', 0)                                          \
      || CHECK_AT(parser->input, '\n', 0)) ?        /* CR|LF -> LF */           \
     (JOIN_OCTET(string, (yaml_char_t) '\n'),                                   \
      parser->input.pointer ++,                                                 \
      parser->mark.index ++,                                                    \
      parser->mark.column = 0,                                                  \
      parser->mark.line ++,                                                     \
      parser->unread --) :                                                      \
     (CHECK_AT(parser->input, '\xC2', 0)                                        \
      && CHECK_AT(parser->input, '\x85', 1)) ?      /* NEL -> LF */             \
     (JOIN_OCTET(string, (yaml_char_t) '\n'),                                   \
      parser->input.pointer += 2,                                               \
      parser->mark.index ++,                                                    \
      parser->mark.column = 0,                                                  \
      parser->mark.line ++,                                                     \
      parser->unread --) :                                                      \
     (CHECK_AT(parser->input, '\xE2', 0) &&                                     \
      CHECK_AT(parser->input, '\x80', 1) &&                                     \
      (CHECK_AT(parser->input, '\xA8', 2) ||                                    \
       CHECK_AT(parser->input, '\xA9', 2))) ?       /* LS|PS -> LS|PS */        \
     (COPY_OCTET(string, parser->input),                                        \
      COPY_OCTET(string, parser->input),                                        \
      COPY_OCTET(string, parser->input),                                        \
      parser->mark.index ++,                                                    \
      parser->mark.column = 0,                                                  \
      parser->mark.line ++,                                                     \
      parser->unread --) : 0),                                                  \
    1) : 0)

/*
 * Public API declarations.
 */

YAML_DECLARE(int)
yaml_parser_parse_token(yaml_parser_t *parser, yaml_token_t *token);

/*
 * High-level token API.
 */

YAML_DECLARE(int)
yaml_parser_fetch_more_tokens(yaml_parser_t *parser);

static int
yaml_parser_fetch_next_token(yaml_parser_t *parser);

/*
 * Potential simple keys.
 */

static int
yaml_parser_stale_simple_keys(yaml_parser_t *parser);

static int
yaml_parser_save_simple_key(yaml_parser_t *parser);

static int
yaml_parser_remove_simple_key(yaml_parser_t *parser);

static int
yaml_parser_increase_flow_level(yaml_parser_t *parser);

static int
yaml_parser_decrease_flow_level(yaml_parser_t *parser);

/*
 * Indentation treatment.
 */

static int
yaml_parser_roll_indent(yaml_parser_t *parser, int column,
        int number, yaml_token_type_t type, yaml_mark_t mark);

static int
yaml_parser_unroll_indent(yaml_parser_t *parser, int column);

/*
 * Token fetchers.
 */

static int
yaml_parser_fetch_stream_start(yaml_parser_t *parser);

static int
yaml_parser_fetch_stream_end(yaml_parser_t *parser);

static int
yaml_parser_fetch_directive(yaml_parser_t *parser);

static int
yaml_parser_fetch_document_indicator(yaml_parser_t *parser,
        yaml_token_type_t type);

static int
yaml_parser_fetch_flow_collection_start(yaml_parser_t *parser,
        yaml_token_type_t type);

static int
yaml_parser_fetch_flow_collection_end(yaml_parser_t *parser,
        yaml_token_type_t type);

static int
yaml_parser_fetch_flow_entry(yaml_parser_t *parser);

static int
yaml_parser_fetch_block_entry(yaml_parser_t *parser);

static int
yaml_parser_fetch_key(yaml_parser_t *parser);

static int
yaml_parser_fetch_value(yaml_parser_t *parser);

static int
yaml_parser_fetch_anchor(yaml_parser_t *parser, yaml_token_type_t type);

static int
yaml_parser_fetch_tag(yaml_parser_t *parser);

static int
yaml_parser_fetch_block_scalar(yaml_parser_t *parser, int literal);

static int
yaml_parser_fetch_flow_scalar(yaml_parser_t *parser, int single);

static int
yaml_parser_fetch_plain_scalar(yaml_parser_t *parser);

/*
 * Token scanners.
 */

static int
yaml_parser_scan_to_next_token(yaml_parser_t *parser);

static int
yaml_parser_scan_directive(yaml_parser_t *parser, yaml_token_t *token);

static int
yaml_parser_scan_directive_name(yaml_parser_t *parser,
        yaml_mark_t start_mark, yaml_char_t **name);

static int
yaml_parser_scan_version_directive_value(yaml_parser_t *parser,
        yaml_mark_t start_mark, int *major, int *minor);

static int
yaml_parser_scan_version_directive_number(yaml_parser_t *parser,
        yaml_mark_t start_mark, int *number);

static int
yaml_parser_scan_tag_directive_value(yaml_parser_t *parser,
        yaml_mark_t mark, yaml_char_t **handle, yaml_char_t **prefix);

static int
yaml_parser_scan_anchor(yaml_parser_t *parser, yaml_token_t *token,
        yaml_token_type_t type);

static int
yaml_parser_scan_tag(yaml_parser_t *parser, yaml_token_t *token);

static int
yaml_parser_scan_tag_handle(yaml_parser_t *parser, int directive,
        yaml_mark_t start_mark, yaml_char_t **handle);

static int
yaml_parser_scan_tag_uri(yaml_parser_t *parser, int directive,
        yaml_char_t *head, yaml_mark_t start_mark, yaml_char_t **uri);

static int
yaml_parser_scan_uri_escapes(yaml_parser_t *parser, int directive,
        yaml_mark_t start_mark, yaml_ostring_t *string);

static int
yaml_parser_scan_block_scalar(yaml_parser_t *parser, yaml_token_t *token,
        int literal);

static int
yaml_parser_scan_block_scalar_breaks(yaml_parser_t *parser,
        int *indent, yaml_ostring_t *breaks,
        yaml_mark_t start_mark, yaml_mark_t *end_mark);

static int
yaml_parser_scan_flow_scalar(yaml_parser_t *parser, yaml_token_t *token,
        int single);

static int
yaml_parser_scan_plain_scalar(yaml_parser_t *parser, yaml_token_t *token);

/*
 * Get the next token.
 */

YAML_DECLARE(int)
yaml_parser_parse_token(yaml_parser_t *parser, yaml_token_t *token)
{
    assert(parser); /* Non-NULL parser object is expected. */
    assert(token);  /* Non-NULL token object is expected. */

    /* Erase the token object. */

    memset(token, 0, sizeof(yaml_token_t));

    /* No tokens after STREAM-END or error. */

    if (parser->is_stream_end_produced || parser->error.type) {
        return 1;
    }

    /* Ensure that the tokens queue contains enough tokens. */

    if (!parser->is_token_available) {
        if (!yaml_parser_fetch_more_tokens(parser))
            return 0;
    }

    /* Fetch the next token from the queue. */
    
    *token = DEQUEUE(parser, parser->tokens);
    parser->is_token_available = 0;
    parser->tokens_parsed ++;

    if (token->type == YAML_STREAM_END_TOKEN) {
        parser->is_stream_end_produced = 1;
    }

    return 1;
}

/*
 * Ensure that the tokens queue contains at least one token which can be
 * returned to the Parser.
 */

YAML_DECLARE(int)
yaml_parser_fetch_more_tokens(yaml_parser_t *parser)
{
    int need_more_tokens;

    /* While we need more tokens to fetch, do it. */

    while (1)
    {
        /*
         * Check if we really need to fetch more tokens.
         */

        need_more_tokens = 0;

        if (parser->tokens.head == parser->tokens.tail)
        {
            /* Queue is empty. */

            need_more_tokens = 1;
        }
        else
        {
            size_t idx;

            /* Check if any potential simple key may occupy the head position. */

            if (!yaml_parser_stale_simple_keys(parser))
                return 0;

            for (idx = 0; idx < parser->simple_keys.length; idx++) {
                yaml_simple_key_t *simple_key  = parser->simple_keys.list + idx;
                if (simple_key->is_possible
                        && simple_key->token_number == parser->tokens_parsed) {
                    need_more_tokens = 1;
                    break;
                }
            }
        }

        /* We are finished. */

        if (!need_more_tokens)
            break;

        /* Fetch the next token. */

        if (!yaml_parser_fetch_next_token(parser))
            return 0;
    }

    parser->is_token_available = 1;

    return 1;
}

/*
 * The dispatcher for token fetchers.
 */

static int
yaml_parser_fetch_next_token(yaml_parser_t *parser)
{
    yaml_mark_t start_mark = parser->mark;

    /* Ensure that the buffer is initialized. */

    if (!CACHE(parser, 1))
        return 0;

    /* Check if we just started scanning.  Fetch STREAM-START then. */

    if (!parser->is_stream_start_produced)
        return yaml_parser_fetch_stream_start(parser);

    /* Eat whitespaces and comments until we reach the next token. */

    if (!yaml_parser_scan_to_next_token(parser))
        return 0;

    /* Remove obsolete potential simple keys. */

    if (!yaml_parser_stale_simple_keys(parser))
        return 0;

    /* Check the indentation level against the current column. */

    if (!yaml_parser_unroll_indent(parser, parser->mark.column))
        return 0;

    /*
     * Ensure that the buffer contains at least 4 characters.  4 is the length
     * of the longest indicators ('--- ' and '... ').
     */

    if (!CACHE(parser, 4))
        return 0;

    /* Is it the end of the stream? */

    if (IS_Z(parser->input))
        return yaml_parser_fetch_stream_end(parser);

    /* Is it a directive? */

    if (parser->mark.column == 0 && CHECK(parser->input, '%'))
        return yaml_parser_fetch_directive(parser);

    /* Is it the document start indicator? */

    if (parser->mark.column == 0
            && CHECK_AT(parser->input, '-', 0)
            && CHECK_AT(parser->input, '-', 1)
            && CHECK_AT(parser->input, '-', 2)
            && IS_BLANKZ_AT(parser->input, 3))
        return yaml_parser_fetch_document_indicator(parser,
                YAML_DOCUMENT_START_TOKEN);

    /* Is it the document end indicator? */

    if (parser->mark.column == 0
            && CHECK_AT(parser->input, '.', 0)
            && CHECK_AT(parser->input, '.', 1)
            && CHECK_AT(parser->input, '.', 2)
            && IS_BLANKZ_AT(parser->input, 3))
        return yaml_parser_fetch_document_indicator(parser,
                YAML_DOCUMENT_END_TOKEN);

    /* Is it the flow sequence start indicator? */

    if (CHECK(parser->input, '['))
        return yaml_parser_fetch_flow_collection_start(parser,
                YAML_FLOW_SEQUENCE_START_TOKEN);

    /* Is it the flow mapping start indicator? */

    if (CHECK(parser->input, '{'))
        return yaml_parser_fetch_flow_collection_start(parser,
                YAML_FLOW_MAPPING_START_TOKEN);

    /* Is it the flow sequence end indicator? */

    if (CHECK(parser->input, ']'))
        return yaml_parser_fetch_flow_collection_end(parser,
                YAML_FLOW_SEQUENCE_END_TOKEN);

    /* Is it the flow mapping end indicator? */

    if (CHECK(parser->input, '}'))
        return yaml_parser_fetch_flow_collection_end(parser,
                YAML_FLOW_MAPPING_END_TOKEN);

    /* Is it the flow entry indicator? */

    if (CHECK(parser->input, ','))
        return yaml_parser_fetch_flow_entry(parser);

    /* Is it the block entry indicator? */

    if (CHECK(parser->input, '-') && IS_BLANKZ_AT(parser->input, 1))
        return yaml_parser_fetch_block_entry(parser);

    /* Is it the key indicator? */

    if (CHECK(parser->input, '?')
            && (parser->flow_level || IS_BLANKZ_AT(parser->input, 1)))
        return yaml_parser_fetch_key(parser);

    /* Is it the value indicator? */

    if (CHECK(parser->input, ':')
            && (parser->flow_level || IS_BLANKZ_AT(parser->input, 1)))
        return yaml_parser_fetch_value(parser);

    /* Is it an alias? */

    if (CHECK(parser->input, '*'))
        return yaml_parser_fetch_anchor(parser, YAML_ALIAS_TOKEN);

    /* Is it an anchor? */

    if (CHECK(parser->input, '&'))
        return yaml_parser_fetch_anchor(parser, YAML_ANCHOR_TOKEN);

    /* Is it a tag? */

    if (CHECK(parser->input, '!'))
        return yaml_parser_fetch_tag(parser);

    /* Is it a literal scalar? */

    if (CHECK(parser->input, '|') && !parser->flow_level)
        return yaml_parser_fetch_block_scalar(parser, 1);

    /* Is it a folded scalar? */

    if (CHECK(parser->input, '>') && !parser->flow_level)
        return yaml_parser_fetch_block_scalar(parser, 0);

    /* Is it a single-quoted scalar? */

    if (CHECK(parser->input, '\''))
        return yaml_parser_fetch_flow_scalar(parser, 1);

    /* Is it a double-quoted scalar? */

    if (CHECK(parser->input, '"'))
        return yaml_parser_fetch_flow_scalar(parser, 0);

    /*
     * Is it a plain scalar?
     *
     * A plain scalar may start with any non-blank characters except
     *
     *      '-', '?', ':', ',', '[', ']', '{', '}',
     *      '#', '&', '*', '!', '|', '>', '\'', '\"',
     *      '%', '@', '`'.
     *
     * In the block context (and, for the '-' indicator, in the flow context
     * too), it may also start with the characters
     *
     *      '-', '?', ':'
     *
     * if it is followed by a non-space character.
     *
     * The last rule is more restrictive than the specification requires.
     */

    if (!(IS_BLANKZ(parser->input) || CHECK(parser->input, '-')
                || CHECK(parser->input, '?') || CHECK(parser->input, ':')
                || CHECK(parser->input, ',') || CHECK(parser->input, '[')
                || CHECK(parser->input, ']') || CHECK(parser->input, '{')
                || CHECK(parser->input, '}') || CHECK(parser->input, '#')
                || CHECK(parser->input, '&') || CHECK(parser->input, '*')
                || CHECK(parser->input, '!') || CHECK(parser->input, '|')
                || CHECK(parser->input, '>') || CHECK(parser->input, '\'')
                || CHECK(parser->input, '"') || CHECK(parser->input, '%')
                || CHECK(parser->input, '@') || CHECK(parser->input, '`')) ||
            (CHECK(parser->input, '-') && !IS_BLANK_AT(parser->input, 1)) ||
            (!parser->flow_level &&
             (CHECK(parser->input, '?') || CHECK(parser->input, ':'))
             && !IS_BLANKZ_AT(parser->input, 1)))
        return yaml_parser_fetch_plain_scalar(parser);

    /*
     * If we don't determine the token type so far, it is an error.
     */

    return SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
            "while scanning for the next token", start_mark,
            "found character that cannot start any token", parser->mark);
}

/*
 * Check the list of potential simple keys and remove the positions that
 * cannot contain simple keys anymore.
 */

static int
yaml_parser_stale_simple_keys(yaml_parser_t *parser)
{
    size_t idx;

    /* Check for a potential simple key for each flow level. */

    for (idx = 0; idx < parser->simple_keys.length; idx ++)
    {
        yaml_simple_key_t *simple_key = parser->simple_keys.list + idx;

        /*
         * The specification requires that a simple key
         *
         *  - is limited to a single line,
         *  - is shorter than 1024 characters.
         */

        if (simple_key->is_possible
                && (simple_key->mark.line < parser->mark.line
                    || simple_key->mark.index+1024 < parser->mark.index)) {

            /* Check if the potential simple key to be removed is required. */

            if (simple_key->is_required) {
                return SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                        "while scanning a simple key", simple_key->mark,
                        "could not find expected ':'", parser->mark);
            }

            simple_key->is_possible = 0;
        }
    }

    return 1;
}

/*
 * Check if a simple key may start at the current position and add it if
 * needed.
 */

static int
yaml_parser_save_simple_key(yaml_parser_t *parser)
{
    /*
     * A simple key is required at the current position if the scanner is in
     * the block context and the current column coincides with the indentation
     * level.
     */

    int is_required = (!parser->flow_level
            && parser->indent == (int)parser->mark.column);

    /*
     * A simple key is required only when it is the first token in the current
     * line.  Therefore it is always allowed.  But we add a check anyway.
     */

    assert(parser->is_simple_key_allowed || !is_required);  /* Impossible. */

    /*
     * If the current position may start a simple key, save it.
     */

    if (parser->is_simple_key_allowed)
    {
        yaml_simple_key_t simple_key = { 1, is_required,
            parser->tokens_parsed + parser->tokens.tail - parser->tokens.head,
            { 0, 0, 0 } };
        simple_key.mark = parser->mark;

        if (!yaml_parser_remove_simple_key(parser)) return 0;

        parser->simple_keys.list[parser->simple_keys.length-1] = simple_key;
    }

    return 1;
}

/*
 * Remove a potential simple key at the current flow level.
 */

static int
yaml_parser_remove_simple_key(yaml_parser_t *parser)
{
    yaml_simple_key_t *simple_key =
        parser->simple_keys.list + parser->simple_keys.length - 1;

    if (simple_key->is_possible)
    {
        /* If the key is required, it is an error. */

        if (simple_key->is_required) {
            return SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                    "while scanning a simple key", simple_key->mark,
                    "could not find expected ':'", parser->mark);
        }
    }

    /* Remove the key from the stack. */

    simple_key->is_possible = 0;

    return 1;
}

/*
 * Increase the flow level and resize the simple key list if needed.
 */

static int
yaml_parser_increase_flow_level(yaml_parser_t *parser)
{
    yaml_simple_key_t empty_simple_key = { 0, 0, 0, { 0, 0, 0 } };

    /* Reset the simple key on the next level. */

    if (!PUSH(parser, parser->simple_keys, empty_simple_key))
        return 0;

    /* Increase the flow level. */

    parser->flow_level++;

    return 1;
}

/*
 * Decrease the flow level.
 */

static int
yaml_parser_decrease_flow_level(yaml_parser_t *parser)
{
    yaml_simple_key_t dummy_key;    /* Used to eliminate a compiler warning. */

    if (parser->flow_level) {
        parser->flow_level --;
        dummy_key = POP(parser, parser->simple_keys);
    }

    return 1;
}

/*
 * Push the current indentation level to the stack and set the new level
 * the current column is greater than the indentation level.  In this case,
 * append or insert the specified token into the token queue.
 * 
 */

static int
yaml_parser_roll_indent(yaml_parser_t *parser, int column,
        int number, yaml_token_type_t type, yaml_mark_t mark)
{
    yaml_token_t token;

    /* In the flow context, do nothing. */

    if (parser->flow_level)
        return 1;

    if (parser->indent < column)
    {
        /*
         * Push the current indentation level to the stack and set the new
         * indentation level.
         */

        if (!PUSH(parser, parser->indents, parser->indent))
            return 0;

        parser->indent = column;

        /* Create a token and insert it into the queue. */

        TOKEN_INIT(token, type, mark, mark);

        if (number == -1) {
            if (!ENQUEUE(parser, parser->tokens, token))
                return 0;
        }
        else {
            if (!QUEUE_INSERT(parser,
                        parser->tokens, number - parser->tokens_parsed, token))
                return 0;
        }
    }

    return 1;
}

/*
 * Pop indentation levels from the indents stack until the current level
 * becomes less or equal to the column.  For each intendation level, append
 * the BLOCK-END token.
 */


static int
yaml_parser_unroll_indent(yaml_parser_t *parser, int column)
{
    yaml_token_t token;

    /* In the flow context, do nothing. */

    if (parser->flow_level)
        return 1;

    /* Loop through the intendation levels in the stack. */

    while (parser->indent > column)
    {
        /* Create a token and append it to the queue. */

        TOKEN_INIT(token, YAML_BLOCK_END_TOKEN, parser->mark, parser->mark);

        if (!ENQUEUE(parser, parser->tokens, token))
            return 0;

        /* Pop the indentation level. */

        parser->indent = POP(parser, parser->indents);
    }

    return 1;
}

/*
 * Initialize the scanner and produce the STREAM-START token.
 */

static int
yaml_parser_fetch_stream_start(yaml_parser_t *parser)
{
    yaml_simple_key_t simple_key = { 0, 0, 0, { 0, 0, 0 } };
    yaml_token_t token;

    /* Set the initial indentation. */

    parser->indent = -1;

    /* Initialize the simple key stack. */

    if (!PUSH(parser, parser->simple_keys, simple_key))
        return 0;

    /* A simple key is allowed at the beginning of the stream. */

    parser->is_simple_key_allowed = 1;

    /* We have started. */

    parser->is_stream_start_produced = 1;

    /* Create the STREAM-START token and append it to the queue. */

    STREAM_START_TOKEN_INIT(token, parser->encoding,
            parser->mark, parser->mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the STREAM-END token and shut down the scanner.
 */

static int
yaml_parser_fetch_stream_end(yaml_parser_t *parser)
{
    yaml_token_t token;

    /* Force new line. */

    if (parser->mark.column != 0) {
        parser->mark.column = 0;
        parser->mark.line ++;
    }

    /* Reset the indentation level. */

    if (!yaml_parser_unroll_indent(parser, -1))
        return 0;

    /* Reset simple keys. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    parser->is_simple_key_allowed = 0;

    /* Create the STREAM-END token and append it to the queue. */

    STREAM_END_TOKEN_INIT(token, parser->mark, parser->mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce a VERSION-DIRECTIVE or TAG-DIRECTIVE token.
 */

static int
yaml_parser_fetch_directive(yaml_parser_t *parser)
{
    yaml_token_t token;

    /* Reset the indentation level. */

    if (!yaml_parser_unroll_indent(parser, -1))
        return 0;

    /* Reset simple keys. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    parser->is_simple_key_allowed = 0;

    /* Create the YAML-DIRECTIVE or TAG-DIRECTIVE token. */

    if (!yaml_parser_scan_directive(parser, &token))
        return 0;

    /* Append the token to the queue. */

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_destroy(&token);
        return 0;
    }

    return 1;
}

/*
 * Produce the DOCUMENT-START or DOCUMENT-END token.
 */

static int
yaml_parser_fetch_document_indicator(yaml_parser_t *parser,
        yaml_token_type_t type)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* Reset the indentation level. */

    if (!yaml_parser_unroll_indent(parser, -1))
        return 0;

    /* Reset simple keys. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    parser->is_simple_key_allowed = 0;

    /* Consume the token. */

    start_mark = parser->mark;

    SKIP(parser);
    SKIP(parser);
    SKIP(parser);

    end_mark = parser->mark;

    /* Create the DOCUMENT-START or DOCUMENT-END token. */

    TOKEN_INIT(token, type, start_mark, end_mark);

    /* Append the token to the queue. */

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the FLOW-SEQUENCE-START or FLOW-MAPPING-START token.
 */

static int
yaml_parser_fetch_flow_collection_start(yaml_parser_t *parser,
        yaml_token_type_t type)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* The indicators '[' and '{' may start a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* Increase the flow level. */

    if (!yaml_parser_increase_flow_level(parser))
        return 0;

    /* A simple key may follow the indicators '[' and '{'. */

    parser->is_simple_key_allowed = 1;

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the FLOW-SEQUENCE-START of FLOW-MAPPING-START token. */

    TOKEN_INIT(token, type, start_mark, end_mark);

    /* Append the token to the queue. */

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the FLOW-SEQUENCE-END or FLOW-MAPPING-END token.
 */

static int
yaml_parser_fetch_flow_collection_end(yaml_parser_t *parser,
        yaml_token_type_t type)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* Reset any potential simple key on the current flow level. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* Decrease the flow level. */

    if (!yaml_parser_decrease_flow_level(parser))
        return 0;

    /* No simple keys after the indicators ']' and '}'. */

    parser->is_simple_key_allowed = 0;

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the FLOW-SEQUENCE-END of FLOW-MAPPING-END token. */

    TOKEN_INIT(token, type, start_mark, end_mark);

    /* Append the token to the queue. */

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the FLOW-ENTRY token.
 */

static int
yaml_parser_fetch_flow_entry(yaml_parser_t *parser)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* Reset any potential simple keys on the current flow level. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* Simple keys are allowed after ','. */

    parser->is_simple_key_allowed = 1;

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the FLOW-ENTRY token and append it to the queue. */

    TOKEN_INIT(token, YAML_FLOW_ENTRY_TOKEN, start_mark, end_mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the BLOCK-ENTRY token.
 */

static int
yaml_parser_fetch_block_entry(yaml_parser_t *parser)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* Check if the scanner is in the block context. */

    if (!parser->flow_level)
    {
        /* Check if we are allowed to start a new entry. */

        if (!parser->is_simple_key_allowed) {
            return SCANNER_ERROR_INIT(parser,
                    "block sequence entries are not allowed in this context",
                    parser->mark);
        }

        /* Add the BLOCK-SEQUENCE-START token if needed. */

        if (!yaml_parser_roll_indent(parser, parser->mark.column, -1,
                    YAML_BLOCK_SEQUENCE_START_TOKEN, parser->mark))
            return 0;
    }
    else
    {
        /*
         * It is an error for the '-' indicator to occur in the flow context,
         * but we let the Parser detect and report about it because the Parser
         * is able to point to the context.
         */
    }

    /* Reset any potential simple keys on the current flow level. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* Simple keys are allowed after '-'. */

    parser->is_simple_key_allowed = 1;

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the BLOCK-ENTRY token and append it to the queue. */

    TOKEN_INIT(token, YAML_BLOCK_ENTRY_TOKEN, start_mark, end_mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the KEY token.
 */

static int
yaml_parser_fetch_key(yaml_parser_t *parser)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* In the block context, additional checks are required. */

    if (!parser->flow_level)
    {
        /* Check if we are allowed to start a new key (not nessesary simple). */

        if (!parser->is_simple_key_allowed) {
            return SCANNER_ERROR_INIT(parser,
                    "mapping keys are not allowed in this context", parser->mark);
        }

        /* Add the BLOCK-MAPPING-START token if needed. */

        if (!yaml_parser_roll_indent(parser, parser->mark.column, -1,
                    YAML_BLOCK_MAPPING_START_TOKEN, parser->mark))
            return 0;
    }

    /* Reset any potential simple keys on the current flow level. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* Simple keys are allowed after '?' in the block context. */

    parser->is_simple_key_allowed = (!parser->flow_level);

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the KEY token and append it to the queue. */

    TOKEN_INIT(token, YAML_KEY_TOKEN, start_mark, end_mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the VALUE token.
 */

static int
yaml_parser_fetch_value(yaml_parser_t *parser)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;
    yaml_simple_key_t *simple_key =
        parser->simple_keys.list + parser->simple_keys.length - 1;

    /* Have we found a simple key? */

    if (simple_key->is_possible)
    {

        /* Create the KEY token and insert it into the queue. */

        TOKEN_INIT(token, YAML_KEY_TOKEN, simple_key->mark, simple_key->mark);

        if (!QUEUE_INSERT(parser, parser->tokens,
                    simple_key->token_number - parser->tokens_parsed, token))
            return 0;

        /* In the block context, we may need to add the BLOCK-MAPPING-START token. */

        if (!yaml_parser_roll_indent(parser, simple_key->mark.column,
                    simple_key->token_number,
                    YAML_BLOCK_MAPPING_START_TOKEN, simple_key->mark))
            return 0;

        /* Remove the simple key. */

        simple_key->is_possible = 0;

        /* A simple key cannot follow another simple key. */

        parser->is_simple_key_allowed = 0;
    }
    else
    {
        /* The ':' indicator follows a complex key. */

        /* In the block context, extra checks are required. */

        if (!parser->flow_level)
        {
            /* Check if we are allowed to start a complex value. */

            if (!parser->is_simple_key_allowed) {
                return SCANNER_ERROR_INIT(parser,
                        "mapping values are not allowed in this context",
                        parser->mark);
            }

            /* Add the BLOCK-MAPPING-START token if needed. */

            if (!yaml_parser_roll_indent(parser, parser->mark.column, -1,
                        YAML_BLOCK_MAPPING_START_TOKEN, parser->mark))
                return 0;
        }

        /* Simple keys after ':' are allowed in the block context. */

        parser->is_simple_key_allowed = (!parser->flow_level);
    }

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the VALUE token and append it to the queue. */

    TOKEN_INIT(token, YAML_VALUE_TOKEN, start_mark, end_mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the ALIAS or ANCHOR token.
 */

static int
yaml_parser_fetch_anchor(yaml_parser_t *parser, yaml_token_type_t type)
{
    yaml_token_t token;

    /* An anchor or an alias could be a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* A simple key cannot follow an anchor or an alias. */

    parser->is_simple_key_allowed = 0;

    /* Create the ALIAS or ANCHOR token and append it to the queue. */

    if (!yaml_parser_scan_anchor(parser, &token, type))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_destroy(&token);
        return 0;
    }
    return 1;
}

/*
 * Produce the TAG token.
 */

static int
yaml_parser_fetch_tag(yaml_parser_t *parser)
{
    yaml_token_t token;

    /* A tag could be a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* A simple key cannot follow a tag. */

    parser->is_simple_key_allowed = 0;

    /* Create the TAG token and append it to the queue. */

    if (!yaml_parser_scan_tag(parser, &token))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_destroy(&token);
        return 0;
    }

    return 1;
}

/*
 * Produce the SCALAR(...,literal) or SCALAR(...,folded) tokens.
 */

static int
yaml_parser_fetch_block_scalar(yaml_parser_t *parser, int literal)
{
    yaml_token_t token;

    /* Remove any potential simple keys. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* A simple key may follow a block scalar. */

    parser->is_simple_key_allowed = 1;

    /* Create the SCALAR token and append it to the queue. */

    if (!yaml_parser_scan_block_scalar(parser, &token, literal))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_destroy(&token);
        return 0;
    }

    return 1;
}

/*
 * Produce the SCALAR(...,single-quoted) or SCALAR(...,double-quoted) tokens.
 */

static int
yaml_parser_fetch_flow_scalar(yaml_parser_t *parser, int single)
{
    yaml_token_t token;

    /* A plain scalar could be a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* A simple key cannot follow a flow scalar. */

    parser->is_simple_key_allowed = 0;

    /* Create the SCALAR token and append it to the queue. */

    if (!yaml_parser_scan_flow_scalar(parser, &token, single))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_destroy(&token);
        return 0;
    }

    return 1;
}

/*
 * Produce the SCALAR(...,plain) token.
 */

static int
yaml_parser_fetch_plain_scalar(yaml_parser_t *parser)
{
    yaml_token_t token;

    /* A plain scalar could be a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* A simple key cannot follow a flow scalar. */

    parser->is_simple_key_allowed = 0;

    /* Create the SCALAR token and append it to the queue. */

    if (!yaml_parser_scan_plain_scalar(parser, &token))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_destroy(&token);
        return 0;
    }

    return 1;
}

/*
 * Eat whitespaces and comments until the next token is found.
 */

static int
yaml_parser_scan_to_next_token(yaml_parser_t *parser)
{
    /* Until the next token is not found. */

    while (1)
    {
        /* Allow the BOM mark to start a line. */

        if (!CACHE(parser, 1)) return 0;

        if (parser->mark.column == 0 && IS_BOM(parser->input))
            SKIP(parser);

        /*
         * Eat whitespaces.
         *
         * Tabs are allowed:
         *
         *  - in the flow context;
         *  - in the block context, but not at the beginning of the line or
         *  after '-', '?', or ':' (complex value).  
         */

        if (!CACHE(parser, 1)) return 0;

        while (CHECK(parser->input,' ') ||
                ((parser->flow_level || !parser->is_simple_key_allowed) &&
                 CHECK(parser->input, '\t'))) {
            SKIP(parser);
            if (!CACHE(parser, 1)) return 0;
        }

        /* Eat a comment until a line break. */

        if (CHECK(parser->input, '#')) {
            while (!IS_BREAKZ(parser->input)) {
                SKIP(parser);
                if (!CACHE(parser, 1)) return 0;
            }
        }

        /* If it is a line break, eat it. */

        if (IS_BREAK(parser->input))
        {
            if (!CACHE(parser, 2)) return 0;
            SKIP_LINE(parser);

            /* In the block context, a new line may start a simple key. */

            if (!parser->flow_level) {
                parser->is_simple_key_allowed = 1;
            }
        }
        else
        {
            /* We have found a token. */

            break;
        }
    }

    return 1;
}

/*
 * Scan a YAML-DIRECTIVE or TAG-DIRECTIVE token.
 *
 * Scope:
 *      %YAML    1.1    # a comment \n
 *      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 */

int
yaml_parser_scan_directive(yaml_parser_t *parser, yaml_token_t *token)
{
    yaml_mark_t start_mark, end_mark;
    yaml_char_t *name = NULL;
    int major, minor;
    yaml_char_t *handle = NULL, *prefix = NULL;

    /* Eat '%'. */

    start_mark = parser->mark;

    SKIP(parser);

    /* Scan the directive name. */

    if (!yaml_parser_scan_directive_name(parser, start_mark, &name))
        goto error;

    /* Is it a YAML directive? */

    if (strcmp((char *)name, "YAML") == 0)
    {
        /* Scan the VERSION directive value. */

        if (!yaml_parser_scan_version_directive_value(parser, start_mark,
                    &major, &minor))
            goto error;

        end_mark = parser->mark;

        /* Create a VERSION-DIRECTIVE token. */

        VERSION_DIRECTIVE_TOKEN_INIT(*token, major, minor,
                start_mark, end_mark);
    }

    /* Is it a TAG directive? */

    else if (strcmp((char *)name, "TAG") == 0)
    {
        /* Scan the TAG directive value. */

        if (!yaml_parser_scan_tag_directive_value(parser, start_mark,
                    &handle, &prefix))
            goto error;

        end_mark = parser->mark;

        /* Create a TAG-DIRECTIVE token. */

        TAG_DIRECTIVE_TOKEN_INIT(*token, handle, prefix,
                start_mark, end_mark);
    }

    /* Unknown directive. */

    else
    {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a directive", start_mark,
                "found uknown directive name", parser->mark);
        goto error;
    }

    /* Eat the rest of the line including any comments. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_BLANK(parser->input)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) goto error;
    }

    if (CHECK(parser->input, '#')) {
        while (!IS_BREAKZ(parser->input)) {
            SKIP(parser);
            if (!CACHE(parser, 1)) goto error;
        }
    }

    /* Check if we are at the end of the line. */

    if (!IS_BREAKZ(parser->input)) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a directive", start_mark,
                "did not find expected comment or line break", parser->mark);
        goto error;
    }

    /* Eat a line break. */

    if (IS_BREAK(parser->input)) {
        if (!CACHE(parser, 2)) goto error;
        SKIP_LINE(parser);
    }

    yaml_free(name);

    return 1;

error:
    yaml_free(prefix);
    yaml_free(handle);
    yaml_free(name);
    return 0;
}

/*
 * Scan the directive name.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *       ^^^^
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *       ^^^
 */

static int
yaml_parser_scan_directive_name(yaml_parser_t *parser,
        yaml_mark_t start_mark, yaml_char_t **name)
{
    yaml_ostring_t string = NULL_OSTRING;

    if (!OSTRING_INIT(parser, string, INITIAL_STRING_CAPACITY))
        goto error;

    /* Consume the directive name. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_ALPHA(parser->input))
    {
        if (!READ(parser, string)) goto error;
        if (!CACHE(parser, 1)) goto error;
    }

    /* Check if the name is empty. */

    if (!string.pointer) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a directive", start_mark,
                "could not find expected directive name", parser->mark);
        goto error;
    }

    /* Check for an blank character after the name. */

    if (!IS_BLANKZ(parser->input)) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a directive", start_mark,
                "found unexpected non-alphabetical character", parser->mark);
        goto error;
    }

    *name = string.buffer;

    return 1;

error:
    OSTRING_DEL(parser, string);
    return 0;
}

/*
 * Scan the value of VERSION-DIRECTIVE.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *           ^^^^^^
 */

static int
yaml_parser_scan_version_directive_value(yaml_parser_t *parser,
        yaml_mark_t start_mark, int *major, int *minor)
{
    /* Eat whitespaces. */

    if (!CACHE(parser, 1)) return 0;

    while (IS_BLANK(parser->input)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) return 0;
    }

    /* Consume the major version number. */

    if (!yaml_parser_scan_version_directive_number(parser, start_mark, major))
        return 0;

    /* Eat '.'. */

    if (!CHECK(parser->input, '.')) {
        return SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a %YAML directive", start_mark,
                "did not find expected digit or '.' character", parser->mark);
    }

    SKIP(parser);

    /* Consume the minor version number. */

    if (!yaml_parser_scan_version_directive_number(parser, start_mark, minor))
        return 0;

    return 1;
}

#define MAX_NUMBER_LENGTH   9

/*
 * Scan the version number of VERSION-DIRECTIVE.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *              ^
 *      %YAML   1.1     # a comment \n
 *                ^
 */

static int
yaml_parser_scan_version_directive_number(yaml_parser_t *parser,
        yaml_mark_t start_mark, int *number)
{
    int value = 0;
    size_t length = 0;

    /* Repeat while the next character is digit. */

    if (!CACHE(parser, 1)) return 0;

    while (IS_DIGIT(parser->input))
    {
        /* Check if the number is too long. */

        if (++length > MAX_NUMBER_LENGTH) {
            return SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                    "while scanning a %YAML directive", start_mark,
                    "found extremely long version number", parser->mark);
        }

        value = value*10 + AS_DIGIT(parser->input);

        SKIP(parser);

        if (!CACHE(parser, 1)) return 0;
    }

    /* Check if the number was present. */

    if (!length) {
        return SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a %YAML directive", start_mark,
                "did not find expected version number", parser->mark);
    }

    *number = value;

    return 1;
}

/*
 * Scan the value of a TAG-DIRECTIVE token.
 *
 * Scope:
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 */

static int
yaml_parser_scan_tag_directive_value(yaml_parser_t *parser,
        yaml_mark_t start_mark, yaml_char_t **handle, yaml_char_t **prefix)
{
    yaml_char_t *handle_value = NULL;
    yaml_char_t *prefix_value = NULL;

    /* Eat whitespaces. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_BLANK(parser->input)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) goto error;
    }

    /* Scan a handle. */

    if (!yaml_parser_scan_tag_handle(parser, 1, start_mark, &handle_value))
        goto error;

    /* Expect a whitespace. */

    if (!CACHE(parser, 1)) goto error;

    if (!IS_BLANK(parser->input)) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a %TAG directive", start_mark,
                "did not find expected whitespace", parser->mark);
        goto error;
    }

    /* Eat whitespaces. */

    while (IS_BLANK(parser->input)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) goto error;
    }

    /* Scan a prefix. */

    if (!yaml_parser_scan_tag_uri(parser, 1, NULL, start_mark, &prefix_value))
        goto error;

    /* Expect a whitespace or line break. */

    if (!CACHE(parser, 1)) goto error;

    if (!IS_BLANKZ(parser->input)) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a %TAG directive", start_mark,
                "did not find expected whitespace or line break", parser->mark);
        goto error;
    }

    *handle = handle_value;
    *prefix = prefix_value;

    return 1;

error:
    yaml_free(handle_value);
    yaml_free(prefix_value);
    return 0;
}

static int
yaml_parser_scan_anchor(yaml_parser_t *parser, yaml_token_t *token,
        yaml_token_type_t type)
{
    int length = 0;
    yaml_mark_t start_mark, end_mark;
    yaml_ostring_t string = NULL_OSTRING;

    if (!OSTRING_INIT(parser, string, INITIAL_STRING_CAPACITY))
        goto error;

    /* Eat the indicator character. */

    start_mark = parser->mark;

    SKIP(parser);

    /* Consume the value. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_ALPHA(parser->input)) {
        if (!READ(parser, string)) goto error;
        if (!CACHE(parser, 1)) goto error;
        length ++;
    }

    end_mark = parser->mark;

    /*
     * Check if length of the anchor is greater than 0 and it is followed by
     * a whitespace character or one of the indicators:
     *
     *      '?', ':', ',', ']', '}', '%', '@', '`'.
     */

    if (!length || !(IS_BLANKZ(parser->input) || CHECK(parser->input, '?')
                || CHECK(parser->input, ':') || CHECK(parser->input, ',')
                || CHECK(parser->input, ']') || CHECK(parser->input, '}')
                || CHECK(parser->input, '%') || CHECK(parser->input, '@')
                || CHECK(parser->input, '`'))) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser, type == YAML_ANCHOR_TOKEN ?
                "while scanning an anchor" : "while scanning an alias",
                start_mark,
                "did not find expected alphabetic or numeric character",
                parser->mark);
        goto error;
    }

    /* Create a token. */

    if (type == YAML_ANCHOR_TOKEN) {
        ANCHOR_TOKEN_INIT(*token, string.buffer, start_mark, end_mark);
    }
    else {
        ALIAS_TOKEN_INIT(*token, string.buffer, start_mark, end_mark);
    }

    return 1;

error:
    OSTRING_DEL(parser, string);
    return 0;
}

/*
 * Scan a TAG token.
 */

static int
yaml_parser_scan_tag(yaml_parser_t *parser, yaml_token_t *token)
{
    yaml_char_t *handle = NULL;
    yaml_char_t *suffix = NULL;
    yaml_mark_t start_mark, end_mark;

    start_mark = parser->mark;

    /* Check if the tag is in the canonical form. */

    if (!CACHE(parser, 2)) goto error;

    if (CHECK_AT(parser->input, '<', 1))
    {
        /* Set the handle to '' */

        handle = yaml_malloc(1);
        if (!handle) goto error;
        handle[0] = '\0';

        /* Eat '!<' */

        SKIP(parser);
        SKIP(parser);

        /* Consume the tag value. */

        if (!yaml_parser_scan_tag_uri(parser, 0, NULL, start_mark, &suffix))
            goto error;

        /* Check for '>' and eat it. */

        if (!CHECK(parser->input, '>')) {
            SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                    "while scanning a tag", start_mark,
                    "did not find the expected '>'", parser->mark);
            goto error;
        }

        SKIP(parser);
    }
    else
    {
        /* The tag has either the '!suffix' or the '!handle!suffix' form. */

        /* First, try to scan a handle. */

        if (!yaml_parser_scan_tag_handle(parser, 0, start_mark, &handle))
            goto error;

        /* Check if it is, indeed, handle. */

        if (handle[0] == '!' && handle[1] != '\0' && handle[strlen((char *)handle)-1] == '!')
        {
            /* Scan the suffix now. */

            if (!yaml_parser_scan_tag_uri(parser, 0, NULL, start_mark, &suffix))
                goto error;
        }
        else
        {
            /* It wasn't a handle after all.  Scan the rest of the tag. */

            if (!yaml_parser_scan_tag_uri(parser, 0, handle, start_mark, &suffix))
                goto error;

            /* Set the handle to '!'. */

            yaml_free(handle);
            handle = yaml_malloc(2);
            if (!handle) goto error;
            handle[0] = '!';
            handle[1] = '\0';

            /*
             * A special case: the '!' tag.  Set the handle to '' and the
             * suffix to '!'.
             */

            if (suffix[0] == '\0') {
                yaml_char_t *tmp = handle;
                handle = suffix;
                suffix = tmp;
            }
        }
    }

    /* Check the character which ends the tag. */

    if (!CACHE(parser, 1)) goto error;

    if (!IS_BLANKZ(parser->input)) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a tag", start_mark,
                "did not find expected whitespace or line break", parser->mark);
        goto error;
    }

    end_mark = parser->mark;

    /* Create a token. */

    TAG_TOKEN_INIT(*token, handle, suffix, start_mark, end_mark);

    return 1;

error:
    yaml_free(handle);
    yaml_free(suffix);
    return 0;
}

/*
 * Scan a tag handle.
 */

static int
yaml_parser_scan_tag_handle(yaml_parser_t *parser, int directive,
        yaml_mark_t start_mark, yaml_char_t **handle)
{
    yaml_ostring_t string = NULL_OSTRING;

    if (!OSTRING_INIT(parser, string, INITIAL_STRING_CAPACITY))
        goto error;

    /* Check the initial '!' character. */

    if (!CACHE(parser, 1)) goto error;

    if (!CHECK(parser->input, '!')) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser, directive ?
                "while scanning a tag directive" : "while scanning a tag",
                start_mark, "did not find expected '!'", parser->mark);
        goto error;
    }

    /* Copy the '!' character. */

    if (!READ(parser, string)) goto error;

    /* Copy all subsequent alphabetical and numerical characters. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_ALPHA(parser->input))
    {
        if (!READ(parser, string)) goto error;
        if (!CACHE(parser, 1)) goto error;
    }

    /* Check if the trailing character is '!' and copy it. */

    if (CHECK(parser->input, '!'))
    {
        if (!READ(parser, string)) goto error;
    }
    else
    {
        /*
         * It's either the '!' tag or not really a tag handle.  If it's a %TAG
         * directive, it's an error.  If it's a tag token, it must be a part of
         * URI.
         */

        if (directive &&
                !(string.buffer[0] == '!' && string.buffer[1] == '\0')) {
            SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                    "while parsing a tag directive", start_mark,
                    "did not find expected '!'", parser->mark);
            goto error;
        }
    }

    *handle = string.buffer;

    return 1;

error:
    OSTRING_DEL(parser, string);
    return 0;
}

/*
 * Scan a tag.
 */

static int
yaml_parser_scan_tag_uri(yaml_parser_t *parser, int directive,
        yaml_char_t *head, yaml_mark_t start_mark, yaml_char_t **uri)
{
    size_t length = head ? strlen((char *)head) : 0;
    yaml_ostring_t string = NULL_OSTRING;

    if (!OSTRING_INIT(parser, string, INITIAL_STRING_CAPACITY))
        goto error;

    /* Resize the string to include the head. */

    while (string.capacity <= length) {
        if (!yaml_ostring_extend(&string.buffer, &string.capacity)) {
            MEMORY_ERROR_INIT(parser);
            goto error;
        }
    }

    /*
     * Copy the head if needed.
     *
     * Note that we don't copy the leading '!' character.
     */

    if (length > 1) {
        memcpy(string.buffer, head+1, length-1);
        string.pointer += length-1;
    }

    /* Scan the tag. */

    if (!CACHE(parser, 1)) goto error;

    /*
     * The set of characters that may appear in URI is as follows:
     *
     *      '0'-'9', 'A'-'Z', 'a'-'z', '_', '-', ';', '/', '?', ':', '@', '&',
     *      '=', '+', '$', ',', '.', '!', '~', '*', '\'', '(', ')', '[', ']',
     *      '%'.
     */

    while (IS_ALPHA(parser->input) || CHECK(parser->input, ';')
            || CHECK(parser->input, '/') || CHECK(parser->input, '?')
            || CHECK(parser->input, ':') || CHECK(parser->input, '@')
            || CHECK(parser->input, '&') || CHECK(parser->input, '=')
            || CHECK(parser->input, '+') || CHECK(parser->input, '$')
            || CHECK(parser->input, ',') || CHECK(parser->input, '.')
            || CHECK(parser->input, '!') || CHECK(parser->input, '~')
            || CHECK(parser->input, '*') || CHECK(parser->input, '\'')
            || CHECK(parser->input, '(') || CHECK(parser->input, ')')
            || CHECK(parser->input, '[') || CHECK(parser->input, ']')
            || CHECK(parser->input, '%'))
    {
        /* Check if it is a URI-escape sequence. */

        if (CHECK(parser->input, '%')) {
            if (!yaml_parser_scan_uri_escapes(parser,
                        directive, start_mark, &string)) goto error;
        }
        else {
            if (!READ(parser, string)) goto error;
        }

        length ++;
        if (!CACHE(parser, 1)) goto error;
    }

    /* Check if the tag is non-empty. */

    if (!length) {
        if (!OSTRING_EXTEND(parser, string))
            goto error;

        SCANNER_ERROR_WITH_CONTEXT_INIT(parser, directive ?
                "while parsing a %TAG directive" : "while parsing a tag",
                start_mark, "did not find expected tag URI", parser->mark);
        goto error;
    }

    *uri = string.buffer;

    return 1;

error:
    OSTRING_DEL(parser, string);
    return 0;
}

/*
 * Decode an URI-escape sequence corresponding to a single UTF-8 character.
 */

static int
yaml_parser_scan_uri_escapes(yaml_parser_t *parser, int directive,
        yaml_mark_t start_mark, yaml_ostring_t *string)
{
    int width = 0;

    /* Decode the required number of characters. */

    do {
        unsigned char octet = 0;

        /* Check for a URI-escaped octet. */

        if (!CACHE(parser, 3)) return 0;

        if (!(CHECK(parser->input, '%')
                    && IS_HEX_AT(parser->input, 1)
                    && IS_HEX_AT(parser->input, 2))) {
            return SCANNER_ERROR_WITH_CONTEXT_INIT(parser, directive ?
                    "while parsing a %TAG directive" : "while parsing a tag",
                    start_mark, "did not find URI escaped octet", parser->mark);
        }

        /* Get the octet. */

        octet = (AS_HEX_AT(parser->input, 1) << 4) + AS_HEX_AT(parser->input, 2);

        /* If it is the leading octet, determine the length of the UTF-8 sequence. */

        if (!width)
        {
            width = (octet & 0x80) == 0x00 ? 1 :
                    (octet & 0xE0) == 0xC0 ? 2 :
                    (octet & 0xF0) == 0xE0 ? 3 :
                    (octet & 0xF8) == 0xF0 ? 4 : 0;
            if (!width) {
                return SCANNER_ERROR_WITH_CONTEXT_INIT(parser, directive ?
                        "while parsing a %TAG directive" : "while parsing a tag",
                        start_mark, "found an incorrect leading UTF-8 octet",
                        parser->mark);
            }
        }
        else
        {
            /* Check if the trailing octet is correct. */

            if ((octet & 0xC0) != 0x80) {
                return SCANNER_ERROR_WITH_CONTEXT_INIT(parser, directive ?
                        "while parsing a %TAG directive" : "while parsing a tag",
                        start_mark, "found an incorrect trailing UTF-8 octet",
                        parser->mark);
            }
        }

        /* Copy the octet and move the pointers. */

        JOIN_OCTET(*string, octet);
        SKIP(parser);
        SKIP(parser);
        SKIP(parser);

    } while (--width);

    return 1;
}

/*
 * Scan a block scalar.
 */

static int
yaml_parser_scan_block_scalar(yaml_parser_t *parser, yaml_token_t *token,
        int literal)
{
    yaml_mark_t start_mark;
    yaml_mark_t end_mark;
    yaml_ostring_t string = NULL_OSTRING;
    yaml_ostring_t leading_break = NULL_OSTRING;
    yaml_ostring_t trailing_breaks = NULL_OSTRING;
    int chomping = 0;
    int increment = 0;
    int indent = 0;
    int leading_blank = 0;
    int trailing_blank = 0;

    if (!OSTRING_INIT(parser, string, INITIAL_STRING_CAPACITY))
        goto error;
    if (!OSTRING_INIT(parser, leading_break, INITIAL_STRING_CAPACITY))
        goto error;
    if (!OSTRING_INIT(parser, trailing_breaks, INITIAL_STRING_CAPACITY))
        goto error;

    /* Eat the indicator '|' or '>'. */

    start_mark = parser->mark;

    SKIP(parser);

    /* Scan the additional block scalar indicators. */

    if (!CACHE(parser, 1)) goto error;

    /* Check for a chomping indicator. */

    if (CHECK(parser->input, '+') || CHECK(parser->input, '-'))
    {
        /* Set the chomping method and eat the indicator. */

        chomping = CHECK(parser->input, '+') ? +1 : -1;

        SKIP(parser);

        /* Check for an indentation indicator. */

        if (!CACHE(parser, 1)) goto error;

        if (IS_DIGIT(parser->input))
        {
            /* Check that the intendation is greater than 0. */

            if (CHECK(parser->input, '0')) {
                SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                        "while scanning a block scalar", start_mark,
                        "found an intendation indicator equal to 0", parser->mark);
                goto error;
            }

            /* Get the intendation level and eat the indicator. */

            increment = AS_DIGIT(parser->input);

            SKIP(parser);
        }
    }

    /* Do the same as above, but in the opposite order. */

    else if (IS_DIGIT(parser->input))
    {
        if (CHECK(parser->input, '0')) {
            SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                    "while scanning a block scalar", start_mark,
                    "found an intendation indicator equal to 0", parser->mark);
            goto error;
        }

        increment = AS_DIGIT(parser->input);

        SKIP(parser);

        if (!CACHE(parser, 1)) goto error;

        if (CHECK(parser->input, '+') || CHECK(parser->input, '-')) {
            chomping = CHECK(parser->input, '+') ? +1 : -1;

            SKIP(parser);
        }
    }

    /* Eat whitespaces and comments to the end of the line. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_BLANK(parser->input)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) goto error;
    }

    if (CHECK(parser->input, '#')) {
        while (!IS_BREAKZ(parser->input)) {
            SKIP(parser);
            if (!CACHE(parser, 1)) goto error;
        }
    }

    /* Check if we are at the end of the line. */

    if (!IS_BREAKZ(parser->input)) {
        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                "while scanning a block scalar", start_mark,
                "did not find expected comment or line break", parser->mark);
        goto error;
    }

    /* Eat a line break. */

    if (IS_BREAK(parser->input)) {
        if (!CACHE(parser, 2)) goto error;
        SKIP_LINE(parser);
    }

    end_mark = parser->mark;

    /* Set the intendation level if it was specified. */

    if (increment) {
        indent = parser->indent >= 0 ? parser->indent+increment : increment;
    }

    /* Scan the leading line breaks and determine the indentation level if needed. */

    if (!yaml_parser_scan_block_scalar_breaks(parser, &indent, &trailing_breaks,
                start_mark, &end_mark)) goto error;

    /* Scan the block scalar content. */

    if (!CACHE(parser, 1)) goto error;

    while ((int)parser->mark.column == indent && !IS_Z(parser->input))
    {
        /*
         * We are at the beginning of a non-empty line.
         */

        /* Is it a trailing whitespace? */

        trailing_blank = IS_BLANK(parser->input);

        /* Check if we need to fold the leading line break. */

        if (!literal && (*leading_break.buffer == '\n')
                && !leading_blank && !trailing_blank)
        {
            /* Do we need to join the lines by space? */

            if (*trailing_breaks.buffer == '\0') {
                if (!OSTRING_EXTEND(parser, string)) goto error;
                JOIN_OCTET(string, ' ');
            }

            CLEAR(parser, leading_break);
        }
        else {
            if (!JOIN(parser, string, leading_break)) goto error;
            CLEAR(parser, leading_break);
        }

        /* Append the remaining line breaks. */

        if (!JOIN(parser, string, trailing_breaks)) goto error;
        CLEAR(parser, trailing_breaks);

        /* Is it a leading whitespace? */

        leading_blank = IS_BLANK(parser->input);

        /* Consume the current line. */

        while (!IS_BREAKZ(parser->input)) {
            if (!READ(parser, string)) goto error;
            if (!CACHE(parser, 1)) goto error;
        }

        /* Consume the line break. */

        if (!CACHE(parser, 2)) goto error;

        if (!READ_LINE(parser, leading_break)) goto error;

        /* Eat the following intendation spaces and line breaks. */

        if (!yaml_parser_scan_block_scalar_breaks(parser,
                    &indent, &trailing_breaks, start_mark, &end_mark)) goto error;
    }

    /* Chomp the tail. */

    if (chomping != -1) {
        if (!JOIN(parser, string, leading_break)) goto error;
    }
    if (chomping == 1) {
        if (!JOIN(parser, string, trailing_breaks)) goto error;
    }

    /* Create a token. */

    SCALAR_TOKEN_INIT(*token, string.buffer, string.pointer,
            literal ? YAML_LITERAL_SCALAR_STYLE : YAML_FOLDED_SCALAR_STYLE,
            start_mark, end_mark);

    OSTRING_DEL(parser, leading_break);
    OSTRING_DEL(parser, trailing_breaks);

    return 1;

error:
    OSTRING_DEL(parser, string);
    OSTRING_DEL(parser, leading_break);
    OSTRING_DEL(parser, trailing_breaks);

    return 0;
}

/*
 * Scan intendation spaces and line breaks for a block scalar.  Determine the
 * intendation level if needed.
 */

static int
yaml_parser_scan_block_scalar_breaks(yaml_parser_t *parser,
        int *indent, yaml_ostring_t *breaks,
        yaml_mark_t start_mark, yaml_mark_t *end_mark)
{
    int max_indent = 0;

    *end_mark = parser->mark;

    /* Eat the intendation spaces and line breaks. */

    while (1)
    {
        /* Eat the intendation spaces. */

        if (!CACHE(parser, 1)) return 0;

        while ((!*indent || (int)parser->mark.column < *indent)
                && IS_SPACE(parser->input)) {
            SKIP(parser);
            if (!CACHE(parser, 1)) return 0;
        }

        if ((int)parser->mark.column > max_indent)
            max_indent = (int)parser->mark.column;

        /* Check for a tab character messing the intendation. */

        if ((!*indent || (int)parser->mark.column < *indent)
                && IS_TAB(parser->input)) {
            return SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                    "while scanning a block scalar", start_mark,
                    "found a tab character where an intendation space is expected",
                    parser->mark);
        }

        /* Have we found a non-empty line? */

        if (!IS_BREAK(parser->input)) break;

        /* Consume the line break. */

        if (!CACHE(parser, 2)) return 0;
        if (!READ_LINE(parser, *breaks)) return 0;
        *end_mark = parser->mark;
    }

    /* Determine the indentation level if needed. */

    if (!*indent) {
        *indent = max_indent;
        if (*indent < parser->indent + 1)
            *indent = parser->indent + 1;
        if (*indent < 1)
            *indent = 1;
    }

   return 1; 
}

/*
 * Scan a quoted scalar.
 */

static int
yaml_parser_scan_flow_scalar(yaml_parser_t *parser, yaml_token_t *token,
        int single)
{
    yaml_mark_t start_mark;
    yaml_mark_t end_mark;
    yaml_ostring_t string = NULL_OSTRING;
    yaml_ostring_t leading_break = NULL_OSTRING;
    yaml_ostring_t trailing_breaks = NULL_OSTRING;
    yaml_ostring_t whitespaces = NULL_OSTRING;
    int leading_blanks;

    if (!OSTRING_INIT(parser, string, INITIAL_STRING_CAPACITY))
        goto error;
    if (!OSTRING_INIT(parser, leading_break, INITIAL_STRING_CAPACITY))
        goto error;
    if (!OSTRING_INIT(parser, trailing_breaks, INITIAL_STRING_CAPACITY))
        goto error;
    if (!OSTRING_INIT(parser, whitespaces, INITIAL_STRING_CAPACITY))
        goto error;

    /* Eat the left quote. */

    start_mark = parser->mark;

    SKIP(parser);

    /* Consume the content of the quoted scalar. */

    while (1)
    {
        /* Check that there are no document indicators at the beginning of the line. */

        if (!CACHE(parser, 4)) goto error;

        if (parser->mark.column == 0 &&
            ((CHECK_AT(parser->input, '-', 0) &&
              CHECK_AT(parser->input, '-', 1) &&
              CHECK_AT(parser->input, '-', 2)) ||
             (CHECK_AT(parser->input, '.', 0) &&
              CHECK_AT(parser->input, '.', 1) &&
              CHECK_AT(parser->input, '.', 2))) &&
            IS_BLANKZ_AT(parser->input, 3))
        {
            SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                    "while scanning a quoted scalar", start_mark,
                    "found unexpected document indicator", parser->mark);
            goto error;
        }

        /* Check for EOF. */

        if (IS_Z(parser->input)) {
            SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                    "while scanning a quoted scalar", start_mark,
                    "found unexpected end of stream", parser->mark);
            goto error;
        }

        /* Consume non-blank characters. */

        if (!CACHE(parser, 2)) goto error;

        leading_blanks = 0;

        while (!IS_BLANKZ(parser->input))
        {
            /* Check for an escaped single quote. */

            if (single && CHECK_AT(parser->input, '\'', 0)
                    && CHECK_AT(parser->input, '\'', 1))
            {
                if (!OSTRING_EXTEND(parser, string)) goto error;
                JOIN_OCTET(string, '\'');
                SKIP(parser);
                SKIP(parser);
            }

            /* Check for the right quote. */

            else if (CHECK(parser->input, single ? '\'' : '"'))
            {
                break;
            }

            /* Check for an escaped line break. */

            else if (!single && CHECK(parser->input, '\\')
                    && IS_BREAK_AT(parser->input, 1))
            {
                if (!CACHE(parser, 3)) goto error;
                SKIP(parser);
                SKIP_LINE(parser);
                leading_blanks = 1;
                break;
            }

            /* Check for an escape sequence. */

            else if (!single && CHECK(parser->input, '\\'))
            {
                size_t code_length = 0;

                if (!OSTRING_EXTEND(parser, string)) goto error;

                /* Check the escape character. */

                switch (OCTET_AT(parser->input, 1))
                {
                    case '0':
                        JOIN_OCTET(string, '\0');
                        break;

                    case 'a':
                        JOIN_OCTET(string, '\x07');
                        break;

                    case 'b':
                        JOIN_OCTET(string, '\x08');
                        break;

                    case 't':
                    case '\t':
                        JOIN_OCTET(string, '\x09');
                        break;

                    case 'n':
                        JOIN_OCTET(string, '\x0A');
                        break;

                    case 'v':
                        JOIN_OCTET(string, '\x0B');
                        break;

                    case 'f':
                        JOIN_OCTET(string, '\x0C');
                        break;

                    case 'r':
                        JOIN_OCTET(string, '\x0D');
                        break;

                    case 'e':
                        JOIN_OCTET(string, '\x1B');
                        break;

                    case ' ':
                        JOIN_OCTET(string, '\x20');
                        break;

                    case '"':
                        JOIN_OCTET(string, '"');
                        break;

                    case '\'':
                        JOIN_OCTET(string, '\'');
                        break;

                    case '\\':
                        JOIN_OCTET(string, '\\');
                        break;

                    case 'N':   /* NEL (#x85) */
                        JOIN_OCTET(string, '\xC2');
                        JOIN_OCTET(string, '\x85');
                        break;

                    case '_':   /* #xA0 */
                        JOIN_OCTET(string, '\xC2');
                        JOIN_OCTET(string, '\xA0');
                        break;

                    case 'L':   /* LS (#x2028) */
                        JOIN_OCTET(string, '\xE2');
                        JOIN_OCTET(string, '\x80');
                        JOIN_OCTET(string, '\xA8');
                        break;

                    case 'P':   /* PS (#x2029) */
                        JOIN_OCTET(string, '\xE2');
                        JOIN_OCTET(string, '\x80');
                        JOIN_OCTET(string, '\xA9');
                        break;

                    case 'x':
                        code_length = 2;
                        break;

                    case 'u':
                        code_length = 4;
                        break;

                    case 'U':
                        code_length = 8;
                        break;

                    default:
                        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                                "while parsing a quoted scalar", start_mark,
                                "found unknown escape character", parser->mark);
                        goto error;
                }

                SKIP(parser);
                SKIP(parser);

                /* Consume an arbitrary escape code. */

                if (code_length)
                {
                    unsigned int value = 0;
                    size_t idx;

                    /* Scan the character value. */

                    if (!CACHE(parser, code_length)) goto error;

                    for (idx = 0; idx < code_length; idx ++) {
                        if (!IS_HEX_AT(parser->input, idx)) {
                            SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                                    "while parsing a quoted scalar", start_mark,
                                    "did not find expected hexdecimal number",
                                    parser->mark);
                            goto error;
                        }
                        value = (value << 4) + AS_HEX_AT(parser->input, idx);
                    }

                    /* Check the value and write the character. */

                    if ((value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
                        SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                                "while parsing a quoted scalar", start_mark,
                                "found invalid Unicode character escape code",
                                parser->mark);
                        goto error;
                    }

                    if (value <= 0x7F) {
                        JOIN_OCTET(string, value);
                    }
                    else if (value <= 0x7FF) {
                        JOIN_OCTET(string, 0xC0 + (value >> 6));
                        JOIN_OCTET(string, 0x80 + (value & 0x3F));
                    }
                    else if (value <= 0xFFFF) {
                        JOIN_OCTET(string, 0xE0 + (value >> 12));
                        JOIN_OCTET(string, 0x80 + ((value >> 6) & 0x3F));
                        JOIN_OCTET(string, 0x80 + (value & 0x3F));
                    }
                    else {
                        JOIN_OCTET(string, 0xF0 + (value >> 18));
                        JOIN_OCTET(string, 0x80 + ((value >> 12) & 0x3F));
                        JOIN_OCTET(string, 0x80 + ((value >> 6) & 0x3F));
                        JOIN_OCTET(string, 0x80 + (value & 0x3F));
                    }

                    /* Advance the pointer. */

                    for (idx = 0; idx < code_length; idx ++) {
                        SKIP(parser);
                    }
                }
            }

            else
            {
                /* It is a non-escaped non-blank character. */

                if (!READ(parser, string)) goto error;
            }

            if (!CACHE(parser, 2)) goto error;
        }

        /* Check if we are at the end of the scalar. */

        if (CHECK(parser->input, single ? '\'' : '"'))
            break;

        /* Consume blank characters. */

        if (!CACHE(parser, 1)) goto error;

        while (IS_BLANK(parser->input) || IS_BREAK(parser->input))
        {
            if (IS_BLANK(parser->input))
            {
                /* Consume a space or a tab character. */

                if (!leading_blanks) {
                    if (!READ(parser, whitespaces)) goto error;
                }
                else {
                    SKIP(parser);
                }
            }
            else
            {
                if (!CACHE(parser, 2)) goto error;

                /* Check if it is a first line break. */

                if (!leading_blanks)
                {
                    CLEAR(parser, whitespaces);
                    if (!READ_LINE(parser, leading_break)) goto error;
                    leading_blanks = 1;
                }
                else
                {
                    if (!READ_LINE(parser, trailing_breaks)) goto error;
                }
            }
            if (!CACHE(parser, 1)) goto error;
        }

        /* Join the whitespaces or fold line breaks. */

        if (leading_blanks)
        {
            /* Do we need to fold line breaks? */

            if (leading_break.buffer[0] == '\n') {
                if (trailing_breaks.buffer[0] == '\0') {
                    if (!OSTRING_EXTEND(parser, string)) goto error;
                    JOIN_OCTET(string, ' ');
                }
                else {
                    if (!JOIN(parser, string, trailing_breaks)) goto error;
                    CLEAR(parser, trailing_breaks);
                }
                CLEAR(parser, leading_break);
            }
            else {
                if (!JOIN(parser, string, leading_break)) goto error;
                if (!JOIN(parser, string, trailing_breaks)) goto error;
                CLEAR(parser, leading_break);
                CLEAR(parser, trailing_breaks);
            }
        }
        else
        {
            if (!JOIN(parser, string, whitespaces)) goto error;
            CLEAR(parser, whitespaces);
        }
    }

    /* Eat the right quote. */

    SKIP(parser);

    end_mark = parser->mark;

    /* Create a token. */

    SCALAR_TOKEN_INIT(*token, string.buffer, string.pointer,
            single ? YAML_SINGLE_QUOTED_SCALAR_STYLE : YAML_DOUBLE_QUOTED_SCALAR_STYLE,
            start_mark, end_mark);

    OSTRING_DEL(parser, leading_break);
    OSTRING_DEL(parser, trailing_breaks);
    OSTRING_DEL(parser, whitespaces);

    return 1;

error:
    OSTRING_DEL(parser, string);
    OSTRING_DEL(parser, leading_break);
    OSTRING_DEL(parser, trailing_breaks);
    OSTRING_DEL(parser, whitespaces);

    return 0;
}

/*
 * Scan a plain scalar.
 */

static int
yaml_parser_scan_plain_scalar(yaml_parser_t *parser, yaml_token_t *token)
{
    yaml_mark_t start_mark;
    yaml_mark_t end_mark;
    yaml_ostring_t string = NULL_OSTRING;
    yaml_ostring_t leading_break = NULL_OSTRING;
    yaml_ostring_t trailing_breaks = NULL_OSTRING;
    yaml_ostring_t whitespaces = NULL_OSTRING;
    int leading_blanks = 0;
    int indent = parser->indent+1;

    if (!OSTRING_INIT(parser, string, INITIAL_STRING_CAPACITY))
        goto error;
    if (!OSTRING_INIT(parser, leading_break, INITIAL_STRING_CAPACITY))
        goto error;
    if (!OSTRING_INIT(parser, trailing_breaks, INITIAL_STRING_CAPACITY))
        goto error;
    if (!OSTRING_INIT(parser, whitespaces, INITIAL_STRING_CAPACITY))
        goto error;

    start_mark = end_mark = parser->mark;

    /* Consume the content of the plain scalar. */

    while (1)
    {
        /* Check for a document indicator. */

        if (!CACHE(parser, 4)) goto error;

        if (parser->mark.column == 0 &&
            ((CHECK_AT(parser->input, '-', 0) &&
              CHECK_AT(parser->input, '-', 1) &&
              CHECK_AT(parser->input, '-', 2)) ||
             (CHECK_AT(parser->input, '.', 0) &&
              CHECK_AT(parser->input, '.', 1) &&
              CHECK_AT(parser->input, '.', 2))) &&
            IS_BLANKZ_AT(parser->input, 3)) break;

        /* Check for a comment. */

        if (CHECK(parser->input, '#'))
            break;

        /* Consume non-blank characters. */

        while (!IS_BLANKZ(parser->input))
        {
            /* Check for 'x:x' in the flow context. TODO: Fix the test "spec-08-13". */

            if (parser->flow_level
                    && CHECK(parser->input, ':')
                    && !IS_BLANKZ_AT(parser->input, 1)) {
                SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                        "while scanning a plain scalar", start_mark,
                        "found unexpected ':'", parser->mark);
                goto error;
            }

            /* Check for indicators that may end a plain scalar. */

            if ((CHECK(parser->input, ':') && IS_BLANKZ_AT(parser->input, 1))
                    || (parser->flow_level &&
                        (CHECK(parser->input, ',') || CHECK(parser->input, ':')
                         || CHECK(parser->input, '?') || CHECK(parser->input, '[')
                         || CHECK(parser->input, ']') || CHECK(parser->input, '{')
                         || CHECK(parser->input, '}'))))
                break;

            /* Check if we need to join whitespaces and breaks. */

            if (leading_blanks || whitespaces.pointer > 0)
            {
                if (leading_blanks)
                {
                    /* Do we need to fold line breaks? */

                    if (leading_break.buffer[0] == '\n') {
                        if (trailing_breaks.buffer[0] == '\0') {
                            if (!OSTRING_EXTEND(parser, string)) goto error;
                            JOIN_OCTET(string, ' ');
                        }
                        else {
                            if (!JOIN(parser, string, trailing_breaks)) goto error;
                            CLEAR(parser, trailing_breaks);
                        }
                        CLEAR(parser, leading_break);
                    }
                    else {
                        if (!JOIN(parser, string, leading_break)) goto error;
                        if (!JOIN(parser, string, trailing_breaks)) goto error;
                        CLEAR(parser, leading_break);
                        CLEAR(parser, trailing_breaks);
                    }

                    leading_blanks = 0;
                }
                else
                {
                    if (!JOIN(parser, string, whitespaces)) goto error;
                    CLEAR(parser, whitespaces);
                }
            }

            /* Copy the character. */

            if (!READ(parser, string)) goto error;

            end_mark = parser->mark;

            if (!CACHE(parser, 2)) goto error;
        }

        /* Is it the end? */

        if (!(IS_BLANK(parser->input) || IS_BREAK(parser->input)))
            break;

        /* Consume blank characters. */

        if (!CACHE(parser, 1)) goto error;

        while (IS_BLANK(parser->input) || IS_BREAK(parser->input))
        {
            if (IS_BLANK(parser->input))
            {
                /* Check for tab character that abuse intendation. */

                if (leading_blanks && (int)parser->mark.column < indent
                        && IS_TAB(parser->input)) {
                    SCANNER_ERROR_WITH_CONTEXT_INIT(parser,
                            "while scanning a plain scalar", start_mark,
                            "found a tab character that violate intendation",
                            parser->mark);
                    goto error;
                }

                /* Consume a space or a tab character. */

                if (!leading_blanks) {
                    if (!READ(parser, whitespaces)) goto error;
                }
                else {
                    SKIP(parser);
                }
            }
            else
            {
                if (!CACHE(parser, 2)) goto error;

                /* Check if it is a first line break. */

                if (!leading_blanks)
                {
                    CLEAR(parser, whitespaces);
                    if (!READ_LINE(parser, leading_break)) goto error;
                    leading_blanks = 1;
                }
                else
                {
                    if (!READ_LINE(parser, trailing_breaks)) goto error;
                }
            }
            if (!CACHE(parser, 1)) goto error;
        }

        /* Check intendation level. */

        if (!parser->flow_level && (int)parser->mark.column < indent)
            break;
    }

    /* Create a token. */

    SCALAR_TOKEN_INIT(*token, string.buffer, string.pointer,
            YAML_PLAIN_SCALAR_STYLE, start_mark, end_mark);

    /* Note that we change the 'is_simple_key_allowed' flag. */

    if (leading_blanks) {
        parser->is_simple_key_allowed = 1;
    }

    OSTRING_DEL(parser, leading_break);
    OSTRING_DEL(parser, trailing_breaks);
    OSTRING_DEL(parser, whitespaces);

    return 1;

error:
    OSTRING_DEL(parser, string);
    OSTRING_DEL(parser, leading_break);
    OSTRING_DEL(parser, trailing_breaks);
    OSTRING_DEL(parser, whitespaces);

    return 0;
}

