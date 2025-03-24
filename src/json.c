#include "./json.h"
#include "./arena.h"

#include <float.h>
#include <math.h>
#include <assert.h>
#include <ctype.h>

typedef struct {
    String_View sv;
    Json_Token_Kind kind;
} Json_Literal_Def;

Json_Literal_Def json_symbols[] = {
    { .sv = SV_STATIC("{"), .kind = JSON_TOKEN_OPEN_CURLY },
    { .sv = SV_STATIC("}"), .kind = JSON_TOKEN_CLOSE_CURLY },
    { .sv = SV_STATIC("["), .kind = JSON_TOKEN_OPEN_BRACKET },
    { .sv = SV_STATIC("]"), .kind = JSON_TOKEN_CLOSE_BRACKET },
    { .sv = SV_STATIC(","), .kind = JSON_TOKEN_COMMA },
    { .sv = SV_STATIC(":"), .kind = JSON_TOKEN_COLON },
};
size_t json_symbols_count = sizeof(json_symbols)/sizeof(*json_symbols);

Json_Literal_Def json_keywords[] = {
    { .sv = SV_STATIC("true"), .kind = JSON_TOKEN_TRUE },
    { .sv = SV_STATIC("false"), .kind = JSON_TOKEN_FALSE },
    { .sv = SV_STATIC("null"), .kind = JSON_TOKEN_NULL },
};
size_t json_keywords_count = sizeof(json_keywords)/sizeof(*json_keywords);

const char *json_token_kind_to_cstr(Json_Token_Kind kind) {
    switch (kind) {
    case JSON_TOKEN_END: return "<eof>";
    case JSON_TOKEN_OPEN_CURLY: return "{";
    case JSON_TOKEN_CLOSE_CURLY: return "}";
    case JSON_TOKEN_OPEN_BRACKET: return "[";
    case JSON_TOKEN_CLOSE_BRACKET: return "]";
    case JSON_TOKEN_COMMA: return ",";
    case JSON_TOKEN_COLON: return ":";
    case JSON_TOKEN_TRUE: return "true";
    case JSON_TOKEN_FALSE: return "false";
    case JSON_TOKEN_NULL: return "null";
    case JSON_TOKEN_INT64: return "integer";
    case JSON_TOKEN_DECIMAL: return "decimal";
    case JSON_TOKEN_STRING: return "string";
    default: assert(0 && "unreachable");
    }
}

String_View json_lexer_consume_chars(Json_Lexer *lexer, size_t count) {
    assert(lexer->cursor + count <= lexer->content.size);
    String_View text = {0};
    text.data = &lexer->content.data[lexer->cursor];
    text.size = count;
    lexer->cursor += count;
    return text;
}

String_View json_lexer_consume_until(Json_Lexer *lexer, char ch) {
    size_t size = 0;
    while (lexer->cursor + size < lexer->content.size &&
           lexer->content.data[lexer->cursor + size] != ch)
    {
        size += 1;
    }
    return json_lexer_consume_chars(lexer, size);
}

String_View json_lexer_consume_while(Json_Lexer *lexer, int (*func)(int)) {
    size_t size = 0;
    while (lexer->cursor + size < lexer->content.size &&
           func(lexer->content.data[lexer->cursor + size]))
    {
        size += 1;
    }
    return json_lexer_consume_chars(lexer, size);
}

bool json_lexer_starts_with(Json_Lexer *lexer, String_View text) {
    if (lexer->cursor + text.size > lexer->content.size) {
        return false;
    }
    return memcmp(&lexer->content.data[lexer->cursor], text.data, text.size) == 0;
}

bool json_lexer_starts_with_char(Json_Lexer *lexer, char ch) {
    return lexer->cursor < lexer->content.size && lexer->content.data[lexer->cursor] == ch;
}

int json_is_number_char(int ch) {
    return isdigit(ch) || ch == '-' || ch == '.';
}

Json_Result json_lexer_next(Json_Lexer *lexer, Json_Token *token) {
    json_lexer_consume_while(lexer, isspace);
    Json_Result result = {0};
    result.error_loc = lexer->cursor;
    token->loc = lexer->cursor;

    if (lexer->cursor >= lexer->content.size) {
        token->kind = JSON_TOKEN_END;
        token->text = SV("<end>");
        return result;
    }

    for (size_t i = 0; i < json_symbols_count; ++i) {
        if (json_lexer_starts_with(lexer, json_symbols[i].sv)) {
            token->kind = json_symbols[i].kind;
            token->text = json_lexer_consume_chars(lexer, json_symbols[i].sv.size);
            return result;
        }
    }

    char ch = lexer->content.data[lexer->cursor];

    if (ch == '"') {
        json_lexer_consume_chars(lexer, 1);
        const char *begin = lexer->content.data + lexer->cursor;
        size_t begin_cursor = lexer->cursor;
        bool escaped = false;
        while (true) {
            if (lexer->cursor >= lexer->content.size ||
                json_lexer_starts_with_char(lexer, '\n'))
            {
                result.failed = true;
                result.error = "unclosed string literal";
                return result;
            }

            char ch = lexer->content.data[lexer->cursor];
            json_lexer_consume_chars(lexer, 1);

            if (!escaped && ch == '\"') {
                break;
            }
            escaped = (!escaped && ch == '\\');
        }
        token->kind = JSON_TOKEN_STRING;
        token->text = (String_View) { begin, lexer->cursor - begin_cursor - 1 };
        return result;
    }

    if (isalpha(ch)) {
        String_View word = json_lexer_consume_while(lexer, isalnum);
        for (size_t i = 0; i < json_keywords_count; ++i) {
            if (sv_eq(word, json_keywords[i].sv)) {
                token->kind = json_keywords[i].kind;
                token->text = word;
                return result;
            }
        }
        result.failed = true;
        result.error = "unknown word";
        return result;
    }

    // NOTE(nic): we do not validate integer and decimal literals
    if (json_is_number_char(ch)) {
        String_View number = json_lexer_consume_while(lexer, json_is_number_char);
        token->kind = (sv_find(number, '.', NULL)) ? JSON_TOKEN_DECIMAL : JSON_TOKEN_INT64;
        token->text = number;
        return result;
    }

    result.failed = true;
    result.error = "invalid character";
    return result;
}

Json_Result json_lexer_peek(Json_Lexer *lexer, Json_Token *token) {
    size_t save_cursor = lexer->cursor;
    Json_Result result = json_lexer_next(lexer, token);
    lexer->cursor = save_cursor;
    return result;
}

void json_print_dict(Json_Dict *dict) {
    printf("{");
    for (size_t i = 0; i < dict->count; ++i) {
        json_print_obj(&dict->items[i].key);
        printf(": ");
        json_print_obj(&dict->items[i].value);
        if (i < dict->count - 1) {
            printf(", ");
        }
    }
    printf("}");
}

void json_print_array(Json_Array *array) {
    printf("[");
    for (size_t i = 0; i < array->count; ++i) {
        json_print_obj(&array->items[i]);
        if (i < array->count - 1) {
            printf(", ");
        }
    }
    printf("]");
}

void json_print_obj(Json_Object *obj) {
    switch (obj->kind) {
    case JSON_OBJ_NULL:
        printf("null");
        break;
    case JSON_OBJ_DICT:
        json_print_dict(&obj->as.dict);
        break;
    case JSON_OBJ_ARRAY:
        json_print_array(&obj->as.array);
        break;
    case JSON_OBJ_BOOLEAN:
        printf("%s", (obj->as.boolean) ? "true" : "false");
        break;
    case JSON_OBJ_INT64:
        printf("%ld", obj->as.int64);
        break;
    case JSON_OBJ_DECIMAL:
        printf("%lf", obj->as.decimal);
        break;
    case JSON_OBJ_STRING:
        printf("\"%.*s\"", (int)obj->as.string.count, obj->as.string.items);
        break;
    default:
        assert(0 && "unreachable");
    }
}

Json_Result json_parse_expect(Json_Lexer *lexer, Json_Token_Kind kind) {
    Json_Token token = {0};
    Json_Result result = json_lexer_next(lexer, &token);
    if (result.failed) {
        return result;
    }
    if (token.kind != kind) {
        result.failed = true;
        result.error = "unexpected token";
    }
    return result;
}

Json_Result json_solve_special_characters(Arena *arena, String *str, String_View sv, size_t loc) {
    Json_Result result = {0};
    size_t backslash_index;
    while (sv_find(sv, '\\', &backslash_index)) {
        arena_da_append_many(arena, str, sv.data, backslash_index);
        assert(backslash_index + 1 < sv.size);
        char special_ch = sv.data[backslash_index + 1];
        switch (special_ch) {
        case '/': str_append_char(arena, str, '/'); break;
        case 'f': str_append_char(arena, str, '\f'); break;
        case 'r': str_append_char(arena, str, '\r'); break;
        case 'b': str_append_char(arena, str, '\b'); break;
        case 'n': str_append_char(arena, str, '\n'); break;
        case 't': str_append_char(arena, str, '\t'); break;
        case '0': str_append_char(arena, str, '\0'); break;
        case '\'': str_append_char(arena, str, '\''); break;
        case '\"': str_append_char(arena, str, '\"'); break;
        case '\\': str_append_char(arena, str, '\\'); break;
        default:
            result.failed = true;
            result.error = "invalid special character";
            result.error_loc = loc;
            return result;
        }
        sv.data = sv.data + backslash_index + 2;
        sv.size = sv.size - backslash_index - 2;
    }
    arena_da_append_many(arena, str, sv.data, sv.size);
    return result;
}

Json_Result json_parse_object(Arena *arena, Json_Lexer *lexer, Json_Object *object) {
    Json_Token token = {0};
    Json_Result result = json_lexer_next(lexer, &token);
    if (result.failed) {
        return result;
    }
    switch (token.kind) {
    case JSON_TOKEN_OPEN_CURLY: {
        object->kind = JSON_OBJ_DICT;
        while (true) {
            Json_Token peek = {0};
            Json_Result result = json_lexer_peek(lexer, &peek);
            if (result.failed) {
                return result;
            }
            if (peek.kind == JSON_TOKEN_CLOSE_CURLY) {
                json_lexer_next(lexer, &peek);
                break;
            }
            Json_Key_Value_Pair pair = {0};
            result = json_parse_object(arena, lexer, &pair.key);
            if (result.failed) {
                return result;
            }
            result = json_parse_expect(lexer, JSON_TOKEN_COLON);
            if (result.failed) {
                return result;
            }
            result = json_parse_object(arena, lexer, &pair.value);
            if (result.failed) {
                return result;
            }
            arena_da_append(arena, &object->as.dict, pair);
            result = json_lexer_peek(lexer, &peek);
            if (result.failed) {
                return result;
            }
            if (peek.kind == JSON_TOKEN_COMMA) {
                json_lexer_next(lexer, &peek);
            } else if (peek.kind != JSON_TOKEN_CLOSE_CURLY) {
                result.failed = true;
                result.error = "unexpected token";
                result.error_loc = peek.loc;
                return result;
            }
        }
    } break;
    case JSON_TOKEN_OPEN_BRACKET: {
        object->kind = JSON_OBJ_ARRAY;
        while (true) {
            Json_Token peek = {0};
            Json_Result result = json_lexer_peek(lexer, &peek);
            if (result.failed) {
                return result;
            }
            if (peek.kind == JSON_TOKEN_CLOSE_BRACKET) {
                json_lexer_next(lexer, &peek);
                break;
            }
            Json_Object item = {0};
            result = json_parse_object(arena, lexer, &item);
            if (result.failed) {
                return result;
            }
            arena_da_append(arena, &object->as.array, item);
            result = json_lexer_peek(lexer, &peek);
            if (result.failed) {
                return result;
            }
            if (peek.kind == JSON_TOKEN_COMMA) {
                json_lexer_next(lexer, &peek);
            } else if (peek.kind != JSON_TOKEN_CLOSE_BRACKET) {
                result.failed = true;
                result.error = "unexpected token";
                result.error_loc = peek.loc;
                return result;
            }
        }
    } break;
    case JSON_TOKEN_TRUE: {
        object->kind = JSON_OBJ_BOOLEAN;
        object->as.boolean = true;
    } break;
    case JSON_TOKEN_FALSE: {
        object->kind = JSON_OBJ_BOOLEAN;
        object->as.boolean = false;
    } break;
    case JSON_TOKEN_NULL: {
        object->kind = JSON_OBJ_NULL;
    } break;
    case JSON_TOKEN_INT64: {
        object->kind = JSON_OBJ_INT64;
        object->as.int64 = sv_to_int64(token.text);
    } break;
    case JSON_TOKEN_DECIMAL: {
        object->kind = JSON_OBJ_DECIMAL;
        object->as.decimal = sv_to_decimal(token.text);
    } break;
    case JSON_TOKEN_STRING: {
        object->kind = JSON_OBJ_STRING;
        result = json_solve_special_characters(
            arena, &object->as.string, token.text, token.loc);
    } break;
    case JSON_TOKEN_END: {
        result.failed = true;
        result.error = "unexpected end of json";
        result.error_loc = token.loc;
    } break;
    default: {
        result.failed = true;
        result.error = "unexpected token";
        result.error_loc = token.loc;
    }
    }
    return result;
}

Json_Result json_parse(Arena *arena, Json_Object *object, const char *data, size_t size) {
    String_View view = { data, size };
    Json_Lexer lexer = { view, 0 };
    return json_parse_object(arena, &lexer, object);
}

Json_Object json_obj_string(Arena *arena, const char *cstr) {
    Json_Object obj = {0};
    String str = str_with_cap(arena, strlen(cstr));
    str_append_cstr(arena, &str, cstr);
    obj.kind = JSON_OBJ_STRING;
    obj.as.string = str;
    return obj;
}

bool json_obj_eq(Json_Object *a, Json_Object *b) {
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
    case JSON_OBJ_NULL:    return true;
    case JSON_OBJ_DICT:    return (uintptr_t)a == (uintptr_t)b;
    case JSON_OBJ_ARRAY:   return (uintptr_t)a == (uintptr_t)b;
    case JSON_OBJ_BOOLEAN: return a->as.boolean == b->as.boolean;
    case JSON_OBJ_INT64:   return a->as.int64 == b->as.int64;
    case JSON_OBJ_DECIMAL: return fabs(a->as.decimal - b->as.decimal) < DBL_EPSILON;
    case JSON_OBJ_STRING:  return str_eq(&a->as.string, &b->as.string);
    default: assert(0 && "unreachable");
    }
}

Json_Object *json_dict_get(Json_Dict *dict, Json_Object key) {
    for (size_t i = 0; i < dict->count; ++i) {
        Json_Key_Value_Pair *pair = &dict->items[i];
        if (json_obj_eq(&pair->key, &key)) {
            return &pair->value;
        }
    }
    return NULL;
}

Json_Object *json_array_get(Json_Array *array, size_t index) {
    assert(index < array->count);
    return &array->items[index];
}

#define X(sufix, kind_, type)                                       \
    type *json_dict_get_##sufix(Json_Dict *dict, Json_Object key) { \
        for (size_t i = 0; i < dict->count; ++i) {                  \
            Json_Key_Value_Pair *pair = &dict->items[i];            \
            if (json_obj_eq(&pair->key, &key)) {                    \
                assert(pair->value.kind == kind_);                  \
                return &pair->value.as.sufix;                       \
            }                                                       \
        }                                                           \
        return NULL;                                                \
    }                                                               \
    type *json_array_get_##sufix(Json_Array *array, size_t index) { \
        assert(index < array->count);                               \
        Json_Object *obj = &array->items[index];                    \
        assert(obj->kind == kind_);                                 \
        return &obj->as.sufix;                                      \
    }
JSON_TYPES_DEF
#undef X
