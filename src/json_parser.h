#ifndef CT_JSON_PARSER_H
#define CT_JSON_PARSER_H
#include <stddef.h>
typedef enum JsonType { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonType;
typedef struct Json {
    JsonType type; char *string; double number; int boolean;
    struct Json **items; char **keys; size_t count;
} Json;
Json *json_parse(const char *text, char *error, size_t cap);
void json_free(Json *value);
const Json *json_get(const Json *object, const char *key);
const char *json_string(const Json *value);
int json_integer(const Json *value, int fallback);
Json *json_new(JsonType type);
Json *json_text(const char *text);
Json *json_num(int value);
Json *json_bool(int value);
int json_add(Json *parent, const char *key, Json *child);
Json *json_clone(const Json *value);
char *json_write(const Json *value);
#endif
