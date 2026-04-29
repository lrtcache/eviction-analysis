#pragma once

#include "recency_pool.hh"
#include <concepts>

template <typename T>
concept IsRecencyStore = requires(T t, size_t capacity, uint64_t key) {
	{ T(capacity) };
	{ t.contains(key) } -> std::same_as<bool>;
	{ t.insert(key) } -> std::same_as<void>;
};

static_assert(IsRecencyStore<RecencyPool<void>>);

template <typename T, typename V>
concept IsValuedRecencyStore = requires(T t, size_t capacity, uint64_t key, V value) {
	{ T(capacity) };
	{ t.contains(key) } -> std::same_as<bool>;
	{ t.insert(key, value) } -> std::same_as<void>;
	{ t.take(key) } -> std::same_as<V>;
};

class NullRecencyStore
{
public:
	NullRecencyStore(size_t) {}
	bool contains(uint64_t) { return false; }
	void insert(uint64_t) {}
};

template <typename V>
class NullValuedRecencyStore
{
public:
	NullValuedRecencyStore(size_t) {}
	bool contains(uint64_t) { return false; }
	void insert(uint64_t, V) {}
	V take(uint64_t) { throw("Invalid usage of class NullValuedRecencyStore"); }
};
