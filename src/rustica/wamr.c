/*
 * Copyright (c) 2024 燕几（北京）科技有限公司
 *
 * Rustica (runtime) is licensed under Mulan PSL v2. You can use this
 * software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *              http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
 * OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 */

#include <wchar.h>

#include "postgres.h"
#include "wasm_runtime_common.h"
#include "aot_runtime.h"

#include "rustica/wamr.h"

static void
spectest_print_char(wasm_exec_env_t exec_env, int c) {
    fwprintf(stderr, L"%lc", c);
}

static NativeSymbol spectest[] = {
    { "print_char", spectest_print_char, "(i)" }
};

static void
native_noop(wasm_exec_env_t exec_env) {}

NativeSymbol rst_noop_native_env[] = {
    { "recv", native_noop, "(rii)i" },
    { "send", native_noop, "(rii)i" },
    { "llhttp_execute", native_noop, "(rii)i" },
    { "llhttp_resume", native_noop, "()i" },
    { "llhttp_finish", native_noop, "(r)i" },
    { "llhttp_reset", native_noop, "()i" },
    { "llhttp_get_error_pos", native_noop, "(r)i" },
    { "llhttp_get_method", native_noop, "()i" },
    { "llhttp_get_http_major", native_noop, "()i" },
    { "llhttp_get_http_minor", native_noop, "()i" },
    { "execute_statement", native_noop, "(i)i" },
    { "detoast", native_noop, "(iii)r" },
};

void
rst_init_wamr() {
    // Initialize WAMR runtime with native stubs
    RuntimeInitArgs init_args = { .mem_alloc_type = Alloc_With_Allocator,
                                  .mem_alloc_option = {
                                      .allocator.malloc_func = palloc,
                                      .allocator.realloc_func = repalloc,
                                      .allocator.free_func = pfree,
                                  },
                                  .gc_heap_size = 16 * 1024 * 1024 };
    if (!wasm_runtime_full_init(&init_args))
        ereport(FATAL, (errmsg("cannot register WASM natives")));
    if (!wasm_runtime_register_natives("spectest",
                                       spectest,
                                       sizeof(spectest) / sizeof(spectest[0])))
        ereport(ERROR, errmsg("cannot register WASM natives"));
    if (!wasm_runtime_register_natives("env",
                                       rst_noop_native_env,
                                       sizeof(rst_noop_native_env)
                                           / sizeof(rst_noop_native_env[0])))
        ereport(ERROR, errmsg("cannot instantiate WASM module"));
}

void
rst_fini_wamr() {
    wasm_runtime_destroy();
}

wasm_struct_type_t
wasm_ref_type_get_referred_struct(wasm_ref_type_t ref_type,
                                  wasm_module_t module,
                                  bool nullable) {
    if (nullable && ref_type.value_type != VALUE_TYPE_HT_NULLABLE_REF)
        return NULL;
    if (!nullable && ref_type.value_type != VALUE_TYPE_HT_NON_NULLABLE_REF)
        return NULL;
    WASMType *type = wasm_get_defined_type(module, ref_type.heap_type);
    if (!wasm_type_is_struct_type(type))
        return NULL;
    return (wasm_struct_type_t)type;
}

wasm_array_type_t
wasm_ref_type_get_referred_array(wasm_ref_type_t ref_type,
                                 wasm_module_t module,
                                 bool nullable) {
    if (nullable && ref_type.value_type != VALUE_TYPE_HT_NULLABLE_REF)
        return NULL;
    if (!nullable && ref_type.value_type != VALUE_TYPE_HT_NON_NULLABLE_REF)
        return NULL;
    WASMType *type = wasm_get_defined_type(module, ref_type.heap_type);
    if (!wasm_type_is_array_type(type))
        return NULL;
    return (wasm_array_type_t)type;
}

wasm_func_type_t
wasm_module_lookup_exported_func(wasm_module_t module, const char *name) {
    int32 count = wasm_runtime_get_export_count(module);
    for (int i = 0; i < count; i++) {
        wasm_export_t type;
        wasm_runtime_get_export_type(module, i, &type);
        if (type.kind != WASM_IMPORT_EXPORT_KIND_FUNC)
            continue;
        if (strcmp(type.name, name) == 0) {
            return type.u.func_type;
        }
    }
    return NULL;
}

const char *
wasm_ref_type_repr(CommonHeapTypes *heap_types, wasm_ref_type_t ref_type) {
    switch (ref_type.value_type) {
        case VALUE_TYPE_I32:
            return "i32";
        case VALUE_TYPE_I64:
            return "i64";
        case VALUE_TYPE_F32:
            return "f32";
        case VALUE_TYPE_F64:
            return "f64";
        case VALUE_TYPE_V128:
            return "v128";

            /* GC Types */
        case VALUE_TYPE_I8:
            return "i8";
        case VALUE_TYPE_I16:
            return "i16";
        case VALUE_TYPE_NULLFUNCREF:
            return "ref null nofunc";
        case VALUE_TYPE_NULLEXTERNREF:
            return "ref null noextern";
        case VALUE_TYPE_NULLREF:
            return "ref null none";
        case VALUE_TYPE_FUNCREF:
            return "ref null func";
        case VALUE_TYPE_EXTERNREF:
            return "ref null extern";
        case VALUE_TYPE_ANYREF:
            return "ref null any";
        case VALUE_TYPE_EQREF:
            return "ref null eq";
        case VALUE_TYPE_I31REF:
            return "ref null i31";
        case VALUE_TYPE_STRUCTREF:
            return "ref null struct";
        case VALUE_TYPE_ARRAYREF:
            return "ref null array";
        case VALUE_TYPE_HT_NON_NULLABLE_REF:
        {
            int32_t heap_type = ref_type.heap_type;
            if (heap_type == heap_types->bytes)
                return "ref $moonbit.bytes";
            if (heap_type == heap_types->datum)
                return "ref $Ref<DatumEnum>";
            if (heap_type == heap_types->as_datum)
                return "ref $AsDatum";
            return "ref <unknown>";
        }
        case VALUE_TYPE_HT_NULLABLE_REF:
        {
            int32_t heap_type = ref_type.heap_type;
            if (heap_type == heap_types->bytes)
                return "ref null $moonbit.bytes";
            if (heap_type == heap_types->datum)
                return "ref null $Ref<DatumEnum>";
            if (heap_type == heap_types->as_datum)
                return "ref null $AsDatum";
            return "ref null <unknown>";
        }

#if WASM_ENABLE_STRINGREF != 0
            /* Stringref Types */
        case VALUE_TYPE_STRINGREF:
            return "stringref";
        case VALUE_TYPE_STRINGVIEWWTF8:
            return "stringview_wtf8";
        case VALUE_TYPE_STRINGVIEWWTF16:
            return "stringview_wtf16";
        case VALUE_TYPE_STRINGVIEWITER:
            return "stringview_iter";
#endif

        default:
            pg_unreachable();
    }
}

void
wasm_runtime_unregister_and_unload(wasm_module_t module) {
    wasm_runtime_unregister_module(module);
    if (module->module_type == Wasm_Module_Bytecode)
        wasm_unload((WASMModule *)module);
    if (module->module_type == Wasm_Module_AoT)
        aot_unload((AOTModule *)module);
}
