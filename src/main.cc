#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>

#include "2q_policy.hh"
#include "benchmark_config.hh"
#include "fifo_policy.hh"
#include "lru_policy.hh"
#include "lru2_policy.hh"
#include "random_policy.hh"
#include "arc_policy.hh"
#include "loader.hh"
#include "runner.hh"
#include "traces.hh"

namespace
{

using std::size_t;
namespace fs = std::filesystem;

template <IsEvictionPolicy Policy, IsTraceLoader Loader>
RunStatistics run_policy(Loader& loader, size_t capacity)
{
	BufPoolRunner<Policy> runner{capacity};
	return run_loader(runner, loader);
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
}

template <size_t K>
    requires(K >= 1)
void run_lruk_trace(TraceView trace, size_t capacity)
{
	print_stats_row(std::format("LRU{} n/rec", K),
	                run_policy<LRUKNoRecencyPolicy<K>>(trace, capacity));
	print_stats_row(std::format("LRU{} w/rec", K),
	                run_policy<LRUKWithRecencyPolicy<K>>(trace, capacity));
}

void run_suite(std::string_view label, TraceView trace, size_t capacity)
{
	std::cout << '\n'
	          << label << "  capacity=" << capacity << "  accesses=" << trace.size()
	          << '\n';
	std::cout << "policy         accesses    hits  misses  evictions  hit_rate\n";

	print_stats_row("FIFO", run_policy<FifoPolicy>(trace, capacity));
	print_stats_row("Random", run_policy<RandomPolicy>(trace, capacity));
	print_stats_row("LRU", run_policy<LRUPolicy>(trace, capacity));

	run_lruk_trace<2>(trace, capacity);
	run_lruk_trace<3>(trace, capacity);
	run_lruk_trace<4>(trace, capacity);

	print_stats_row("2Q n/rec", run_policy<S2QNoRecencyPolicy>(trace, capacity));
	print_stats_row("2Q w/rec", run_policy<S2QWithRecencyPolicy>(trace, capacity));
}

void run_suite(std::string_view label, const fs::path& trace_path, size_t capacity)
{
	std::cout << '\n'
	          << label << "  capacity=" << capacity << "  trace=" << trace_path.string()
	          << '\n';
	std::cout << "policy         accesses    hits  misses  evictions  hit_rate\n";

	// {
	// 	FileLoader fifo_loader{trace_path};
	// 	print_stats_row("FIFO", run_policy<FifoPolicy>(fifo_loader, capacity));
	// }

	// {
	// 	FileLoader random_loader{trace_path};
	// 	print_stats_row("Random", run_policy<RandomPolicy>(random_loader, capacity));
	// }
	//
	// {
	// 	FileLoader lru_loader{trace_path};
	// 	print_stats_row("LRU", run_policy<LRUPolicy>(lru_loader, capacity));
	// }
	//
	run_lruk_file<2>(trace_path, capacity);
	// run_lruk_file<3>(trace_path, capacity);
	// run_lruk_file<4>(trace_path, capacity);

	{
		FileLoader s2qn_loader{trace_path};
		print_stats_row("2Q n/rec",
		                run_policy<S2QNoRecencyPolicy>(s2qn_loader, capacity));
	}
	// {
	// 	FileLoader s2qw_loader{trace_path};
	// 	print_stats_row("2Q w/rec",
	// 	                run_policy<S2QWithRecencyPolicy>(s2qw_loader, capacity));
	// }
	// {
	// 	FileLoader arc_loader{trace_path};
	// 	print_stats_row("ARC", run_policy<ARCPolicy>(arc_loader, capacity));
	// }
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
