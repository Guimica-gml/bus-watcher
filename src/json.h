#ifndef JSON_H_
#define JSON_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "./arena.h"
#include "./utils.h"

typedef enum {
    JSON_OBJ_NULL,
    JSON_OBJ_DICT,
    JSON_OBJ_ARRAY,
    JSON_OBJ_BOOLEAN,
    JSON_OBJ_INT64,
    JSON_OBJ_DECIMAL,
    JSON_OBJ_STRING,
} Json_Object_Kind;

typedef struct Json_Object Json_Object;
typedef struct Json_Key_Value_Pair Json_Key_Value_Pair;

typedef struct {
    Json_Key_Value_Pair *items;
    size_t count;
    size_t capacity;
} Json_Dict;

typedef struct {
    Json_Object *items;
    size_t count;
    size_t capacity;
} Json_Array;

typedef union {
    Json_Dict dict;
    Json_Array array;
    bool boolean;
    int64_t int64;
    double decimal;
    String string;
} Json_Object_As;

struct Json_Object {
    Json_Object_As as;
    Json_Object_Kind kind;
};

struct Json_Key_Value_Pair {
    Json_Object key;
    Json_Object value;
};

typedef enum {
    JSON_TOKEN_END,
    JSON_TOKEN_OPEN_CURLY,
    JSON_TOKEN_CLOSE_CURLY,
    JSON_TOKEN_OPEN_BRACKET,
    JSON_TOKEN_CLOSE_BRACKET,
    JSON_TOKEN_COMMA,
    JSON_TOKEN_COLON,
    JSON_TOKEN_TRUE,
    JSON_TOKEN_FALSE,
    JSON_TOKEN_NULL,
    JSON_TOKEN_INT64,
    JSON_TOKEN_DECIMAL,
    JSON_TOKEN_STRING,
} Json_Token_Kind;

typedef struct {
    size_t loc;
    String_View text;
    Json_Token_Kind kind;
} Json_Token;

typedef struct {
    String_View content;
    size_t cursor;
} Json_Lexer;

typedef struct {
    bool failed;
    const char *error;
    size_t error_loc;
} Json_Result;

const char *json_token_kind_to_cstr(Json_Token_Kind kind);

String_View json_lexer_consume_chars(Json_Lexer *lexer, size_t count);
String_View json_lexer_consume_until(Json_Lexer *lexer, char ch);
String_View json_lexer_consume_while(Json_Lexer *lexer, int (*func)(int));

bool json_lexer_starts_with(Json_Lexer *lexer, String_View text);
bool json_lexer_starts_with_char(Json_Lexer *lexer, char ch);

Json_Result json_lexer_next(Json_Lexer *lexer, Json_Token *token);
Json_Result json_lexer_peek(Json_Lexer *lexer, Json_Token *token);

void json_print_obj(Json_Object *obj);
void json_print_dict(Json_Dict *dict);
void json_print_array(Json_Array *array);

bool json_obj_eq(Json_Object *a, Json_Object *b);
Json_Object json_obj_string(Arena *arena, const char *cstr);

Json_Result json_parse_expect(Json_Lexer *lexer, Json_Token_Kind kind);
Json_Result json_parse_object(Arena *arena, Json_Lexer *lexer, Json_Object *object);
Json_Result json_parse(Arena *arena, Json_Object *object, const char *data, size_t size);

Json_Object *json_dict_get(Json_Dict *dict, Json_Object key);
Json_Object *json_array_get(Json_Array *array, size_t index);

// NOTE(nic): yes, X macros makes me feel smart
#define JSON_TYPES_DEF                          \
    X(dict, JSON_OBJ_DICT, Json_Dict)           \
    X(array, JSON_OBJ_ARRAY, Json_Array)        \
    X(boolean, JSON_OBJ_BOOLEAN, bool)          \
    X(int64, JSON_OBJ_INT64, int64_t)           \
    X(decimal, JSON_OBJ_DECIMAL, double)        \
    X(string, JSON_OBJ_STRING, String)

#define X(sufix, kind, type)                                        \
    type *json_dict_get_##sufix(Json_Dict *dict, Json_Object key);  \
    type *json_array_get_##sufix(Json_Array *array, size_t index);
JSON_TYPES_DEF
#undef X

#endif // JSON_H_
