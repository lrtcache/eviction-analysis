#pragma once

#include "heap.hh"
#include "list.hh"
#include "eviction_policy.hh"
#include "recency.hh"
#include "recency_pool.hh"
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>

using std::array;
using std::size_t;
using std::unique_ptr;
using std::unordered_set;

struct HeapMeta
{
	size_t heap_id;
};

template <size_t K>
    requires(K >= 1)
struct LRUKMetadata : ListNode<LRUKMetadata<K>>, HeapMeta
{
	static constexpr uint64_t SENTINEL{std::numeric_limits<uint64_t>::min()};
	using ArrayType = array<uint64_t, K>;
	uint64_t page_id;
	ArrayType last_access;
	void update_latest(uint64_t seqnum)
	{
		for (size_t i{1}; i < K; ++i) {
			last_access[i - 1] = last_access[i];
		}
		last_access.back() = seqnum;
	}
	LRUKMetadata() : last_access{} { clear_access(); }
	void clear_access() { last_access.fill(SENTINEL); }
};

template <>
struct LRUKMetadata<1> : ListNode<LRUKMetadata<1>>, HeapMeta
{
	static constexpr int64_t SENTINEL{std::numeric_limits<uint64_t>::min()};
	using ArrayType = int64_t;
	uint64_t page_id;
	ArrayType last_access;
	void update_latest(uint64_t seqnum) { last_access = seqnum; }
	LRUKMetadata() : last_access{} { clear_access(); }
	void clear_access() { last_access = SENTINEL; }
};

template <size_t K, template <typename T> class ValuedRecencyStore>
    requires(K >= 1) &&
            IsValuedRecencyStore<ValuedRecencyStore<typename LRUKMetadata<K>::ArrayType>,
                                 typename LRUKMetadata<K>::ArrayType>
class LRUKPolicy
{
public:
	using Metadata = LRUKMetadata<K>;
	using RecencyStoreType = ValuedRecencyStore<typename Metadata::ArrayType>;

private:
	struct MetaCmp
	{
		bool operator()(Metadata* lhs, Metadata* rhs)
		{
			return lhs->last_access < rhs->last_access;
		};
	};
	struct HeapCallback
	{
		void operator()(Metadata* lhs, size_t heap_id) { lhs->heap_id = heap_id; };
	};
	LRU2::UpdatableHeap<Metadata*, MetaCmp, HeapCallback> heap_;
	uint64_t seqnum_;
	uint64_t seq_grace_;
	List<Metadata> free_list_;
	RecencyStoreType recent_pool_;

public:
	LRUKPolicy(size_t capacity, size_t seq_grace = 0, double recency_pct = 0.3)
	    : heap_(capacity), seqnum_{1}, seq_grace_{seq_grace},
	      recent_pool_(static_cast<size_t>(std::ceil(capacity * recency_pct)))
	{
		assert(seqnum_ != Metadata::SENTINEL);
	}
	// TODO: this move constructor is very brittle, other's fields are not cleared properly
	LRUKPolicy(LRUKPolicy&& other)
	    : heap_{std::move(other.heap_)}, seqnum_{other.seqnum_},
	      seq_grace_{other.seq_grace_}, free_list_{std::move(other.free_list_)},
	      recent_pool_{std::move(other.recent_pool_)}
	{
	}

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	Metadata& get(uint64_t page_id, CallbackFn&& on_evict)
	{
		if (free_list_.empty()) {
			evict(on_evict);
		}
		assert(!free_list_.empty());
		auto& res = free_list_.pop_back();
		if (recent_pool_.contains(page_id)) {
			auto old_access = recent_pool_.take(page_id);
			res.last_access = old_access;
		}
		res.page_id = page_id;
		heap_.push(&res);
		touch(res);
		return res;
	}
	void register_metadata(Metadata& meta)
	{
		meta.clear_access();
		free_list_.push_front(meta);
	}
	void touch(Metadata& meta)
	{
		// within grace period
		if (meta.last_access.back() != Metadata::SENTINEL &&
		    seqnum_ - meta.last_access.back() <= seq_grace_) {
			return;
		}
		meta.update_latest(seqnum_);
		heap_.update(meta.heap_id, &meta);
		seqnum_++;
		[[unlikely]]
		if (seqnum_ == Metadata::SENTINEL) {
			fprintf(stderr, "LRU2 sequence number exceeded\n");
			abort();
		}
	}
	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	void evict_one(CallbackFn&& on_evict)
	{
		auto& victim = *heap_.pop();

		recent_pool_.insert(victim.page_id, victim.last_access);

		victim.clear_access();
		on_evict(victim);
		free_list_.push_front(victim);
	}

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	void evict(CallbackFn&& on_evict)
	{
		evict_one(on_evict);
	}
	void release(Metadata&) {}
};

template <size_t K>
using LRUKNoRecencyPolicy = LRUKPolicy<K, NullValuedRecencyStore>;

template <size_t K>
using LRUKWithRecencyPolicy = LRUKPolicy<K, RecencyPool>;

using LRU2Policy = LRUKNoRecencyPolicy<2>;

static_assert(IsEvictionPolicy<LRUKNoRecencyPolicy<1>>);
static_assert(IsEvictionPolicy<LRUKWithRecencyPolicy<1>>);
static_assert(IsEvictionPolicy<LRUKNoRecencyPolicy<2>>);
static_assert(IsEvictionPolicy<LRUKWithRecencyPolicy<2>>);
