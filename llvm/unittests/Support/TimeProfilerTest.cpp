//===- unittests/TimeProfilerTest.cpp - TimeProfiler tests ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// These are bare-minimum 'smoke' tests of the time profiler. Not tested:
//  - multi-threading
//  - elision of short or ill-formed entries
//  - detail callback
//  - no calls to now() if profiling is disabled
//===----------------------------------------------------------------------===//

#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/JSON.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

void setupProfiler() {
  timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0, "test");
}

std::string teardownProfiler() {
  SmallVector<char, 1024> smallVector;
  raw_svector_ostream os(smallVector);
  timeTraceProfilerWrite(os);
  timeTraceProfilerCleanup();
  return os.str().str();
}

void expectTotalCount(StringRef Trace, StringRef Name, int64_t Count) {
  auto Parsed = json::parse(Trace);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << toString(Parsed.takeError());
  auto *Root = Parsed->getAsObject();
  ASSERT_NE(Root, nullptr);
  auto *Events = Root->getArray("traceEvents");
  ASSERT_NE(Events, nullptr);
  unsigned Matches = 0;
  for (const auto &Event : *Events) {
    auto *Object = Event.getAsObject();
    ASSERT_NE(Object, nullptr);
    if (Object->getString("name") != Name)
      continue;
    ++Matches;
    auto *Args = Object->getObject("args");
    ASSERT_NE(Args, nullptr);
    EXPECT_EQ(Args->getInteger("count"), Count);
  }
  EXPECT_EQ(Matches, 1u) << Name.str();
}

TEST(TimeProfiler, Total_Async_Outlives_Scope) {
  setupProfiler();

  TimeTraceProfilerEntry *Async;
  {
    TimeTraceScope Outer("outer");
    Async = timeTraceAsyncProfilerBegin("async", "detail");
  }
  timeTraceProfilerEnd(Async);

  std::string Trace = teardownProfiler();
  expectTotalCount(Trace, "Total outer", 1);
  expectTotalCount(Trace, "Total async", 1);
}

TEST(TimeProfiler, Total_Async_Outlives_Same_Name_Scope) {
  setupProfiler();

  TimeTraceProfilerEntry *Async;
  {
    TimeTraceScope Outer("event");
    Async = timeTraceAsyncProfilerBegin("event", "detail");
  }
  timeTraceProfilerEnd(Async);

  // Neither event has an earlier, still-open event of the same name when it
  // ends. A later event must not suppress the total for the earlier one.
  expectTotalCount(teardownProfiler(), "Total event", 2);
}

TEST(TimeProfiler, Total_Nested_Same_Name_Suppressed) {
  setupProfiler();

  {
    TimeTraceScope Outer("event");
    TimeTraceProfilerEntry *Async;
    {
      TimeTraceScope Inner("event");
      Async = timeTraceAsyncProfilerBegin("async", "detail");
    }
    timeTraceProfilerEnd(Async);
    { TimeTraceScope Inner("event"); }
  }

  std::string Trace = teardownProfiler();
  expectTotalCount(Trace, "Total event", 1);
  expectTotalCount(Trace, "Total async", 1);
}

TEST(TimeProfiler, Scope_Smoke) {
  setupProfiler();

  { TimeTraceScope scope("event", "detail"); }

  std::string json = teardownProfiler();
  ASSERT_TRUE(json.find(R"("name":"event")") != std::string::npos);
  ASSERT_TRUE(json.find(R"("detail":"detail")") != std::string::npos);
}

TEST(TimeProfiler, Begin_End_Smoke) {
  setupProfiler();

  timeTraceProfilerBegin("event", "detail");
  timeTraceProfilerEnd();

  std::string json = teardownProfiler();
  ASSERT_TRUE(json.find(R"("name":"event")") != std::string::npos);
  ASSERT_TRUE(json.find(R"("detail":"detail")") != std::string::npos);
}

TEST(TimeProfiler, Async_Begin_End_Smoke) {
  setupProfiler();

  auto *Profiler = timeTraceAsyncProfilerBegin("event", "detail");
  timeTraceProfilerEnd(Profiler);

  std::string json = teardownProfiler();
  ASSERT_TRUE(json.find(R"("name":"event")") != std::string::npos);
  ASSERT_TRUE(json.find(R"("detail":"detail")") != std::string::npos);
}

TEST(TimeProfiler, Begin_End_Disabled) {
  // Nothing should be observable here. The test is really just making sure
  // we've not got a stray nullptr deref.
  timeTraceProfilerBegin("event", "detail");
  timeTraceProfilerEnd();
}

TEST(TimeProfiler, Instant_Add_Smoke) {
  setupProfiler();

  timeTraceProfilerBegin("sync event", "sync detail");
  timeTraceAddInstantEvent("instant event", [&] { return "instant detail"; });
  timeTraceProfilerEnd();

  std::string json = teardownProfiler();
  ASSERT_TRUE(json.find(R"("name":"sync event")") != std::string::npos);
  ASSERT_TRUE(json.find(R"("detail":"sync detail")") != std::string::npos);
  ASSERT_TRUE(json.find(R"("name":"instant event")") != std::string::npos);
  ASSERT_TRUE(json.find(R"("detail":"instant detail")") != std::string::npos);
}

TEST(TimeProfiler, Instant_Not_Added_Smoke) {
  setupProfiler();

  timeTraceAddInstantEvent("instant event", [&] { return "instant detail"; });

  std::string json = teardownProfiler();
  ASSERT_TRUE(json.find(R"("name":"instant event")") == std::string::npos);
  ASSERT_TRUE(json.find(R"("detail":"instant detail")") == std::string::npos);
}

} // namespace
