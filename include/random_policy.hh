#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "eviction_policy.hh"
#include "list.hh"

using std::size_t;
using std::vector;

struct RandomMetadata : ListNode<RandomMetadata>
{
};

class RandomPolicy
{
public:
	using Metadata = RandomMetadata;

private:
	List<Metadata> free_list_;
	vector<Metadata*> residents_;
	std::mt19937_64 rng_;

public:
	explicit RandomPolicy(size_t capacity)
	    : free_list_{}, residents_{}, rng_{0}
	{
		residents_.reserve(capacity);
	}

	void register_metadata(Metadata& meta) { free_list_.push_front(meta); }

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	Metadata& get(uint64_t, CallbackFn&& on_evict)
	{
		if (free_list_.empty()) {
			assert(!residents_.empty());
			std::uniform_int_distribution<size_t> dist{0, residents_.size() - 1};
			const size_t victim_idx = dist(rng_);
			Metadata& victim = *residents_[victim_idx];
			on_evict(victim);
			residents_[victim_idx] = residents_.back();
			residents_.pop_back();
			free_list_.push_front(victim);
		}

		auto& res = free_list_.pop_back();
		residents_.push_back(&res);
		return res;
	}

	void touch(Metadata&) {}
	void release(Metadata&) {}
};

static_assert(IsEvictionPolicy<RandomPolicy>);
