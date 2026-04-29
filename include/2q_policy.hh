#pragma once
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <unordered_set>
#include <type_traits>
#include "list.hh"
#include "eviction_policy.hh"
#include "recency.hh"
#include "recency_pool.hh"

using std::size_t;
using std::unique_ptr;
using std::unordered_set;

struct S2QMetadata : ListNode<S2QMetadata>
{
	[[maybe_unused]] enum class State {
		FREE = 0,
		COLD = 1,
		HOT = 2,
	} state;
	uint64_t page_id;
};

template <class RecencyStore = RecencyPool<>>
    requires IsRecencyStore<RecencyStore>
class S2QPolicy_
{
public:
	using Metadata = S2QMetadata;
	using RecencyStoreType = RecencyStore;
	static constexpr double COLDQ_PCT{0.25};
	static constexpr double HOTQ_PCT{1 - COLDQ_PCT};

private:
	List<Metadata> free_list_;
	List<Metadata> cold_list_;
	List<Metadata> hot_list_;

	[[maybe_unused]] size_t total_cap_;
	size_t cold_thres_;

	RecencyStoreType recent_pool_;

public:
	S2QPolicy_(size_t capacity)
	    : free_list_{}, cold_list_{}, hot_list_{}, total_cap_{capacity},
	      cold_thres_{static_cast<size_t>(std::ceil(COLDQ_PCT * capacity))},
	      recent_pool_{static_cast<size_t>(std::ceil(capacity * 0.3))}
	{
		assert(capacity > 0);
	}

	void register_metadata(Metadata& meta)
	{
		assert(total_cap_ >= free_list_.size());
		free_list_.push_front(meta);
	}

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	Metadata& get(uint64_t page_id, CallbackFn&& on_evict)
	{
		if (free_list_.empty()) {
			// get more free pages, evict
			evict(on_evict);
		}
		assert(!free_list_.empty());

		auto& res = free_list_.pop_back();
		res.page_id = page_id;
		// put to correct list
		if (recent_pool_.contains(page_id)) {
			// push to hot
			res.state = Metadata::State::HOT;
			hot_list_.push_front(res);
		} else {
			// push to cold
			res.state = Metadata::State::COLD;
			cold_list_.push_front(res);
		}

		return res;
	}
	void touch(Metadata& meta)
	{
		assert(meta.state != Metadata::State::FREE);

		if (meta.state == Metadata::State::HOT) {
			// move to lru
			meta.release();
			hot_list_._release_callback();
			hot_list_.push_front(meta);
		} else {
			assert(meta.state == Metadata::State::COLD);
			if constexpr (std::is_same_v<RecencyStoreType, NullRecencyStore>) {
				meta.release();
				cold_list_._release_callback();
				meta.state = Metadata::State::HOT;
				hot_list_.push_front(meta);
			}
		}
	}

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	void evict_one(CallbackFn&& on_evict)
	{
		auto pick_victim = [&]() -> Metadata& {
			if (cold_list_.size() > cold_thres_) {
				auto& node = cold_list_.pop_back();
				return node;
			}
			assert(!hot_list_.empty());
			auto& node = hot_list_.pop_back();
			return node;
		};
		Metadata& victim{pick_victim()};

		free_list_.push_front(victim);
		auto victim_id = victim.page_id;

		// put to recently evicted
		recent_pool_.insert(victim_id);
		victim.state = Metadata::State::FREE;
		on_evict(victim);
	}
	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	void evict(CallbackFn&& on_evict)
	{
		evict_one(on_evict);
		// auto to_get = static_cast<size_t>(std::ceil(0.05 * total_cap_));
		// while (to_get > 0) {
		// 	evict_one(on_evict);
		// 	to_get--;
		// }
	}
	void release(Metadata&) {}
};

using S2QPolicy = S2QPolicy_<>;
using S2QNoRecencyPolicy = S2QPolicy_<NullRecencyStore>;
using S2QWithRecencyPolicy = S2QPolicy_<RecencyPool<>>;

static_assert(IsEvictionPolicy<S2QNoRecencyPolicy>);
static_assert(IsEvictionPolicy<S2QWithRecencyPolicy>);
