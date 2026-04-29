#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

using std::size_t;
enum class AccessMode : std::uint8_t
{
	Read = 0,
	Write = 1,
};

struct TraceEntry
{
	AccessMode mode {AccessMode::Read};
	uint64_t page_id {};

	constexpr TraceEntry() = default;

	constexpr TraceEntry(uint64_t page_id_, AccessMode mode_ = AccessMode::Read)
	    : mode{mode_}, page_id{page_id_}
	{
	}

	[[nodiscard]] bool is_write() const noexcept { return mode == AccessMode::Write; }
	bool operator==(const TraceEntry&) const = default;
};

using Trace = std::vector<TraceEntry>;
using TraceView = std::span<const TraceEntry>;

inline Trace make_random_read_trace(size_t length, int page_domain, uint64_t seed = 0)
{
	if (page_domain <= 0) {
		throw std::invalid_argument("page_domain must be positive");
	}

	std::mt19937_64 rng{seed};
	std::uniform_int_distribution<int> dist{0, page_domain - 1};
	Trace trace{};
	trace.reserve(length);

	for (size_t i = 0; i < length; ++i) {
		trace.push_back(TraceEntry{static_cast<uint64_t>(dist(rng))});
	}

	return trace;
}

inline Trace make_zipf_read_trace(size_t length, int page_domain, double alpha,
                                  uint64_t seed = 0)
{
	if (page_domain <= 0) {
		throw std::invalid_argument("page_domain must be positive");
	}
	if (alpha < 0.0) {
		throw std::invalid_argument("alpha must be nonnegative");
	}

	std::mt19937_64 rng{seed};
	std::vector<double> weights{};
	weights.reserve(static_cast<size_t>(page_domain));
	for (int rank = 1; rank <= page_domain; ++rank) {
		weights.push_back(1.0 / std::pow(static_cast<double>(rank), alpha));
	}

	std::discrete_distribution<int> dist(weights.begin(), weights.end());
	Trace trace{};
	trace.reserve(length);

	for (size_t i = 0; i < length; ++i) {
		trace.push_back(TraceEntry{static_cast<uint64_t>(dist(rng))});
	}

	return trace;
}

inline Trace make_range_read_trace(int start_page, int page_count, size_t repetitions = 1)
{
	if (page_count <= 0) {
		throw std::invalid_argument("page_count must be positive");
	}

	Trace trace{};
	trace.reserve(static_cast<size_t>(page_count) * repetitions);

	for (size_t rep = 0; rep < repetitions; ++rep) {
		for (int offset = 0; offset < page_count; ++offset) {
			trace.push_back(TraceEntry{static_cast<uint64_t>(start_page + offset)});
		}
	}

	return trace;
}

inline Trace make_sampled_range_read_trace(int page_domain, size_t segments,
                                           double alpha = 1.2, uint64_t seed = 0)
{
	if (page_domain <= 0) {
		throw std::invalid_argument("page_domain must be positive");
	}
	if (alpha < 0.0) {
		throw std::invalid_argument("alpha must be nonnegative");
	}

	Trace trace{};

	std::mt19937_64 rng{seed};
	std::vector<double> weights{};
	weights.reserve(static_cast<size_t>(page_domain));
	for (int rank = 1; rank <= page_domain; ++rank) {
		weights.push_back(1.0 / std::pow(static_cast<double>(rank), alpha));
	}

	std::discrete_distribution<int> start_dst(weights.begin(), weights.end());
	std::discrete_distribution<int> len_dst(weights.begin(), weights.end());

	for (size_t segment = 0; segment < segments; ++segment) {
		const int start_page = start_dst(rng);
		const int span_len = len_dst(rng) + 1;
		for (int offset = 0; offset < span_len && start_page + offset < page_domain;
		     ++offset) {
			trace.push_back(TraceEntry{static_cast<uint64_t>(start_page + offset)});
		}
	}

	return trace;
}

inline Trace make_mixed_read_trace(size_t random_prefix_length, int random_page_domain,
                                   int range_start_page, int range_page_count,
                                   size_t range_repetitions, std::uint64_t seed = 0)
{
	Trace trace = make_random_read_trace(random_prefix_length, random_page_domain, seed);
	Trace range =
	    make_range_read_trace(range_start_page, range_page_count, range_repetitions);
	trace.insert(trace.end(), range.begin(), range.end());
	return trace;
}

inline Trace make_hotset_scan_trace(int hotset_start_page, int hotset_size,
                                    size_t hotset_rounds_before_scan, int scan_start_page,
                                    int scan_length, size_t hotset_rounds_after_scan)
{
	if (hotset_size <= 0 || scan_length <= 0) {
		throw std::invalid_argument("hotset_size and scan_length must be positive");
	}

	Trace trace{};
	trace.reserve(static_cast<size_t>(hotset_size) *
	                  (hotset_rounds_before_scan + hotset_rounds_after_scan) +
	              static_cast<size_t>(scan_length));

	for (size_t round = 0; round < hotset_rounds_before_scan; ++round) {
		for (int offset = 0; offset < hotset_size; ++offset) {
			trace.push_back(
			    TraceEntry{static_cast<uint64_t>(hotset_start_page + offset)});
		}
	}

	for (int offset = 0; offset < scan_length; ++offset) {
		trace.push_back(TraceEntry{static_cast<uint64_t>(scan_start_page + offset)});
	}

	for (size_t round = 0; round < hotset_rounds_after_scan; ++round) {
		for (int offset = 0; offset < hotset_size; ++offset) {
			trace.push_back(
			    TraceEntry{static_cast<uint64_t>(hotset_start_page + offset)});
		}
	}

	return trace;
}

inline Trace make_hot_cold_burst_trace(int hotset_start_page, int hotset_size,
                                       size_t hot_repetitions_per_burst,
                                       int coldset_start_page, int coldset_size,
                                       size_t bursts)
{
	if (hotset_size <= 0 || coldset_size <= 0) {
		throw std::invalid_argument("hotset_size and coldset_size must be positive");
	}

	Trace trace{};
	trace.reserve(bursts * (static_cast<size_t>(hotset_size) * hot_repetitions_per_burst +
	                        static_cast<size_t>(coldset_size)));

	for (size_t burst = 0; burst < bursts; ++burst) {
		for (size_t rep = 0; rep < hot_repetitions_per_burst; ++rep) {
			for (int offset = 0; offset < hotset_size; ++offset) {
				trace.push_back(
				    TraceEntry{static_cast<uint64_t>(hotset_start_page + offset)});
			}
		}

		for (int offset = 0; offset < coldset_size; ++offset) {
			trace.push_back(
			    TraceEntry{static_cast<uint64_t>(coldset_start_page + offset)});
		}
	}

	return trace;
}
