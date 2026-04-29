#pragma once

#include <cstddef>

using std::size_t;

struct AccessResult
{
	bool hit{};
	bool eviction{};
};

struct RunStatistics
{
	size_t accesses{};
	size_t hits{};
	size_t misses{};
	size_t evictions{};

	void record(AccessResult result) noexcept
	{
		++accesses;
		if (result.hit) {
			++hits;
		} else {
			++misses;
		}

		if (result.eviction) {
			++evictions;
		}
	}

	[[nodiscard]] double hit_rate() const noexcept
	{
		if (accesses == 0) {
			return 0.0;
		}
		return static_cast<double>(hits) / static_cast<double>(accesses);
	}
};
