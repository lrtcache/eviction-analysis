#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include "list.hh"
#include "eviction_policy.hh"

using std::size_t;

struct LRUMetadata : ListNode<LRUMetadata>
{
};

class LRUPolicy
{
public:
	using Metadata = LRUMetadata;

private:
	List<Metadata> free_list_;
	List<Metadata> lru_list_;

	size_t total_cap_;

public:
	LRUPolicy(std::size_t capacity) : free_list_{}, lru_list_{}, total_cap_{capacity} {}

	void register_metadata(Metadata& meta)
	{
		free_list_.push_front(meta);
		assert(free_list_.size() <= total_cap_);
	}
	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	Metadata& get(uint64_t, CallbackFn&& on_evict)
	{
		assert(total_cap_ > 0 && "capacity must be nonzero");
		if (free_list_.empty()) {
			// get more free pages, evict
			evict(on_evict);
		}
		assert(!free_list_.empty());
		auto& res = free_list_.pop_back();
		lru_list_.push_front(res);
		return res;
	}
	void release(Metadata&) {}

	void touch(Metadata& meta)
	{
		meta.release();
		lru_list_.push_front(meta);
	}

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	void evict_one(CallbackFn&& on_evict)
	{
		auto& freed = lru_list_.pop_back();
		on_evict(freed);
		free_list_.push_front(freed);
	}

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	void evict(CallbackFn&& on_evict)
	{
		evict_one(on_evict);
		// auto to_get = static_cast<size_t>(std::ceil(0.2 * total_cap_));
		// while (to_get > 0) {
		// 	evict_one(on_evict);
		// 	to_get--;
		// }
	}
};

static_assert(IsEvictionPolicy<LRUPolicy>);
