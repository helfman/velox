/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "velox/exec/tests/OperatorTestBase.h"
#include "velox/exec/tests/PlanBuilder.h"

using namespace facebook::velox;
using namespace facebook::velox::exec::test;

class LimitTest : public OperatorTestBase {};

TEST_F(LimitTest, basic) {
  vector_size_t batchSize = 1'000;
  std::vector<RowVectorPtr> vectors;
  for (int32_t i = 0; i < 3; ++i) {
    auto c0 = makeFlatVector<int64_t>(
        batchSize, [&](auto row) { return batchSize * i + row; }, nullEvery(5));
    auto c1 = makeFlatVector<int32_t>(
        batchSize, [&](auto row) { return row; }, nullEvery(7));
    auto c2 = makeFlatVector<double>(
        batchSize, [](auto row) { return row * 0.1; }, nullEvery(11));
    vectors.push_back(makeRowVector({c0, c1, c2}));
  }
  createDuckDbTable(vectors);

  auto makePlan = [&](int32_t offset, int32_t limit) {
    return PlanBuilder().values(vectors).limit(offset, limit, true).planNode();
  };

  assertQuery(makePlan(0, 10), "SELECT * FROM tmp LIMIT 10");
  assertQuery(makePlan(0, 1'000), "SELECT * FROM tmp LIMIT 1000");
  assertQuery(makePlan(0, 1'234), "SELECT * FROM tmp LIMIT 1234");

  assertQuery(makePlan(17, 10), "SELECT * FROM tmp OFFSET 17 LIMIT 10");
  assertQuery(makePlan(17, 983), "SELECT * FROM tmp OFFSET 17 LIMIT 983");
  assertQuery(makePlan(17, 1'000), "SELECT * FROM tmp OFFSET 17 LIMIT 1000");
  assertQuery(makePlan(17, 2'000), "SELECT * FROM tmp OFFSET 17 LIMIT 2000");

  assertQuery(makePlan(1'000, 145), "SELECT * FROM tmp OFFSET 1000 LIMIT 145");
  assertQuery(
      makePlan(1'000, 1'000), "SELECT * FROM tmp OFFSET 1000 LIMIT 1000");
  assertQuery(
      makePlan(1'000, 1'234), "SELECT * FROM tmp OFFSET 1000 LIMIT 1234");

  assertQuery(makePlan(1'234, 10), "SELECT * FROM tmp OFFSET 1234 LIMIT 10");
  assertQuery(makePlan(1'234, 983), "SELECT * FROM tmp OFFSET 1234 LIMIT 983");
  assertQuery(
      makePlan(1'234, 1'000), "SELECT * FROM tmp OFFSET 1234 LIMIT 1000");
  assertQuery(
      makePlan(1'234, 2'000), "SELECT * FROM tmp OFFSET 1234 LIMIT 2000");

  assertQueryReturnsEmptyResult(makePlan(12'345, 10));
}
