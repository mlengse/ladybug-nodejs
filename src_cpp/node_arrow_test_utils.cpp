#include "include/node_arrow_test_utils.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common/arrow/arrow.h"
#include <napi.h>

namespace {

// Fill a leaf (non-struct) ArrowSchema in place.
static void fillLeafSchema(ArrowSchema* s, const char* format, const char* name) {
    s->format = format;
    s->name = name;
    s->metadata = nullptr;
    s->flags = ARROW_FLAG_NULLABLE;
    s->n_children = 0;
    s->children = nullptr;
    s->dictionary = nullptr;
    s->release = [](ArrowSchema* s) { s->release = nullptr; };
    s->private_data = nullptr;
}

// Fill a struct ArrowSchema in place. `children` must already be malloc'd (n_fields pointers).
static void fillStructSchema(ArrowSchema* s, ArrowSchema** children, int64_t nFields) {
    s->format = "+s";
    s->name = nullptr;
    s->metadata = nullptr;
    s->flags = 0;
    s->n_children = nFields;
    s->children = children;
    s->dictionary = nullptr;
    s->private_data = nullptr;
    s->release = [](ArrowSchema* s) {
        if (s->children) {
            for (int64_t i = 0; i < s->n_children; ++i) {
                if (s->children[i] && s->children[i]->release) {
                    s->children[i]->release(s->children[i]);
                }
                free(s->children[i]);
            }
            free(s->children);
        }
        s->release = nullptr;
    };
}

// Allocate a leaf ArrowArray on the heap and fill it with typed data.
template<typename T>
static ArrowArray* makeLeafArray(const std::vector<T>& data) {
    auto* a = static_cast<ArrowArray*>(malloc(sizeof(ArrowArray)));
    auto* buf = malloc(data.size() * sizeof(T));
    memcpy(buf, data.data(), data.size() * sizeof(T));
    a->length = static_cast<int64_t>(data.size());
    a->null_count = 0;
    a->offset = 0;
    a->n_buffers = 2;
    a->n_children = 0;
    a->buffers = static_cast<const void**>(malloc(sizeof(void*) * 2));
    a->buffers[0] = nullptr;
    a->buffers[1] = buf;
    a->children = nullptr;
    a->dictionary = nullptr;
    a->private_data = nullptr;
    a->release = [](ArrowArray* a) {
        if (a->buffers) {
            free(const_cast<void*>(a->buffers[1]));
            free(const_cast<void**>(a->buffers));
        }
        a->release = nullptr;
    };
    return a;
}

// Allocate a struct ArrowArray on the heap with pre-built children.
static ArrowArray* makeStructArray(int64_t length, ArrowArray** children, int64_t nChildren) {
    auto* a = static_cast<ArrowArray*>(malloc(sizeof(ArrowArray)));
    a->length = length;
    a->null_count = 0;
    a->offset = 0;
    a->n_buffers = 1;
    a->n_children = nChildren;
    a->buffers = static_cast<const void**>(malloc(sizeof(void*)));
    a->buffers[0] = nullptr;
    a->children = children;
    a->dictionary = nullptr;
    a->private_data = nullptr;
    a->release = [](ArrowArray* a) {
        if (a->children) {
            for (int64_t i = 0; i < a->n_children; ++i) {
                if (a->children[i] && a->children[i]->release) {
                    a->children[i]->release(a->children[i]);
                }
                free(a->children[i]);
            }
            free(a->children);
        }
        if (a->buffers) {
            free(const_cast<void**>(a->buffers));
        }
        a->release = nullptr;
    };
    return a;
}

// Wrap a heap-allocated ArrowSchema (new'd) in an N-API External.
// The finalizer calls release (if set) then deletes the struct.
static Napi::External<ArrowSchema> toSchemaExternal(Napi::Env env, ArrowSchema* s) {
    return Napi::External<ArrowSchema>::New(env, s, [](Napi::Env, ArrowSchema* s) {
        if (s->release) {
            s->release(s);
        }
        delete s;
    });
}

// Wrap a heap-allocated ArrowArray (malloc'd) in an N-API External.
// The finalizer calls release (if set) then frees the struct.
static Napi::External<ArrowArray> toArrayExternal(Napi::Env env, ArrowArray* a) {
    return Napi::External<ArrowArray>::New(env, a, [](Napi::Env, ArrowArray* a) {
        if (a->release) {
            a->release(a);
        }
        free(a);
    });
}

} // namespace

Napi::Value CreateArrowCSRTestData(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::string dstColName = "to";
    if (info.Length() > 0 && info[0].IsString()) {
        dstColName = info[0].As<Napi::String>().Utf8Value();
    }

    // ----- Node table: struct { id: int64 } with 3 rows [0, 1, 2] -----
    auto* nodeSchema = new ArrowSchema;
    auto** nc = static_cast<ArrowSchema**>(malloc(sizeof(ArrowSchema*)));
    nc[0] = static_cast<ArrowSchema*>(malloc(sizeof(ArrowSchema)));
    fillLeafSchema(nc[0], "l", "id");
    fillStructSchema(nodeSchema, nc, 1);

    auto** na = static_cast<ArrowArray**>(malloc(sizeof(ArrowArray*)));
    na[0] = makeLeafArray<int64_t>({0, 1, 2});
    auto* nodeArray = makeStructArray(3, na, 1);

    // ----- CSR indices: struct { <dstColName>: uint64, weight: int64 } -----
    // Edges: person0→person1 (w=10), person0→person2 (w=20), person1→person2 (w=30)
    auto* indicesSchema = new ArrowSchema;
    auto** ic = static_cast<ArrowSchema**>(malloc(sizeof(ArrowSchema*) * 2));
    ic[0] = static_cast<ArrowSchema*>(malloc(sizeof(ArrowSchema)));
    ic[1] = static_cast<ArrowSchema*>(malloc(sizeof(ArrowSchema)));
    // dstColName is dynamic — strdup it and store in private_data so the child release frees it.
    char* nameBuf = strdup(dstColName.c_str());
    fillLeafSchema(ic[0], "L", nameBuf);
    ic[0]->private_data = nameBuf;
    ic[0]->release = [](ArrowSchema* s) {
        free(s->private_data);
        s->release = nullptr;
    };
    fillLeafSchema(ic[1], "l", "weight");
    fillStructSchema(indicesSchema, ic, 2);

    auto** ia = static_cast<ArrowArray**>(malloc(sizeof(ArrowArray*) * 2));
    ia[0] = makeLeafArray<uint64_t>({1, 2, 2});
    ia[1] = makeLeafArray<int64_t>({10, 20, 30});
    auto* indicesArray = makeStructArray(3, ia, 2);

    // ----- CSR indptr: struct { v: uint64 } -----
    // Offsets: person0 → edges[0,2), person1 → edges[2,3), person2 → edges[3,3)
    auto* indptrSchema = new ArrowSchema;
    auto** pc = static_cast<ArrowSchema**>(malloc(sizeof(ArrowSchema*)));
    pc[0] = static_cast<ArrowSchema*>(malloc(sizeof(ArrowSchema)));
    fillLeafSchema(pc[0], "L", "v");
    fillStructSchema(indptrSchema, pc, 1);

    auto** pa = static_cast<ArrowArray**>(malloc(sizeof(ArrowArray*)));
    pa[0] = makeLeafArray<uint64_t>({0, 2, 3, 3});
    auto* indptrArray = makeStructArray(4, pa, 1);

    auto result = Napi::Object::New(env);
    result.Set("nodeSchemaPtr", toSchemaExternal(env, nodeSchema));
    result.Set("nodeArrayPtr", toArrayExternal(env, nodeArray));
    result.Set("indicesSchemaPtr", toSchemaExternal(env, indicesSchema));
    result.Set("indicesArrayPtr", toArrayExternal(env, indicesArray));
    result.Set("indptrSchemaPtr", toSchemaExternal(env, indptrSchema));
    result.Set("indptrArrayPtr", toArrayExternal(env, indptrArray));
    return result;
}
