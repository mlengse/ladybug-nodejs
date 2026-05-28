const { assert } = require("chai");
const path = require("path");
const os = require("os");
const fs = require("fs");

// Access the native module directly for test helpers
const LbugNative = require("../build/lbug_native.js");

let arrowDb, arrowConn;

before(function () {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "lbug-arrow-test-"));
  const dbPath = path.join(tmpDir, "arrow_test.lbdb");
  arrowDb = new lbug.Database(dbPath, 1 << 27);
  arrowConn = new lbug.Connection(arrowDb, 2);
});

describe("createArrowRelTableSync CSR", function () {
  it("should throw when dstColName is not a string in CSR mode", function () {
    const { nodeSchemaPtr, nodeArrayPtr, indicesSchemaPtr, indicesArrayPtr,
      indptrSchemaPtr, indptrArrayPtr } = LbugNative.createArrowCSRTestData("to");

    arrowConn.createArrowTableSync("person_err", nodeSchemaPtr, nodeArrayPtr, 1);

    assert.throws(() => {
      arrowConn.createArrowRelTableSync(
        "knows_err", "person_err", "person_err",
        indicesSchemaPtr, indicesArrayPtr, 1,
        indptrSchemaPtr, indptrArrayPtr, 1,
        42 /* not a string */
      );
    }, /dstColName must be a string/);
  });

  it("should create a CSR rel table with the default dstColName", function () {
    const { nodeSchemaPtr, nodeArrayPtr, indicesSchemaPtr, indicesArrayPtr,
      indptrSchemaPtr, indptrArrayPtr } = LbugNative.createArrowCSRTestData("to");

    arrowConn.createArrowTableSync("person_default", nodeSchemaPtr, nodeArrayPtr, 1);

    // Use default dstColName "to" (omit last optional param)
    arrowConn.createArrowRelTableSync(
      "knows_default", "person_default", "person_default",
      indicesSchemaPtr, indicesArrayPtr, 1,
      indptrSchemaPtr, indptrArrayPtr, 1
    );

    // Verify edges: person0→person1 (w=10), person0→person2 (w=20), person1→person2 (w=30)
    const rows = arrowConn.querySync(
      "MATCH (a:person_default)-[r:knows_default]->(b:person_default) " +
      "RETURN a.id, r.weight, b.id ORDER BY a.id, b.id"
    ).getAllSync();

    assert.deepEqual(
      rows.map(r => [Number(r["a.id"]), Number(r["r.weight"]), Number(r["b.id"])]),
      [[0, 10, 1], [0, 20, 2], [1, 30, 2]]
    );
  });

  it("should create a CSR rel table with a custom dstColName", function () {
    const dstColName = "destination";
    const { nodeSchemaPtr, nodeArrayPtr, indicesSchemaPtr, indicesArrayPtr,
      indptrSchemaPtr, indptrArrayPtr } = LbugNative.createArrowCSRTestData(dstColName);

    arrowConn.createArrowTableSync("person_custom", nodeSchemaPtr, nodeArrayPtr, 1);

    arrowConn.createArrowRelTableSync(
      "knows_custom", "person_custom", "person_custom",
      indicesSchemaPtr, indicesArrayPtr, 1,
      indptrSchemaPtr, indptrArrayPtr, 1,
      dstColName
    );

    const rows = arrowConn.querySync(
      "MATCH (a:person_custom)-[r:knows_custom]->(b:person_custom) " +
      "RETURN a.id, r.weight, b.id ORDER BY a.id, b.id"
    ).getAllSync();

    assert.deepEqual(
      rows.map(r => [Number(r["a.id"]), Number(r["r.weight"]), Number(r["b.id"])]),
      [[0, 10, 1], [0, 20, 2], [1, 30, 2]]
    );
  });

  it("should fall through to flat mode when no CSR params are passed", function () {
    // Fresh data since each call to createArrowTableSync transfers ownership
    const d = LbugNative.createArrowCSRTestData("to");

    arrowConn.createArrowTableSync("person_flat_test", d.nodeSchemaPtr, d.nodeArrayPtr, 1);

    // Flat path requires "from"/"to" columns in the Arrow batch.
    // Passing a batch with only "id" should trigger a Ladybug error (confirming flat path is used).
    const d2 = LbugNative.createArrowCSRTestData("to");
    assert.throws(() => {
      arrowConn.createArrowRelTableSync(
        "knows_flat_test", "person_flat_test", "person_flat_test",
        d2.nodeSchemaPtr, d2.nodeArrayPtr, 1
        // no CSR params → flat path
      );
    });
  });
});

