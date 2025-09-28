#ifndef RUSTICA_ENV_H
#define RUSTICA_ENV_H

#include "gc_export.h"

#define RUSTICA_ENV_CSTRING 0x01
#define RUSTICA_ENV_CSTRING_ARRAY 0x01

extern char **saved_argv;
extern int saved_argc;

typedef struct RusticaPointerArray {
    size_t size;
    void *items[];
} RusticaPointerArray;

typedef struct RusticaValue {
    uint8_t type;
    bool data_builtin;
    bool owns_ptr;
    union {
        void *ptr;
        char data[];
        RusticaPointerArray arr[];
    };
} RusticaValue;

typedef RusticaValue *rustica_value_t;

typedef struct StringReader {
    uint32_t offset;
    uint32_t size;
    char *data;
} StringReader;

typedef struct ArrayReader {
    size_t offset;
    RusticaPointerArray *arr;
} ArrayReader;

rustica_value_t
rustica_value_new(uint8_t type, void *ptr, size_t size);

wasm_externref_obj_t
rustica_value_to_wasm(wasm_exec_env_t exec_env, rustica_value_t val);

rustica_value_t
rustica_value_from_wasm(wasm_obj_t refobj, uint8_t expected_type);

void
rustica_register_natives();

#endif
