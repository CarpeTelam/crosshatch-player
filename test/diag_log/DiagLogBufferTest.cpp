#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "DiagLogBuffer.h"

using DiagLog::LineBuffer;

namespace {

std::string drainAll(auto& buffer) {
  std::string out;
  char chunk[512];
  for (;;) {
    const size_t n = buffer.drain(chunk, sizeof(chunk));
    if (n == 0) break;
    out.append(chunk, n);
  }
  return out;
}

bool put(auto& buffer, const std::string& line) { return buffer.append(line.data(), line.size()); }

}  // namespace

TEST(DiagLogBuffer, LinesComeOutWholeAndInTheOrderTheyWentIn) {
  LineBuffer<1024> buffer;
  EXPECT_TRUE(put(buffer, "[1] [INF] [A] one\n"));
  EXPECT_TRUE(put(buffer, "[2] [INF] [B] two\n"));
  EXPECT_TRUE(put(buffer, "[3] [ERR] [C] three\n"));
  EXPECT_EQ(buffer.acceptedTotal(), 3u);
  EXPECT_EQ(drainAll(buffer), "[1] [INF] [A] one\n[2] [INF] [B] two\n[3] [ERR] [C] three\n");
  EXPECT_EQ(buffer.used(), 0u);
  EXPECT_EQ(buffer.drainedTotal(), 3u);
}

TEST(DiagLogBuffer, ALineWithoutANewlineGetsOneSoTheNextLineStartsFresh) {
  // logPrintf cuts a long message at its 256-byte buffer, which drops the "\n" the macro appended.
  LineBuffer<1024> buffer;
  EXPECT_TRUE(put(buffer, "[1] cut here"));
  EXPECT_TRUE(put(buffer, "[2] next\n"));
  EXPECT_EQ(drainAll(buffer), "[1] cut here\n[2] next\n");
}

TEST(DiagLogBuffer, ALineLongerThanTheLongestIsCutToItAndEndsInANewline) {
  LineBuffer<1024> buffer;
  const std::string longLine(400, 'x');
  EXPECT_TRUE(put(buffer, longLine));
  const std::string out = drainAll(buffer);
  EXPECT_EQ(out.size(), LineBuffer<1024>::MAX_LINE);
  EXPECT_EQ(out.back(), '\n');
  EXPECT_EQ(out.substr(0, 254), std::string(254, 'x'));
}

TEST(DiagLogBuffer, AnEmptyLineIsNothingToStore) {
  LineBuffer<1024> buffer;
  EXPECT_TRUE(buffer.append("", 0));
  EXPECT_EQ(buffer.used(), 0u);
  EXPECT_EQ(buffer.acceptedTotal(), 0u);
  EXPECT_EQ(buffer.droppedTotal(), 0u);
}

TEST(DiagLogBuffer, AFullBufferDropsWholeLinesCountsThemAndKeepsWhatItHas) {
  LineBuffer<300> buffer;
  const std::string line(99, 'a');  // 100 bytes with the newline, 101 in the buffer
  const std::string first = line + "\n";
  EXPECT_TRUE(put(buffer, first));
  EXPECT_TRUE(put(buffer, first));
  EXPECT_FALSE(put(buffer, first));  // 202 used of 300: 98 left, the third needs 101
  EXPECT_FALSE(put(buffer, first));
  EXPECT_EQ(buffer.droppedTotal(), 2u);
  EXPECT_EQ(buffer.takeDropped(), 2u);
  EXPECT_EQ(buffer.takeDropped(), 0u);   // taking resets the count the writer reports
  EXPECT_EQ(buffer.droppedTotal(), 2u);  // the running total does not
  EXPECT_EQ(drainAll(buffer), first + first);
  EXPECT_TRUE(put(buffer, first));  // room again after a drain
}

TEST(DiagLogBuffer, ADrainStopsBeforeALineThatWouldNotFitAndLeavesItForTheNextCall) {
  LineBuffer<1024> buffer;
  put(buffer, std::string(59, 'a') + "\n");
  put(buffer, std::string(59, 'b') + "\n");
  put(buffer, std::string(59, 'c') + "\n");
  char out[130];
  const size_t first = buffer.drain(out, sizeof(out));  // two 60-byte lines fit in 130
  EXPECT_EQ(first, 120u);
  EXPECT_EQ(buffer.used(), 61u);
  const size_t second = buffer.drain(out, sizeof(out));
  EXPECT_EQ(second, 60u);
  EXPECT_EQ(std::string(out, 3), "ccc");
  EXPECT_EQ(buffer.drain(out, sizeof(out)), 0u);
}

TEST(DiagLogBuffer, ALongestLineEndingInANewlineIsStoredWholeAndUnchanged) {
  // 255 bytes with the newline last: the record is 254 bytes of text plus the newline.
  LineBuffer<1024> buffer;
  std::string line(254, 'a');
  line[0] = '[';
  line += '\n';
  ASSERT_EQ(line.size(), LineBuffer<1024>::MAX_LINE);
  EXPECT_TRUE(put(buffer, line));
  EXPECT_EQ(buffer.used(), line.size() + 1);  // the length byte and the record
  EXPECT_EQ(drainAll(buffer), line);
}

TEST(DiagLogBuffer, ALongestLineThatStraddlesTheEndOfTheRingComesOutIntact) {
  LineBuffer<512> buffer;
  const std::string filler(100, 'f');
  std::string longest(254, 'z');
  longest += '\n';
  for (int round = 0; round < 6; round++) {
    // Each round moves head and tail on by 101 + 256 bytes, so the longest line lands across the ring end in some
    // round.
    ASSERT_TRUE(put(buffer, filler + "\n"));
    ASSERT_TRUE(put(buffer, longest));
    EXPECT_EQ(drainAll(buffer), filler + "\n" + longest) << round;
  }
}

TEST(DiagLogBuffer, TheRingWrapsWithoutTearingALine) {
  LineBuffer<512> buffer;
  std::string expected;
  std::string got;
  for (int i = 0; i < 400; i++) {
    const std::string line = "[" + std::to_string(i) + "] " + std::string(static_cast<size_t>(i % 90), 'w') + "\n";
    ASSERT_TRUE(put(buffer, line)) << i;
    expected += line;
    if (i % 3 == 2) got += drainAll(buffer);  // drains at varying fill levels, so the head and tail cross the end
  }
  got += drainAll(buffer);
  EXPECT_EQ(got, expected);
  EXPECT_EQ(buffer.droppedTotal(), 0u);
}

TEST(DiagLogBuffer, SeveralThreadsAppendWhileOneDrainsEachWritersOrderHoldsAndEveryLineIsAccountedFor) {
  constexpr int THREADS = 8;
  constexpr int LINES = 3000;
  LineBuffer<2048> buffer;  // small on purpose: appends overtake the drain, so drops happen
  std::atomic<int> producersDone{0};
  std::atomic<uint32_t> accepted[THREADS] = {};
  std::string drained;
  std::thread consumer([&] {
    char chunk[512];
    while (producersDone.load() < THREADS || buffer.used() > 0) {
      const size_t n = buffer.drain(chunk, sizeof(chunk));
      drained.append(chunk, n);
      if (n == 0) std::this_thread::yield();
    }
  });
  std::vector<std::thread> producers;
  for (int t = 0; t < THREADS; t++) {
    producers.emplace_back([&, t] {
      for (int i = 0; i < LINES; i++) {
        const std::string line = "T" + std::to_string(t) + " " + std::to_string(i) + "\n";
        if (put(buffer, line)) accepted[t]++;
      }
      producersDone++;
    });
  }
  for (auto& producer : producers) producer.join();
  consumer.join();

  uint32_t acceptedTotal = 0;
  for (auto& count : accepted) acceptedTotal += count.load();
  EXPECT_EQ(buffer.acceptedTotal(), acceptedTotal);
  EXPECT_EQ(buffer.acceptedTotal() + buffer.droppedTotal(), static_cast<uint32_t>(THREADS * LINES));
  EXPECT_GT(buffer.droppedTotal(), 0u) << "the test must overrun the buffer to cover the drop path";

  std::map<int, int> last;  // thread -> the last number seen
  std::map<int, uint32_t> seen;
  size_t pos = 0;
  while (pos < drained.size()) {
    const size_t end = drained.find('\n', pos);
    ASSERT_NE(end, std::string::npos) << "a torn line at the end";
    const std::string line = drained.substr(pos, end - pos);
    pos = end + 1;
    ASSERT_EQ(line[0], 'T') << line;
    const size_t space = line.find(' ');
    ASSERT_NE(space, std::string::npos) << line;
    const int t = std::stoi(line.substr(1, space - 1));
    const int i = std::stoi(line.substr(space + 1));
    if (last.count(t)) {
      EXPECT_GT(i, last[t]) << "thread " << t << " went backwards";
    }
    last[t] = i;
    seen[t]++;
  }
  for (int t = 0; t < THREADS; t++) EXPECT_EQ(seen[t], accepted[t].load()) << "thread " << t;
}

TEST(DiagLogBuffer, AppendNeverWaitsForTheDrainSoANeverDrainedBufferStillTakesEveryCall) {
  // No consumer at all: every call must return (a full buffer answers false at once, it does not wait for room).
  constexpr int THREADS = 4;
  constexpr int LINES = 5000;
  LineBuffer<1024> buffer;
  std::promise<void> finished;
  std::atomic<int> done{0};
  std::vector<std::thread> producers;
  for (int t = 0; t < THREADS; t++) {
    producers.emplace_back([&] {
      for (int i = 0; i < LINES; i++) put(buffer, "[1] [INF] [X] a line that will not be drained\n");
      if (++done == THREADS) finished.set_value();
    });
  }
  const auto status = finished.get_future().wait_for(std::chrono::seconds(60));
  for (auto& producer : producers) producer.join();
  ASSERT_EQ(status, std::future_status::ready);
  EXPECT_EQ(buffer.acceptedTotal() + buffer.droppedTotal(), static_cast<uint32_t>(THREADS * LINES));
  EXPECT_LE(buffer.used(), 1024u);
  EXPECT_GT(buffer.droppedTotal(), 0u);
}
