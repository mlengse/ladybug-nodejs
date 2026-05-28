#pragma once

#include <napi.h>

// Creates Arrow C Data Interface structures for CSR rel-table tests.
// info[0] (optional string): destination column name (default: "to")
// Returns a plain object: { nodeSchemaPtr, nodeArrayPtr,
//                           indicesSchemaPtr, indicesArrayPtr,
//                           indptrSchemaPtr,  indptrArrayPtr }
// where each value is an N-API External wrapping a heap-allocated ArrowSchema/ArrowArray.
// Ownership is held by the External; it is transferred to Ladybug when the pointer is passed to
// createArrowRelTableSync.
Napi::Value CreateArrowCSRTestData(const Napi::CallbackInfo& info);
