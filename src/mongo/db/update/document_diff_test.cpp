/**
 *    Copyright (C) 2020-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest

#include "mongo/platform/basic.h"

#include "mongo/bson/bson_depth.h"
#include "mongo/bson/json.h"
#include "mongo/db/update/document_diff_applier.h"
#include "mongo/db/update/document_diff_calculator.h"
#include "mongo/db/update/document_diff_test_helpers.h"
#include "mongo/db/update/update_oplog_entry_serialization.h"
#include "mongo/logv2/log.h"
#include "mongo/platform/random.h"
#include "mongo/unittest/bson_test_util.h"
#include "mongo/unittest/unittest.h"

namespace mongo::doc_diff {
namespace {

// We use the same seed and random number generator throughout the tests to be able to reproduce a
// failure easily.
auto getSeed() {
    const static auto seed = std::random_device()();
    return seed;
}
PseudoRandom* getRNG() {
    static auto rng = PseudoRandom(getSeed());
    return &rng;
}

std::vector<BSONObj> getDocumentsRepo() {
    const static std::vector<BSONObj> documents = {
        createObjWithLargePrefix("{}"),  // Empty object.
        // Simple subobjects.
        createObjWithLargePrefix("{field1: {level1Field1: {level2Field1: 'val'}}, field2: "
                                 "{level1Field1: {}}, field3: {level1Field3: 'va'}}"),
        createObjWithLargePrefix(
            "{field1: {level1Field1: {level1Field1: 1}}, field2: {level1Field1: {}}, field3: "
            "{level1Field3: 'va2'}, field4: ['arrayVal1']}"),

        // Simple arrays.
        createObjWithLargePrefix(
            "{field1: ['arrayVal1', 'arrayVal2'], field2: ['arrayVal1', 'arrayVal2', 'arrayVal3' ],"
            "field4: {}, field3: null}"),
        createObjWithLargePrefix("{field2: ['arrayVal1', ['subArrayVal1', 'subArrayVal2', "
                                 "'subArrayVal3'], 'val'], field1: ['arrayVal1', 'arrayVal2'], "
                                 "field3: ['arrayVal1', 'arrayVal2']}"),
        createObjWithLargePrefix("{field2: ['arrayVal1', ['subArrayVal1','subArrayVal2', "
                                 "'subArrayVal', 'val' ], 'val'], field1: ['arrayVal1', "
                                 "'arrayVal2'], field3: ['arrayVal1', 'arrayVal2']}"),

        // Array and sub-object combination.
        createObjWithLargePrefix("{field1: {level1Field1: [{level1Field1: [1]}]}, field0: "
                                 "{level1Field1: {}}, field3: {level1Field3: 'val2'}}"),
        createObjWithLargePrefix("{field1: {level1Field1: [{level1Field1: [1, 2]}]}, field2: "
                                 "{level1Field1: {}}, field3: {level1Field3: ['val']}}"),
        createObjWithLargePrefix(
            "{field3: {level1Field1: [{level1Field1: [1, 2]}]}, field1: "
            "{level1Field1: {}}, field2: {level1Field3: ['val']}}, field4: [[]]"),

        // Unrelated documents.
        createObjWithLargePrefix(
            "{newField1: {level1Field1: [{level1Field1: [1, 2]}]}, newField2: {level1Field1: {}}, "
            "newField4: {level1Field3: ['val']}}, newField3: [[]]"),
        createObjWithLargePrefix(
            "{newField2: {level1Field1: {}}, newField1: {level1Field1: [{level1Field1: [1, 2]}]},"
            "newField4: {level1Field3: ['val']}}, newField3: [[]]"),
        createObjWithLargePrefix(
            "{newField3: {level1Field1: [{level1Field1: [1, 2]}]}, newField2: {level1Field1: {}}, "
            "newField4: {level1Field3: ['val']}}, newField1: [[]]"),
    };
    return documents;
}

void runTest(std::vector<BSONObj> documents, size_t numSimulations) {
    // Shuffle them into a random order
    auto rng = getRNG();
    LOGV2(4785301, "Seed used for the test ", "seed"_attr = getSeed());
    for (size_t simulation = 0; simulation < numSimulations; ++simulation) {
        std::shuffle(documents.begin(), documents.end(), rng->urbg());

        auto preDoc = documents[0];
        std::vector<BSONObj> diffs;
        diffs.reserve(documents.size() - 1);
        for (size_t i = 1; i < documents.size(); ++i) {
            const auto diff = computeDiff(
                preDoc, documents[i], update_oplog_entry::kSizeOfDeltaOplogEntryMetadata);

            ASSERT(diff);
            diffs.push_back(*diff);
            const auto postObj = applyDiff(preDoc, *diff);
            ASSERT_BSONOBJ_BINARY_EQ(documents[i], postObj);

            // Applying the diff the second time also generates the same object.
            ASSERT_BSONOBJ_BINARY_EQ(postObj, applyDiff(postObj, *diff));

            preDoc = documents[i];
        }

        // Verify that re-applying any suffix of the diffs in the sequence order will end produce
        // the same end state.
        for (size_t start = 0; start < diffs.size(); ++start) {
            auto endObj = documents.back();
            for (size_t i = start; i < diffs.size(); ++i) {
                endObj = applyDiff(endObj, diffs[i]);
            }
            ASSERT_BSONOBJ_BINARY_EQ(endObj, documents.back());
        }
    }
}
TEST(DocumentDiffTest, PredefinedDocumentsTest) {
    runTest(getDocumentsRepo(), 10);
}

TEST(DocumentDiffTest, RandomizedDocumentBuilderTest) {
    const auto numDocs = 20;
    std::vector<BSONObj> documents(numDocs);
    auto rng = getRNG();
    for (int i = 0; i < numDocs; ++i) {
        MutableDocument doc;
        documents[i] = generateDoc(rng, &doc, 0);
    }
    runTest(std::move(documents), 10);
}

}  // namespace
}  // namespace mongo::doc_diff
