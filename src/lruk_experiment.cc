#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "benchmark_config.hh"
#include "lru2_policy.hh"
#include "loader.hh"
#include "runner.hh"
#include "traces.hh"

namespace
{

using std::size_t;
namespace fs = std::filesystem;

template <IsEvictionPolicy Policy, IsTraceLoader Loader>
RunStatistics run_policy(Loader& loader, size_t capacity, size_t grace,
                         double recency_pct)
{
	BufPoolRunner<Policy> runner{capacity, std::in_place, capacity, grace, recency_pct};
	return run_loader(runner, loader);
}

template <IsEvictionPolicy Policy, IsTraceLoader Loader>
RunStatistics run_policy(Loader& loader, size_t capacity)
{
	BufPoolRunner<Policy> runner{capacity};
	return run_loader(runner, loader);
}

template <IsEvictionPolicy Policy>
RunStatistics run_policy(TraceView traces, size_t capacity, size_t grace,
                         double recency_pct)
{
	SpanLoader loader{traces};
	return run_policy<Policy, SpanLoader>(loader, capacity, grace, recency_pct);
}

template <IsEvictionPolicy Policy>
RunStatistics run_policy(TraceView traces, size_t capacity)
{
	SpanLoader loader{traces};
	return run_policy<Policy, SpanLoader>(loader, capacity);
}

void print_stats_row(std::string_view policy_name, const RunStatistics& stats)
{
	std::cout << std::left << std::setw(12) << policy_name << "  " << std::right
	          << std::setw(8) << stats.accesses << "  " << std::setw(6) << stats.hits
	          << "  " << std::setw(6) << stats.misses << "  " << std::setw(9)
	          << stats.evictions << "  " << std::fixed << std::setprecision(3)
	          << stats.hit_rate() << '\n';
}

template <size_t K>
    requires(K >= 1)
void run_lruk_file(const fs::path& trace_path, size_t capacity)
{
	{
		FileLoader n_loader{trace_path};
		print_stats_row(std::format("LRU{} n/rec", K),
		                run_policy<LRUKNoRecencyPolicy<K>>(n_loader, capacity));
	}
	{
		FileLoader w_loader{trace_path};
		print_stats_row(std::format("LRU{} w/rec", K),
		                run_policy<LRUKWithRecencyPolicy<K>>(w_loader, capacity));
	}
	auto run_lru_config = [&](size_t grace, double recency_pct) {
		{
			FileLoader n_loader{trace_path};
			auto stats = run_policy<LRUKNoRecencyPolicy<K>>(n_loader, capacity, grace,
			                                                recency_pct);
			print_stats_row(
			    std::format("LRU{} n/rec g={} rp={:.2f}", K, grace, recency_pct), stats);
		}
		{
			FileLoader w_loader{trace_path};
			auto stats = run_policy<LRUKWithRecencyPolicy<K>>(w_loader, capacity, grace,
			                                                  recency_pct);
			print_stats_row(
			    std::format("LRU{} w/rec g={} rp={:.2f}", K, grace, recency_pct), stats);
		}
	};
	run_lru_config(0, 0.25);
	run_lru_config(0, 0.5);
	run_lru_config(0, 0.75);
	run_lru_config(0, 1);
	run_lru_config(128, 0.3);
	run_lru_config(512, 0.3);
	run_lru_config(2048, 0.3);
}

template <size_t K>
    requires(K >= 1)
void run_lruk_trace(TraceView trace, size_t capacity)
{
	print_stats_row(std::format("LRU{} n/rec", K),
	                run_policy<LRUKNoRecencyPolicy<K>>(trace, capacity));
	print_stats_row(std::format("LRU{} w/rec", K),
	                run_policy<LRUKWithRecencyPolicy<K>>(trace, capacity));
	auto run_lru_config = [&](size_t grace, double recency_pct) {
		auto no_rec_stats =
		    run_policy<LRUKNoRecencyPolicy<K>>(trace, capacity, grace, recency_pct);
		print_stats_row(std::format("LRU{} n/rec g={} rp={:.2f}", K, grace, recency_pct),
		                no_rec_stats);

		auto with_rec_stats =
		    run_policy<LRUKWithRecencyPolicy<K>>(trace, capacity, grace, recency_pct);
		print_stats_row(std::format("LRU{} w/rec g={} rp={:.2f}", K, grace, recency_pct),
		                with_rec_stats);
	};
	run_lru_config(0, 0.25);
	run_lru_config(0, 0.5);
	run_lru_config(0, 0.75);
	run_lru_config(0, 1);
	run_lru_config(16, 0.3);
	run_lru_config(128, 0.3);
	run_lru_config(512, 0.3);
}

void run_suite(std::string_view label, TraceView trace, size_t capacity)
{
	std::cout << '\n'
	          << label << "  capacity=" << capacity << "  accesses=" << trace.size()
	          << '\n';
	std::cout << "policy         accesses    hits  misses  evictions  hit_rate\n";

	run_lruk_trace<2>(trace, capacity);
	run_lruk_trace<3>(trace, capacity);
	run_lruk_trace<4>(trace, capacity);
}

void run_suite(std::string_view label, const fs::path& trace_path, size_t capacity)
{
	std::cout << '\n'
	          << label << "  capacity=" << capacity << "  trace=" << trace_path.string()
	          << '\n';
	std::cout << "policy         accesses    hits  misses  evictions  hit_rate\n";

	run_lruk_file<2>(trace_path, capacity);
	run_lruk_file<3>(trace_path, capacity);
	run_lruk_file<4>(trace_path, capacity);
}

} // namespace

int main(int argc, char** argv)
{
	const std::string config_path = argc >= 2 ? argv[1] : "benchmarks.ini";

	try {
		const auto benchmarks = benchmark_config::load_benchmark_specs(config_path);
		for (const BenchmarkSpec& bench : benchmarks) {
			std::visit(
			    [&](const auto& trace) {
				    using TraceType = std::decay_t<decltype(trace)>;
				    if constexpr (std::is_same_v<TraceType, Trace>) {
					    run_suite(bench.name, trace, bench.capacity);
				    } else if constexpr (std::is_same_v<TraceType, FileTraceSpec>) {
					    run_suite(bench.name, trace.path, bench.capacity);
				    }
			    },
			    bench.trace);
		}
	} catch (const std::exception& ex) {
		std::cerr << "error: " << ex.what() << '\n';
		return 1;
	}

	return 0;
}
