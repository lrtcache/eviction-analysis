#pragma once

#include <cstdlib>

#include "eviction_policy.hh"
#include "list.hh"

struct FifoMetadata : ListNode<FifoMetadata>
{
};

class FifoPolicy
{
public:
	using Metadata = FifoMetadata;

private:
	List<Metadata> free_list_;
	List<Metadata> used_list_;

public:
	FifoPolicy(size_t) : free_list_{}, used_list_{} {}

	void register_metadata(Metadata& meta) { free_list_.push_front(meta); }

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	Metadata& get(uint64_t, CallbackFn&& on_evict)
	{
		if (free_list_.empty()) {
			auto& freed = used_list_.pop_back();
			on_evict(freed);
			used_list_.push_front(freed);
			return freed;
		}

		auto& res = free_list_.pop_back();
		used_list_.push_front(res);
		return res;
	}

	void touch(Metadata&) {}
	void release(Metadata&) {}
};

static_assert(IsEvictionPolicy<FifoPolicy>);
