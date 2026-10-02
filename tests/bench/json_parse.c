#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The C mirror of bench/json_parse.r: a recursive-descent parser that builds a heap tree
   (objects and arrays own child vectors, strings own copies) and frees it. */
typedef enum Kind { K_NULL, K_BOOL, K_NUMBER, K_STRING, K_ARRAY, K_OBJECT } Kind;
typedef struct Node { Kind kind; double number; int boolean; char *text; char **keys; struct Node *children; size_t count; size_t capacity; } Node;

static const char *document =
    "{\"users\":[{\"id\":1,\"name\":\"alice\",\"tags\":[\"admin\",\"ops\"],\"active\":true,\"score\":12.5},"
    "{\"id\":2,\"name\":\"bob\",\"tags\":[],\"active\":false,\"score\":7},{\"id\":3,\"name\":\"carol\","
    "\"tags\":[\"dev\"],\"active\":true,\"score\":99.25}],\"page\":{\"number\":4,\"size\":50,\"total\":1234,"
    "\"next\":null},\"labels\":[\"alpha\",\"beta\",\"gamma\",\"delta\",\"epsilon\"],\"matrix\":[[1,2,3],[4,5,6],"
    "[7,8,9]],\"flags\":{\"verbose\":false,\"dry_run\":true,\"retries\":3,\"timeout\":30.5},"
    "\"description\":\"a short description of the payload used by the benchmark\"}";

static const char *cursor;
static int failed;

static void skip(void) { while (*cursor == ' ' || *cursor == '\n' || *cursor == '\t' || *cursor == '\r') cursor++; }

static char *parse_string(void) {
    if (*cursor != '"') { failed = 1; return NULL; }
    cursor++;
    const char *start = cursor;
    while (*cursor != '"' && *cursor != '\0') { if (*cursor == '\\') cursor++; cursor++; }
    if (*cursor != '"') { failed = 1; return NULL; }
    size_t length = (size_t)(cursor - start);
    char *copy = malloc(length + 1U);
    if (copy == NULL) { failed = 1; return NULL; }
    memcpy(copy, start, length); copy[length] = '\0';
    cursor++;
    return copy;
}

static int push_child(Node *parent, Node child, char *key) {
    if (parent->count == parent->capacity) {
        size_t next = parent->capacity == 0U ? 4U : parent->capacity * 2U;
        Node *grown = realloc(parent->children, next * sizeof(Node));
        char **grown_keys = parent->kind == K_OBJECT ? realloc(parent->keys, next * sizeof(char *)) : NULL;
        if (grown == NULL || (parent->kind == K_OBJECT && grown_keys == NULL)) { failed = 1; return 1; }
        parent->children = grown; if (parent->kind == K_OBJECT) parent->keys = grown_keys; parent->capacity = next;
    }
    parent->children[parent->count] = child;
    if (parent->kind == K_OBJECT) parent->keys[parent->count] = key;
    parent->count++;
    return 0;
}

static Node parse_value(void);

static Node parse_container(Kind kind) {
    Node node = {0}; node.kind = kind;
    char close = kind == K_ARRAY ? ']' : '}';
    cursor++; skip();
    if (*cursor == close) { cursor++; return node; }
    for (;;) {
        char *key = NULL;
        skip();
        if (kind == K_OBJECT) { key = parse_string(); if (failed) return node; skip(); if (*cursor != ':') { failed = 1; return node; } cursor++; }
        Node child = parse_value();
        if (failed) return node;
        if (push_child(&node, child, key)) return node;
        skip();
        if (*cursor == ',') { cursor++; continue; }
        if (*cursor == close) { cursor++; return node; }
        failed = 1; return node;
    }
}

static Node parse_value(void) {
    Node node = {0};
    skip();
    switch (*cursor) {
    case '{': return parse_container(K_OBJECT);
    case '[': return parse_container(K_ARRAY);
    case '"': node.kind = K_STRING; node.text = parse_string(); return node;
    case 't': if (strncmp(cursor, "true", 4) == 0) { cursor += 4; node.kind = K_BOOL; node.boolean = 1; return node; } break;
    case 'f': if (strncmp(cursor, "false", 5) == 0) { cursor += 5; node.kind = K_BOOL; return node; } break;
    case 'n': if (strncmp(cursor, "null", 4) == 0) { cursor += 4; node.kind = K_NULL; return node; } break;
    default: {
        char *end = NULL;
        node.kind = K_NUMBER; node.number = strtod(cursor, &end);
        if (end == cursor) break;
        cursor = end; return node;
    }
    }
    failed = 1; return node;
}

static void free_node(Node *node) {
    for (size_t i = 0; i < node->count; ++i) free_node(&node->children[i]);
    if (node->kind == K_OBJECT) { for (size_t i = 0; i < node->count; ++i) free(node->keys[i]); free(node->keys); }
    free(node->children); free(node->text);
}

int main(void) {
    const size_t iterations = (size_t)200000u;
    size_t total = 0u;
    for (size_t index = 0; index < iterations; ++index) {
        cursor = document; failed = 0;
        Node root = parse_value();
        if (failed) { free_node(&root); return 65; }
        total += root.count;
        free_node(&root);
    }
    return (int)(total % 109u);
}
