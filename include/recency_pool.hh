#pragma once

#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <utility>
#include "list.hh"

using std::size_t;
using std::unique_ptr;
using std::unordered_map;
using std::unordered_set;

template <typename V = void>
class RecencyPool;

struct RecentNode : ListNode<RecentNode>
{
	static constexpr uint64_t SENTINEL{UINT64_MAX};
	uint64_t key{};
};

template <typename V>
class RecencyPool
{
	using value_type = V;
	// using value_type = int;
	unordered_map<uint64_t, std::pair<value_type, RecentNode*>> recently_evicted_;
	size_t capacity_;
	unique_ptr<RecentNode[]> nodes_;
	List<RecentNode> list_;

public:
	RecencyPool(size_t capacity)
	    : recently_evicted_{}, capacity_{capacity},
	      nodes_{std::make_unique<RecentNode[]>(capacity)}, list_{}
	{
		recently_evicted_.reserve(capacity_);
		for (size_t i{0}; i < capacity_; ++i) {
			nodes_[i].key = RecentNode::SENTINEL;
			list_.push_front(nodes_[i]);
		}
	}

	void insert(uint64_t key, value_type value)
	{
		if (auto it = recently_evicted_.find(key); it != recently_evicted_.end()) {
			// move to lru position
			auto& node = *it->second.second;
			node.release();
			list_.push_front(node);
			it->second = {value, &node};
			return;
		}

		auto& node = list_.pop_back();
		if (node.key != RecentNode::SENTINEL) {
			recently_evicted_.erase(node.key);
		}
		recently_evicted_.emplace(key, make_pair(value, &node));
		node.key = key;
		list_.push_front(node);
	}
	bool contains(uint64_t key) { return recently_evicted_.contains(key); }
	value_type take(uint64_t key)
	{
		auto it = recently_evicted_.find(key);
		auto res = it->second.first;

		// release the node
		auto& node = *it->second.second;
		node.release();
		node.key = RecentNode::SENTINEL;
		list_.push_back(node);

		recently_evicted_.erase(it);
		return res;
	}
};

template <>
class RecencyPool<void>
{
	unordered_map<uint64_t, RecentNode*> recently_evicted_;
	size_t capacity_;
	unique_ptr<RecentNode[]> nodes_;
	List<RecentNode> list_;

public:
	RecencyPool(size_t capacity)
	    : recently_evicted_{}, capacity_{capacity},
	      nodes_{std::make_unique<RecentNode[]>(capacity)}, list_{}
	{
		recently_evicted_.reserve(capacity_);
		for (size_t i{0}; i < capacity_; ++i) {
			nodes_[i].key = RecentNode::SENTINEL;
			list_.push_front(nodes_[i]);
		}
	}

	void insert(uint64_t key)
	{
		if (auto it = recently_evicted_.find(key); it != recently_evicted_.end()) {
			// move to lru position
			auto& node = *it->second;
			node.release();
			list_.push_front(node);
			return;
		}

		auto& node = list_.pop_back();
		if (node.key != RecentNode::SENTINEL) {
			assert(recently_evicted_.size() == capacity_);
			recently_evicted_.erase(node.key);
		}
		recently_evicted_.emplace(key, &node);
		node.key = key;
		list_.push_front(node);
	}
	bool contains(uint64_t key) { return recently_evicted_.contains(key); }
};
