#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "benchmark_config.hh"

namespace {

std::string write_temp_config(const std::string& name, const std::string& contents)
{
	const std::string path = "/tmp/" + name;
	std::ofstream out{path};
	out << contents;
	return path;
}

} // namespace

TEST(BenchmarkConfigTest, LoadsNamedSectionsIntoBenchmarks)
{
	const std::string path = write_temp_config(
	    "bufpool_bench_valid.ini",
	    R"ini(
[ZipfRun]
kind = zipf
capacity = 64
length = 100
page_domain = 16
alpha = 1.1
seed = 9

[RangeRun]
kind = range
capacity = 8
start_page = 10
page_count = 4
repetitions = 3

[SampledRangeRun]
kind = sampled_range
capacity = 16
page_domain = 12
segments = 5
alpha = 1.1
seed = 4
)ini");

	const auto specs = benchmark_config::load_benchmark_specs(path);

	ASSERT_EQ(specs.size(), 3U);
	EXPECT_EQ(specs[0].name, "ZipfRun");
	EXPECT_EQ(specs[0].capacity, 64U);
	ASSERT_TRUE(std::holds_alternative<Trace>(specs[0].trace));
	EXPECT_EQ(std::get<Trace>(specs[0].trace).size(), 100U);
	EXPECT_EQ(specs[1].name, "RangeRun");
	EXPECT_EQ(specs[1].capacity, 8U);
	ASSERT_TRUE(std::holds_alternative<Trace>(specs[1].trace));
	const Trace& range_trace = std::get<Trace>(specs[1].trace);
	ASSERT_EQ(range_trace.size(), 12U);
	EXPECT_EQ(range_trace.front().page_id, 10);
	EXPECT_EQ(range_trace.back().page_id, 13);
	EXPECT_EQ(specs[2].name, "SampledRangeRun");
	EXPECT_EQ(specs[2].capacity, 16U);
	ASSERT_TRUE(std::holds_alternative<Trace>(specs[2].trace));
	const Trace& sampled_trace = std::get<Trace>(specs[2].trace);
	EXPECT_FALSE(sampled_trace.empty());
	for (const TraceEntry& entry : sampled_trace) {
		EXPECT_GE(entry.page_id, 0);
		EXPECT_LT(entry.page_id, 12);
	}
}

TEST(BenchmarkConfigTest, LoadsBinaryFileTraceSpec)
{
	const std::string path = write_temp_config(
	    "bufpool_bench_binary_file.ini",
	    R"ini(
[BinaryRun]
kind = binary_file
capacity = 32
filename = /tmp/trace.bin
)ini");

	const auto specs = benchmark_config::load_benchmark_specs(path);

	ASSERT_EQ(specs.size(), 1U);
	ASSERT_TRUE(std::holds_alternative<FileTraceSpec>(specs[0].trace));
	EXPECT_EQ(std::get<FileTraceSpec>(specs[0].trace).path, "/tmp/trace.bin");
}

TEST(BenchmarkConfigTest, RejectsUnknownKinds)
{
	const std::string path = write_temp_config(
	    "bufpool_bench_unknown_kind.ini",
	    R"ini(
[BadRun]
kind = nope
capacity = 64
)ini");

	EXPECT_THROW(
	    {
		    try {
			    static_cast<void>(benchmark_config::load_benchmark_specs(path));
		    } catch (const std::runtime_error& ex) {
			    EXPECT_NE(std::string{ex.what()}.find("unknown kind"), std::string::npos);
			    throw;
		    }
	    },
	    std::runtime_error);
}

TEST(BenchmarkConfigTest, RejectsMissingRequiredKeys)
{
	const std::string path = write_temp_config(
	    "bufpool_bench_missing.ini",
	    R"ini(
[RandomRun]
kind = random
length = 128
page_domain = 16
)ini");

	EXPECT_THROW(
	    {
		    try {
			    static_cast<void>(benchmark_config::load_benchmark_specs(path));
		    } catch (const std::runtime_error& ex) {
			    EXPECT_NE(std::string{ex.what()}.find("missing key 'capacity'"),
			              std::string::npos);
			    throw;
		    }
	    },
	    std::runtime_error);
}
