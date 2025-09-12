/*
 * SPDX-FileCopyrightText: 2024 燕几（北京）科技有限公司
 * SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0
 */

#include "postgres.h"
#include "catalog/pg_type_d.h"

#include "wasm_runtime_common.h"
#include "wasm_c_api.h"
#include "aot_runtime.h"

#include "rustica/datatypes.h"
#include "rustica/query.h"
#include "rustica/wamr.h"

static void
native_noop(wasm_exec_env_t exec_env) {}

TidOid *tid_map = NULL;
int tid_map_len = 0;

int32_t
env_tid_to_oid(wasm_exec_env_t exec_env, wasm_obj_t obj) {
    if (tid_map == NULL || tid_map_len == 0)
        return 0;
    pg_uuid_t *tid = DatumGetUUIDP(wasm_externref_obj_get_datum(obj, UUIDOID));
    for (int i = 0; i < tid_map_len; i++) {
        if (memcmp(tid_map[i].tid->data, tid->data, UUID_LEN) == 0) {
            return (int32_t)tid_map[i].oid;
        }
    }
    return 0;
}

int32_t
env_ereport(wasm_exec_env_t exec_env, int32_t level, wasm_obj_t ref) {
    if (level == ERROR) {
        char *msg = wasm_text_copy_cstring(ref);
        ereport(WARNING,
                (errhidestmt(true),
                 errmsg_internal("WASM application panicked: %s", msg)));
        wasm_runtime_set_exception(wasm_exec_env_get_module_inst(exec_env),
                                   msg);
        pfree(msg);
    }
    else {
        Datum str = wasm_externref_obj_get_datum(ref, TEXTOID);
        text *t = DatumGetTextPP(str);
        char *msg = VARDATA_ANY(t);
        int size = VARSIZE_ANY_EXHDR(t);
        ereport(level, (errhidestmt(true), errmsg_internal("%.*s", size, msg)));
        RST_FREE_IF_COPY(t, str);
    }
    return 0;
}

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
    { "ereport", env_ereport, "(ir)i" },
    { "tid_to_oid", env_tid_to_oid, "(r)i" },
#ifdef RUSTICA_SQL_BACKDOOR
    { "sql_backdoor", native_noop, "(rii)r" },
#endif
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
    REGISTER_WASM_NATIVES("env", rst_noop_native_env);
    rst_register_natives_query();
    rst_register_natives_bytea();
    rst_register_natives_date();
    rst_register_natives_jsonb();
    rst_register_natives_json();
    rst_register_natives_primitives();
    rst_register_natives_stringbuilder();
    rst_register_natives_text();
    rst_register_natives_timestamp();
    rst_register_natives_uuid();
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

bool
wasm_ref_type_is_ref_extern(wasm_ref_type_t ref_type) {
    return ref_type.value_type == VALUE_TYPE_HT_NON_NULLABLE_REF
           && ref_type.heap_type == HEAP_TYPE_EXTERN;
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
            if (heap_type == HEAP_TYPE_EXTERN)
                return "ref extern";
            return "ref <unknown>";
        }
        case VALUE_TYPE_HT_NULLABLE_REF:
        {
            int32_t heap_type = ref_type.heap_type;
            if (heap_type == HEAP_TYPE_EXTERN)
                return "ref null extern";
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

void
wasm_runtime_remove_local_obj_ref(wasm_exec_env_t exec_env,
                                  wasm_local_obj_ref_t *me) {
    wasm_local_obj_ref_t *current =
        wasm_runtime_get_cur_local_obj_ref(exec_env);
    if (current == me)
        wasm_runtime_pop_local_obj_ref(exec_env);
    else {
        wasm_local_obj_ref_t *next;
        while (current != me) {
            next = current;
            current = current->prev;
        }
        next->prev = me->prev;
    }
}
