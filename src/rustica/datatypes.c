// SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#include "postgres.h"
#include "utils/builtins.h"

#include "rustica/datatypes.h"
#include "rustica/wamr.h"

static void
obj_finalizer(wasm_obj_t wasm_obj, void *ptr) {
    obj_t obj = (obj_t)wasm_anyref_obj_get_value((wasm_anyref_obj_t)wasm_obj);
    wasm_exec_env_t exec_env = (wasm_exec_env_t)ptr;

    switch (obj->type) {
        case OBJ_DATUM:
            if (obj->flags & OBJ_OWNS_BODY)
                pfree(DatumGetPointer(obj->body.datum));
            break;

        case OBJ_STRING_INFO:
            if (obj->flags & OBJ_OWNS_BODY_MEMBERS)
                pfree(obj->body.sb->data);
            if (obj->flags & OBJ_OWNS_BODY)
                pfree(obj->body.sb);
            break;

        case OBJ_JSONB_VALUE:
            if (obj->flags & OBJ_OWNS_BODY_MEMBERS)
                switch (obj->body.jbv->type) {
                    case jbvString:
                        pfree(obj->body.jbv->val.string.val);
                        break;
                    case jbvArray:
                        pfree(obj->body.jbv->val.array.elems);
                        break;
                    case jbvObject:
                        pfree(obj->body.jbv->val.object.pairs);
                        break;
                    default:
                        break;
                }
            if (obj->flags & OBJ_OWNS_BODY)
                pfree(obj->body.jbv);
            break;

        case OBJ_PORTAL:
            if (PortalIsValid(obj->body.portal))
                SPI_cursor_close(obj->body.portal);
            break;

        case OBJ_TUPLE_TABLE:
            SPI_freetuptable(obj->body.tuptable);
            break;

        case OBJ_HEAP_TUPLE:
            break;

        default:
            break;
    }

    if (obj->flags & OBJ_REFERENCING) {
        wasm_runtime_remove_local_obj_ref(exec_env, obj->ref);
    }

    pfree(obj);
}

wasm_externref_obj_t
rst_externref_of_obj(wasm_exec_env_t exec_env, obj_t obj) {
    wasm_externref_obj_t rv = wasm_externref_obj_new(exec_env, obj);
    wasm_obj_set_gc_finalizer(exec_env,
                              wasm_externref_obj_to_internal_obj(rv),
                              obj_finalizer,
                              exec_env);
    return rv;
}

wasm_obj_t
rst_anyref_of_obj(wasm_exec_env_t exec_env, obj_t obj) {
    wasm_obj_t rv = (wasm_obj_t)wasm_anyref_obj_new(exec_env, obj);
    wasm_obj_set_gc_finalizer(exec_env, rv, obj_finalizer, exec_env);
    return rv;
}

obj_t
rst_obj_new(wasm_exec_env_t exec_env,
            ObjType type,
            wasm_obj_t ref,
            size_t embed_size) {
    size_t size = sizeof(Obj) + embed_size;
    if (ref != NULL)
        size += sizeof(wasm_local_obj_ref_t);

    obj_t rv = (obj_t)palloc(size);
    rv->type = type;
    rv->flags = 0;

    if (ref != NULL) {
        Assert(exec_env != NULL);
        wasm_runtime_push_local_obj_ref(exec_env, rv->ref);
        rv->ref->val = ref;
        rv->flags |= OBJ_REFERENCING;
        if (embed_size > 0)
            rv->body.ptr = rv->ref + 1;
    }
    else if (embed_size > 0)
        rv->body.ptr = rv->ref;

    return rv;
}

WASMValue *
rst_obj_new_static(ObjType type, obj_t *obj_out, size_t embed_size) {
    obj_t obj = rst_obj_new(NULL,
                            type,
                            NULL,
                            sizeof(WASMValue) + sizeof(WASMExternrefObject)
                                + sizeof(WASMAnyrefObject) + embed_size);
    *obj_out = obj;
    char *ptr = obj->body.ptr;
    WASMValue *rv = (WASMValue *)ptr;
    ptr += sizeof(WASMValue);
    wasm_externref_obj_t externref = (wasm_externref_obj_t)ptr;
    ptr += sizeof(WASMExternrefObject);
    wasm_anyref_obj_t anyref = (wasm_anyref_obj_t)ptr;
    if (embed_size > 0) {
        obj->body.ptr = ptr + sizeof(WASMAnyrefObject);
    }

    anyref->header = WASM_OBJ_ANYREF_OBJ_FLAG;
    anyref->host_obj = obj;
    externref->header = WASM_OBJ_EXTERNREF_OBJ_FLAG | WASM_OBJ_STATIC_OBJ_FLAG;
    externref->internal_obj = (WASMObjectRef)anyref;
    rv->gc_obj = (wasm_obj_t)externref;
    return rv;
}

obj_t
wasm_externref_obj_get_obj(wasm_obj_t refobj, ObjType type) {
    if (!wasm_obj_is_externref_obj(refobj))
        ereport(ERROR, errmsg("not an externref object"));
    wasm_obj_t anyref =
        wasm_externref_obj_to_internal_obj((wasm_externref_obj_t)refobj);
    if (!wasm_obj_is_anyref_obj(anyref))
        ereport(ERROR, errmsg("not an externref of anyref object"));
    obj_t rv = (obj_t)wasm_anyref_obj_get_value((wasm_anyref_obj_t)anyref);
    if (rv->type != type)
        ereport(ERROR,
                errmsg("expected object type %d, got %d", type, rv->type));
    return rv;
}

Datum
wasm_externref_obj_get_datum(wasm_obj_t refobj, Oid oid) {
    obj_t obj = wasm_externref_obj_get_obj(refobj, OBJ_DATUM);
    if (obj->oid != oid)
        ereport(ERROR, errmsg("expected OID %d, got %d", oid, obj->oid));
    PG_RETURN_DATUM(obj->body.datum);
}

char *
wasm_text_copy_cstring(wasm_obj_t refobj) {
    return TextDatumGetCString(wasm_externref_obj_get_datum(refobj, TEXTOID));
}

wasm_externref_obj_t
rst_externref_of_owned_datum(wasm_exec_env_t exec_env, Datum datum, Oid oid) {
    obj_t obj = rst_obj_new(exec_env, OBJ_DATUM, NULL, 0);
    obj->flags |= OBJ_OWNS_BODY;
    obj->body.datum = datum;
    obj->oid = oid;
    return rst_externref_of_obj(exec_env, obj);
}

wasm_externref_obj_t
cstring_into_varatt_obj(wasm_exec_env_t exec_env,
                        const void *data,
                        size_t llen,
                        Oid oid) {
    if (llen > VARATT_MAX - VARHDRSZ)
        ereport(ERROR, errmsg("varlena_new: input too long"));
    int len = (int)llen;
    bool is_short = len <= VARATT_SHORT_MAX - VARHDRSZ_SHORT;
    obj_t obj = rst_obj_new(exec_env,
                            OBJ_DATUM,
                            NULL,
                            (is_short ? VARHDRSZ_SHORT : VARHDRSZ) + len);
    obj->oid = oid;
    if (is_short)
        SET_VARSIZE_1B(obj->body.ptr, len + VARHDRSZ_SHORT);
    else
        SET_VARSIZE(obj->body.ptr, len + VARHDRSZ);
    memcpy(VARDATA_ANY(obj->body.ptr), data, len);
    return rst_externref_of_obj(exec_env, obj);
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
