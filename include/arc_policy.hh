#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include "list.hh"
#include "eviction_policy.hh"

using std::size_t;
using std::unique_ptr;
using std::unordered_map;

struct ARCMetadata : ListNode<ARCMetadata>
{
	[[maybe_unused]] enum class State {
		FREE = 0,
		COLD = 1,
		HOT = 2,
	} state;
	uint64_t page_id;
};

// separate these for stronger typing
struct ARCHistory : ListNode<ARCHistory>
{
	[[maybe_unused]] enum class State {
		FREE = 0,
		COLD = 1,
		HOT = 2,
	} state;
	uint64_t page_id;
};

class ARCPolicy
{
public:
	using Metadata = ARCMetadata;
	static constexpr double THRES_INIT_PCT{0.25};

private:
	List<Metadata> free_list_;
	List<Metadata> cold_list_;
	List<Metadata> hot_list_;

	List<ARCHistory> free_hist_list_;
	List<ARCHistory> cold_hist_list_;
	List<ARCHistory> hot_hist_list_;

	[[maybe_unused]] size_t capacity_;
	unique_ptr<ARCHistory[]> history_nodes_;

	size_t thres_;

	unordered_map<uint64_t, ARCHistory&> hist_map_;

	inline void push_hot(Metadata& meta)
	{
		meta.state = Metadata::State::HOT;
		hot_list_.push_front(meta);
	}

	inline void push_cold(Metadata& meta)
	{
		meta.state = Metadata::State::COLD;
		cold_list_.push_front(meta);
	}

	inline void push_hist_hot(ARCHistory& hist)
	{
		hist.state = ARCHistory::State::HOT;
		hot_hist_list_.push_front(hist);
	}

	inline void push_hist_cold(ARCHistory& hist)
	{
		hist.state = ARCHistory::State::COLD;
		cold_hist_list_.push_front(hist);
	}

	inline void release_histnode(ARCHistory& hist)
	{
		assert(hist.state != ARCHistory::State::FREE);
		hist.release();
		if (hist.state == ARCHistory::State::HOT) {
			hot_hist_list_._release_callback();
		} else {
			assert(hist.state == ARCHistory::State::COLD);
			cold_hist_list_._release_callback();
		}
	}

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	Metadata& replace(uint64_t page_id, size_t thres, CallbackFn&& on_evict)
	{
		if (!cold_list_.empty()) {
			auto check_condition = [&]() {
				if (cold_list_.size() > thres) {
					return true;
				}
				if (cold_list_.size() != thres) {
					return false;
				}
				auto it = hist_map_.find(page_id);
				if (it == end(hist_map_)) {
					return false;
				}
				return (it->second.state == ARCHistory::State::HOT);
			};
			if (check_condition()) {
				auto& freed = cold_list_.pop_back();
				on_evict(freed);

				auto& histnode = free_hist_list_.pop_back();
				histnode.page_id = freed.page_id;
				hist_map_.emplace(histnode.page_id, histnode);
				push_hist_cold(histnode);

				return freed;
			}
		}
		auto& freed = hot_list_.pop_back();
		on_evict(freed);

		auto& histnode = free_hist_list_.pop_back();
		histnode.page_id = freed.page_id;
		hist_map_.emplace(histnode.page_id, histnode);

		push_hist_hot(histnode);
		return freed;
	}

public:
	ARCPolicy(size_t capacity)
	    : free_list_{}, cold_list_{}, hot_list_{}, capacity_{capacity},
	      history_nodes_{std::make_unique<ARCHistory[]>(capacity)},
	      thres_{static_cast<size_t>(std::ceil(THRES_INIT_PCT * capacity))}
	{
		assert(capacity > 0);
		hist_map_.reserve(capacity);
		for (size_t i{0}; i < capacity; ++i) {
			free_hist_list_.push_front(history_nodes_[i]);
		}
	}

	void register_metadata(Metadata& meta)
	{
		assert(capacity_ >= free_list_.size());
		free_list_.push_front(meta);
	}

	template <typename CallbackFn>
	    requires IsEvictCallback<CallbackFn, Metadata>
	Metadata& get(uint64_t page_id, CallbackFn&& on_evict)
	{
		// check recency
		if (auto it = hist_map_.find(page_id); it != end(hist_map_)) {

			auto& histnode = it->second;

			// erase the page_id's history
			hist_map_.erase(it);

			if (histnode.state == ARCHistory::State::COLD) {
				// in B1
				auto delta = hot_hist_list_.size() >= cold_hist_list_.size()
				                 ? hot_hist_list_.size() / cold_hist_list_.size()
				                 : 1UL;
				thres_ = std::min(thres_ + delta, capacity_);

				release_histnode(histnode);
				free_hist_list_.push_front(histnode);

				auto& node = replace(page_id, thres_, on_evict);
				node.page_id = page_id;
				push_hot(node);
				return node;
			} else {
				// in B2
				assert(histnode.state == ARCHistory::State::HOT);
				auto delta = cold_hist_list_.size() >= hot_hist_list_.size()
				                 ? cold_hist_list_.size() / hot_hist_list_.size()
				                 : 1UL;
				thres_ = thres_ >= delta ? thres_ - delta : 0UL;

				release_histnode(histnode);
				histnode.state = ARCHistory::State::FREE;
				free_hist_list_.push_front(histnode);

				auto& node = replace(page_id, thres_, on_evict);
				node.page_id = page_id;
				push_hot(node);
				return node;
			}
		}

		if ((cold_list_.size() + cold_hist_list_.size()) == capacity_) {
			if (cold_list_.size() < capacity_) {
				auto& histnode = cold_hist_list_.pop_back();
				hist_map_.erase(histnode.page_id);
				free_hist_list_.push_front(histnode);
				auto& freed = replace(page_id, thres_, on_evict);
				freed.page_id = page_id;
				push_cold(freed);

				return freed;
			}
			auto& freed = cold_list_.pop_back();
			on_evict(freed);
			freed.page_id = page_id;
			push_cold(freed);

			return freed;
		}
		size_t total{0};
		total += cold_list_.size() + cold_hist_list_.size();
		total += hot_list_.size() + hot_hist_list_.size();
		if (total >= capacity_) {
			if (total == 2 * capacity_) {
				auto& histnode = hot_hist_list_.pop_back();
				hist_map_.erase(histnode.page_id);
				free_hist_list_.push_front(histnode);
			}
			auto& freed = replace(page_id, thres_, on_evict);
			freed.page_id = page_id;
			push_cold(freed);

			return freed;
		}
		auto& freed = free_list_.pop_back();
		freed.page_id = page_id;
		push_cold(freed);

		return freed;
	}

	void touch(Metadata& meta)
	{
		assert(meta.state != Metadata::State::FREE);
		meta.release();
		if (meta.state == Metadata::State::COLD) {
			// move to lru
			cold_list_._release_callback();
			meta.state = Metadata::State::HOT;
		} else {
			hot_list_._release_callback();
		}
		hot_list_.push_front(meta);
	}
	void release(Metadata&) {}
};

static_assert(IsEvictionPolicy<ARCPolicy>);
