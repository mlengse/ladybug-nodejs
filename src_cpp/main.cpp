#include "include/node_connection.h"
#include "include/node_database.h"
#include "include/node_query_result.h"
#include <napi.h>

#ifdef LBUG_ENABLE_TEST_EXPORTS
#include "include/node_arrow_test_utils.h"
#endif

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    NodeConnection::Init(env, exports);
    NodeDatabase::Init(env, exports);
    NodePreparedStatement::Init(env, exports);
    NodeQueryResult::Init(env, exports);
#ifdef LBUG_ENABLE_TEST_EXPORTS
    exports.Set("createArrowCSRTestData", Napi::Function::New(env, CreateArrowCSRTestData));
#endif
    return exports;
}

NODE_API_MODULE(addon, InitAll);
