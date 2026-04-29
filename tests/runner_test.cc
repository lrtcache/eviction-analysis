#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "2q_policy.hh"
#include "fifo_policy.hh"
#include "lru_policy.hh"
#include "random_policy.hh"
#include "runner.hh"

namespace {

struct FakeRunner
{
	std::vector<AccessResult> scripted_results {};
	std::vector<TraceEntry> seen_entries {};
	std::size_t cursor {};

	AccessResult access(const TraceEntry& entry)
	{
		seen_entries.push_back(entry);
		return scripted_results.at(cursor++);
	}
};

template <IsEvictionPolicy Policy>
class PolicyRunnerTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(PolicyRunnerTest);

}  // namespace

TEST(RunStatisticsTest, RecordsOutcomesAndComputesHitRate)
{
	RunStatistics stats {};

	stats.record({.hit = false, .eviction = false});
	stats.record({.hit = true, .eviction = false});
	stats.record({.hit = false, .eviction = true});

	EXPECT_EQ(stats.accesses, 3U);
	EXPECT_EQ(stats.hits, 1U);
	EXPECT_EQ(stats.misses, 2U);
	EXPECT_EQ(stats.evictions, 1U);
	EXPECT_DOUBLE_EQ(stats.hit_rate(), 1.0 / 3.0);
}

TEST(RunnerTest, ReplaysTraceInOrderAndAggregatesStats)
{
	Trace trace {
	    {1, AccessMode::Read},
	    {2, AccessMode::Write},
	    {1, AccessMode::Read},
	};

	FakeRunner runner {
	    .scripted_results =
	        {
	            {.hit = false, .eviction = false},
	            {.hit = false, .eviction = true},
	            {.hit = true, .eviction = false},
	        },
	};

	const RunStatistics stats = run_trace(runner, trace);

	ASSERT_EQ(runner.seen_entries.size(), 3U);
	EXPECT_EQ(runner.seen_entries[0].page_id, 1);
	EXPECT_FALSE(runner.seen_entries[0].is_write());
	EXPECT_EQ(runner.seen_entries[1].page_id, 2);
	EXPECT_TRUE(runner.seen_entries[1].is_write());

	EXPECT_EQ(stats.accesses, 3U);
	EXPECT_EQ(stats.hits, 1U);
	EXPECT_EQ(stats.misses, 2U);
	EXPECT_EQ(stats.evictions, 1U);
}

TEST(RunnerTest, SpanLoaderAdvancesThroughTrace)
{
	Trace trace {
	    {1, AccessMode::Read},
	    {2, AccessMode::Write},
	    {3, AccessMode::Read},
	};

	FakeRunner runner {
	    .scripted_results =
	        {
	            {.hit = false, .eviction = false},
	            {.hit = false, .eviction = false},
	            {.hit = true, .eviction = false},
	        },
	};

	SpanLoader loader {trace};
	const RunStatistics stats = run_loader(runner, loader);

	ASSERT_EQ(runner.seen_entries.size(), 3U);
	EXPECT_EQ(runner.seen_entries[0], trace[0]);
	EXPECT_EQ(runner.seen_entries[1], trace[1]);
	EXPECT_EQ(runner.seen_entries[2], trace[2]);
	EXPECT_TRUE(loader.done());
	EXPECT_EQ(stats.accesses, trace.size());
}

TEST(RunnerTest, FileLoaderReadsTraceInOrder)
{
	const Trace trace {
	    {11, AccessMode::Read},
	    {22, AccessMode::Write},
	    {33, AccessMode::Read},
	};
	const std::string path = "/tmp/bufpool_trace_loader_test.bin";

	{
		std::ofstream out {path, std::ios::binary | std::ios::trunc};
		ASSERT_TRUE(out.is_open());
		out.write(reinterpret_cast<const char*>(trace.data()),
		          static_cast<std::streamsize>(trace.size() * sizeof(TraceEntry)));
		ASSERT_TRUE(out.good());
	}

	FakeRunner runner {
	    .scripted_results =
	        {
	            {.hit = false, .eviction = false},
	            {.hit = true, .eviction = false},
	            {.hit = false, .eviction = true},
	        },
	};

	FileLoader loader {path};
	const RunStatistics stats = run_loader(runner, loader);

	ASSERT_EQ(runner.seen_entries, trace);
	EXPECT_TRUE(loader.done());
	EXPECT_EQ(stats.accesses, trace.size());
	EXPECT_EQ(stats.hits, 1U);
	EXPECT_EQ(stats.evictions, 1U);
}

TEST(TraceFactoryTest, BuildsDeterministicSyntheticTraces)
{
	const Trace random_trace = make_random_read_trace(6, 4, 7);
	const Trace repeated_random_trace = make_random_read_trace(6, 4, 7);
	const Trace zipf_trace = make_zipf_read_trace(64, 8, 1.1, 13);
	const Trace repeated_zipf_trace = make_zipf_read_trace(64, 8, 1.1, 13);
	const Trace range_trace = make_range_read_trace(10, 4, 2);
	const Trace sampled_range_trace = make_sampled_range_read_trace(10, 4, 1.2, 2);
	const Trace repeated_sampled_range_trace =
	    make_sampled_range_read_trace(10, 4, 1.2, 2);
	const Trace mixed_trace = make_mixed_read_trace(3, 5, 20, 4, 2, 11);
	const Trace hotset_scan_trace = make_hotset_scan_trace(0, 3, 2, 100, 5, 2);
	const Trace hot_cold_burst_trace = make_hot_cold_burst_trace(0, 2, 3, 100, 4, 2);

	EXPECT_EQ(random_trace, repeated_random_trace);
	EXPECT_EQ(zipf_trace, repeated_zipf_trace);
	EXPECT_EQ(sampled_range_trace, repeated_sampled_range_trace);
	ASSERT_EQ(zipf_trace.size(), 64U);
	ASSERT_EQ(range_trace.size(), 8U);
	EXPECT_EQ(range_trace.front().page_id, 10);
	EXPECT_EQ(range_trace.back().page_id, 13);
	EXPECT_EQ(mixed_trace.size(), 11U);
	EXPECT_EQ(mixed_trace[3].page_id, 20);
	EXPECT_EQ(mixed_trace.back().page_id, 23);
	EXPECT_EQ(hotset_scan_trace.size(), 17U);
	EXPECT_EQ(hotset_scan_trace[6].page_id, 100);
	EXPECT_EQ(hotset_scan_trace.back().page_id, 2);
	EXPECT_EQ(hot_cold_burst_trace.size(), 20U);
	EXPECT_EQ(hot_cold_burst_trace[0].page_id, 0);
	EXPECT_EQ(hot_cold_burst_trace[6].page_id, 100);

	for (const TraceEntry& entry : zipf_trace) {
		EXPECT_GE(entry.page_id, 0);
		EXPECT_LT(entry.page_id, 8);
		EXPECT_FALSE(entry.is_write());
	}

	for (const TraceEntry& entry : sampled_range_trace) {
		EXPECT_GE(entry.page_id, 0);
		EXPECT_LT(entry.page_id, 10);
		EXPECT_FALSE(entry.is_write());
	}
}

TEST(TraceFactoryTest, ZipfTraceBiasesTowardLowRankPages)
{
	const Trace zipf_trace = make_zipf_read_trace(5000, 10, 1.2, 29);
	std::array<size_t, 10> counts{};

	for (const TraceEntry& entry : zipf_trace) {
		counts[static_cast<size_t>(entry.page_id)]++;
	}

	EXPECT_GT(counts[0], counts[1]);
	EXPECT_GT(counts[1], counts[4]);
	EXPECT_GT(counts[4], counts[9]);
}

TEST(TraceFactoryTest, SampledRangeTraceIsDeterministicAndBounded)
{
	const Trace sampled_range_trace = make_sampled_range_read_trace(32, 20, 1.2, 31);
	const Trace repeated_trace = make_sampled_range_read_trace(32, 20, 1.2, 31);

	EXPECT_EQ(sampled_range_trace, repeated_trace);
	EXPECT_FALSE(sampled_range_trace.empty());

	for (const TraceEntry& entry : sampled_range_trace) {
		EXPECT_GE(entry.page_id, 0);
		EXPECT_LT(entry.page_id, 32);
	}
}

TYPED_TEST_P(PolicyRunnerTest, CollectsBasicTraceStatistics)
{
	Trace trace {
	    {1, AccessMode::Read},
	    {2, AccessMode::Read},
	    {3, AccessMode::Read},
	    {1, AccessMode::Read},
	    {4, AccessMode::Read},
	};

	BufPoolRunner<TypeParam> runner {3};
	const RunStatistics stats = run_trace(runner, trace);

	EXPECT_EQ(stats.accesses, 5U);
	EXPECT_EQ(stats.hits, 1U);
	EXPECT_EQ(stats.misses, 4U);
	EXPECT_EQ(stats.evictions, 1U);
	EXPECT_DOUBLE_EQ(stats.hit_rate(), 0.2);
}

TYPED_TEST_P(PolicyRunnerTest, RunsSyntheticReadPatterns)
{
	const Trace random_trace = make_random_read_trace(32, 6, 17);
	const Trace range_trace = make_range_read_trace(0, 8, 3);
	const Trace mixed_trace = make_mixed_read_trace(16, 6, 0, 4, 4, 99);

	BufPoolRunner<TypeParam> random_runner {4};
	BufPoolRunner<TypeParam> range_runner {4};
	BufPoolRunner<TypeParam> mixed_runner {4};

	const RunStatistics random_stats = run_trace(random_runner, random_trace);
	const RunStatistics range_stats = run_trace(range_runner, range_trace);
	const RunStatistics mixed_stats = run_trace(mixed_runner, mixed_trace);

	EXPECT_EQ(random_stats.accesses, random_trace.size());
	EXPECT_EQ(range_stats.accesses, range_trace.size());
	EXPECT_EQ(mixed_stats.accesses, mixed_trace.size());

	EXPECT_GT(random_stats.hits, 0U);
	EXPECT_GT(mixed_stats.hits, 0U);
	EXPECT_GT(range_stats.misses, 0U);
	EXPECT_GT(mixed_stats.evictions, 0U);
	EXPECT_EQ(range_stats.hits, 0U);
}

REGISTER_TYPED_TEST_SUITE_P(PolicyRunnerTest, CollectsBasicTraceStatistics,
                            RunsSyntheticReadPatterns);

using PolicyTypes = ::testing::Types<FifoPolicy, LRUPolicy, S2QWithRecencyPolicy>;
INSTANTIATE_TYPED_TEST_SUITE_P(AllPolicies, PolicyRunnerTest, PolicyTypes);

TEST(RandomPolicyRunnerTest, RunsSyntheticReadPatterns)
{
	const Trace random_trace = make_random_read_trace(32, 6, 17);
	const Trace range_trace = make_range_read_trace(0, 8, 3);
	const Trace mixed_trace = make_mixed_read_trace(16, 6, 0, 4, 4, 99);

	BufPoolRunner<RandomPolicy> random_runner{4};
	BufPoolRunner<RandomPolicy> range_runner{4};
	BufPoolRunner<RandomPolicy> mixed_runner{4};

	const RunStatistics random_stats = run_trace(random_runner, random_trace);
	const RunStatistics range_stats = run_trace(range_runner, range_trace);
	const RunStatistics mixed_stats = run_trace(mixed_runner, mixed_trace);

	EXPECT_EQ(random_stats.accesses, random_trace.size());
	EXPECT_EQ(range_stats.accesses, range_trace.size());
	EXPECT_EQ(mixed_stats.accesses, mixed_trace.size());

	EXPECT_GT(random_stats.misses, 0U);
	EXPECT_GT(range_stats.misses, 0U);
	EXPECT_GT(mixed_stats.hits, 0U);
	EXPECT_GT(mixed_stats.evictions, 0U);
}
