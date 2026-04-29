#pragma once

#include <concepts>

#include "bufpool.hh"
#include "stats.hh"
#include "traces.hh"
#include "loader.hh"

template <typename Runner>
concept IsTraceRunner = requires(Runner runner, const TraceEntry& entry) {
	{ runner.access(entry) } -> std::same_as<AccessResult>;
};

template <IsEvictionPolicy Policy, std::size_t PageSize = 4096>
class BufPoolRunner
{
private:
	BufPool<Policy, PageSize> bufpool_;

public:
	explicit BufPoolRunner(std::size_t capacity) : bufpool_{capacity} {}
	// NOTE: this requires policy to be intialized with the same capacity
	explicit BufPoolRunner(std::size_t capacity, Policy&& policy) : bufpool_{capacity, policy} {}

	AccessResult access(const TraceEntry& entry)
	{
		const bool hit = bufpool_.contains(entry.page_id);
		const bool eviction = !hit && bufpool_.resident_pages() == bufpool_.capacity();
		auto page = bufpool_.get_page(DataRequest{static_cast<uint64_t>(entry.page_id)});
		static_cast<void>(page);
		return AccessResult{.hit = hit, .eviction = eviction};
	}
};

template <IsTraceRunner Runner, IsTraceLoader Loader>
RunStatistics run_loader(Runner& runner, Loader& loader)
{
	RunStatistics stats{};
	while (!loader.done()) {
		stats.record(runner.access(loader.get()));
	}
	return stats;
}
template <IsTraceRunner Runner>
RunStatistics run_trace(Runner& runner, TraceView traces)
{
	RunStatistics stats{};
	for (const auto& t : traces) {
		stats.record(runner.access(t));
	}
	return stats;
}
