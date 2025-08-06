/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <fb303/Timeseries.h>

#include <gtest/gtest.h>

using namespace std;
using namespace facebook::fb303;
using std::chrono::seconds;

/*
 * Helper function to log timeseries sum data for debugging purposes in the
 * test.
 */
template <typename T>
std::ostream& operator<<(
    std::ostream& os,
    const folly::MultiLevelTimeSeries<T>& ts) {
  for (size_t n = 0; n < ts.numLevels(); ++n) {
    if (n != 0) {
      os << "/";
    }
    os << ts.sum(n);
  }
  return os;
}

TEST(MinuteHourTimeSeries, Basic) {
  using IntMHTS = MinuteHourTimeSeries<int>;
  IntMHTS mhts;

  EXPECT_EQ(mhts.numLevels(), IntMHTS::NUM_LEVELS);
  EXPECT_EQ(mhts.numLevels(), 3);
  mhts.flush();

  LOG(INFO) << "init: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 0);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 0);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 0);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 0);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 0);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 0);

  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 0);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 0);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 0);

  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 0);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 0);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 0);

  IntMHTS::TimePoint cur_time(IntMHTS::Duration(0));

  mhts.addValue(cur_time++, 10);
  mhts.flush();
  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 1);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 1);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 1);

  for (int i = 0; i < 299; ++i) {
    mhts.addValue(cur_time++, 10);
  }
  mhts.flush();

  LOG(INFO) << "after 300 at 10: " << mhts;

  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 60);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 300);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 300);

  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 600);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 300 * 10);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 300 * 10);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 10);

  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 10);

  for (int i = 0; i < 3600 * 3 - 300; ++i) {
    mhts.addValue(cur_time++, 10);
  }
  mhts.flush();

  LOG(INFO) << "after 3600*3 at 10: " << mhts;

  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 60);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 3600);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 3600 * 3);

  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 600);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 3600 * 10);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 3600 * 3 * 10);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 10);

  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 10);

  for (int i = 0; i < 3600; ++i) {
    mhts.addValue(cur_time++, 100);
  }
  mhts.flush();

  LOG(INFO) << "after 3600 at 100: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 100);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 3600 * 100);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 3600 * 3 * 10 + 3600 * 100);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 100);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 100);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 32);

  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 100);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 100);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 32);

  for (int i = 0; i < 1800; ++i) {
    mhts.addValue(cur_time++, 120);
  }
  mhts.flush();

  LOG(INFO) << "after 1800 at 120: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 120);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 1800 * 100 + 1800 * 120);
  EXPECT_EQ(
      mhts.sum(IntMHTS::ALLTIME), 3600 * 3 * 10 + 3600 * 100 + 1800 * 120);

  for (int i = 0; i < 60; ++i) {
    mhts.addValue(cur_time++, 1000);
  }
  mhts.flush();

  LOG(INFO) << "after 60 at 1000: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 1000);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 1740 * 100 + 1800 * 120 + 60 * 1000);
  EXPECT_EQ(
      mhts.sum(IntMHTS::ALLTIME),
      3600 * 3 * 10 + 3600 * 100 + 1800 * 120 + 60 * 1000);

  // Test non-integral rates
  mhts.addValue(cur_time++, 23);
  mhts.flush();
  EXPECT_NEAR(mhts.rate<double>(IntMHTS::MINUTE), (double)59023 / 60, 0.001);

  mhts.clear();
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 0);
}

TEST(MinuteHourTimeSeries, QueryByInterval) {
  using IntMHTS = MinuteHourTimeSeries<int>;
  IntMHTS mhts;

  IntMHTS::TimePoint curTime;
  for (curTime = IntMHTS::TimePoint(IntMHTS::Duration(0));
       curTime < IntMHTS::TimePoint(IntMHTS::Duration(7200));
       curTime++) {
    mhts.addValue(curTime, 1);
  }
  for (curTime = IntMHTS::TimePoint(IntMHTS::Duration(7200));
       curTime < IntMHTS::TimePoint(IntMHTS::Duration(7200 + 3540));
       curTime++) {
    mhts.addValue(curTime, 10);
  }
  for (curTime = IntMHTS::TimePoint(IntMHTS::Duration(7200 + 3540));
       curTime < IntMHTS::TimePoint(IntMHTS::Duration(7200 + 3600));
       curTime++) {
    mhts.addValue(curTime, 100);
  }
  mhts.flush();

  struct TimeInterval {
    IntMHTS::TimePoint start;
    IntMHTS::TimePoint end;
  };
  TimeInterval intervals[12] = {
      {curTime - IntMHTS::Duration(60), curTime},
      {curTime - IntMHTS::Duration(3600), curTime},
      {curTime - IntMHTS::Duration(7200), curTime},
      {curTime - IntMHTS::Duration(3600), curTime - IntMHTS::Duration(60)},
      {curTime - IntMHTS::Duration(7200), curTime - IntMHTS::Duration(60)},
      {curTime - IntMHTS::Duration(7200), curTime - IntMHTS::Duration(3600)},
      {curTime - IntMHTS::Duration(50), curTime - IntMHTS::Duration(20)},
      {curTime - IntMHTS::Duration(3020), curTime - IntMHTS::Duration(20)},
      {curTime - IntMHTS::Duration(7200), curTime - IntMHTS::Duration(20)},
      {curTime - IntMHTS::Duration(3000), curTime - IntMHTS::Duration(1000)},
      {curTime - IntMHTS::Duration(7200), curTime - IntMHTS::Duration(1000)},
      {curTime - IntMHTS::Duration(7200), curTime - IntMHTS::Duration(3600)},
  };

  int expectedSums[12] = {
      6000,
      41400,
      32400,
      35400,
      32130,
      16200,
      3000,
      33600,
      32310,
      20000,
      27900,
      16200,
  };

  int expectedCounts[12] = {
      60,
      3600,
      7200,
      3540,
      7140,
      3600,
      30,
      3000,
      7180,
      2000,
      6200,
      3600,
  };

  for (int i = 0; i < 12; i++) {
    TimeInterval interval = intervals[i];

    int s = mhts.sum(interval.start, interval.end);
    EXPECT_EQ(expectedSums[i], s);

    int c = mhts.count(interval.start, interval.end);
    EXPECT_EQ(expectedCounts[i], c);

    int a = mhts.avg<int>(interval.start, interval.end);
    EXPECT_EQ(expectedCounts[i] ? (expectedSums[i] / expectedCounts[i]) : 0, a);

    int r = mhts.rate<int>(interval.start, interval.end);
    int expectedRate =
        expectedSums[i] / (interval.end - interval.start).count();
    EXPECT_EQ(expectedRate, r);
  }
}
