#pragma once

#include <cassert>
#include <concepts>
#include <utility>
#include <vector>

using std::size_t;
using std::vector;

namespace LRU2
{

template <typename Callback, typename T>
concept IsHeapCallback = std::is_invocable_r_v<void, Callback, T, size_t>;

// min-heap by default
template <typename T, typename Compare, typename Callback>
    requires std::predicate<Compare, const T&, const T&> &&
             std::is_default_constructible_v<T> && IsHeapCallback<Callback, T>
class UpdatableHeap
{
private:
	vector<T> heap_;
	size_t size_;
	[[no_unique_address]] Compare cmp_;
	[[no_unique_address]] Callback callback_;

	bool _compare_lt(const T& lhs, const T& rhs) { return cmp_(lhs, rhs); }

	static inline size_t parent(size_t i) { return (i - 1) >> 1; }
	static inline size_t left(size_t i) { return (i << 1) + 1; }
	static inline size_t right(size_t i) { return (i + 1) << 1; }

public:
	UpdatableHeap(size_t capacity) : heap_(capacity), size_{}, cmp_{}, callback_{} {}

	const T& get_min() { return heap_[0]; }

	void push(T value)
	{
		heap_[size_] = value;
		auto cur = size_;
		size_++;

		while (cur > 0 && _compare_lt(heap_[cur], heap_[parent(cur)])) {
			std::swap(heap_[cur], heap_[parent(cur)]);
			callback_(heap_[cur], cur);
			cur = parent(cur);
		}
		callback_(heap_[cur], cur);
	}

	T pop()
	{
		T res{heap_[0]};
		assert(size_ > 0);
		std::swap(heap_[0], heap_[size_ - 1]);
		callback_(heap_[0], 0);
		callback_(heap_[size_ - 1], size_ - 1);
		size_--;
		size_t cur{0};
		while (true) {
			size_t swap_cand{cur};
			if (left(cur) < size_ && _compare_lt(heap_[left(cur)], heap_[swap_cand])) {
				swap_cand = left(cur);
			}
			if (right(cur) < size_ && _compare_lt(heap_[right(cur)], heap_[swap_cand])) {
				swap_cand = right(cur);
			}
			if (swap_cand == cur) break;
			std::swap(heap_[cur], heap_[swap_cand]);
			callback_(heap_[cur], cur);
			callback_(heap_[swap_cand], swap_cand);
			cur = swap_cand;
		}
		return res;
	}

	void update(size_t heap_id, T&& value)
	{
		if (_compare_lt(value, heap_[heap_id])) {
			heap_[heap_id] = value;
			// new value is 'smaller', go up
			while (heap_id > 0 && _compare_lt(heap_[heap_id], heap_[parent(heap_id)])) {
				std::swap(heap_[heap_id], heap_[parent(heap_id)]);
				callback_(heap_[heap_id], heap_id);
				callback_(heap_[parent(heap_id)], parent(heap_id));
				heap_id = parent(heap_id);
			}
		} else {
			heap_[heap_id] = value;
			// new value is 'larger', go down
			size_t cur{heap_id};
			while (true) {
				size_t swap_cand{cur};
				if (left(cur) < size_ &&
				    _compare_lt(heap_[left(cur)], heap_[swap_cand])) {
					swap_cand = left(cur);
				}
				if (right(cur) < size_ &&
				    _compare_lt(heap_[right(cur)], heap_[swap_cand])) {
					swap_cand = right(cur);
				}
				if (swap_cand == cur) break;
				std::swap(heap_[cur], heap_[swap_cand]);
				callback_(heap_[cur], cur);
				callback_(heap_[swap_cand], swap_cand);
				cur = swap_cand;
			}
		}
	}
};

} // namespace LRU2
