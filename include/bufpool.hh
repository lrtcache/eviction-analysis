#pragma once
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include "eviction_policy.hh"

using std::make_unique;
using std::size_t;
using std::span;
using std::unique_ptr;
using std::unordered_map;

struct DataRequest
{
	uint64_t page_id;
};

template <IsEvictionPolicy Policy>
class _BufferPageHandle : public Policy::Metadata
{
public:
	uint64_t page_id;
	void* data_;
	void* data() { return data_; }
};

template <class T>
concept IsBufferPool = true; // requires(T t) { typename T::BufferHandle; };

template <IsBufferPool BufPoolType>
class PageHandle
{
private:
	using BufferPoolType = BufPoolType;
	using InternalHandle = BufPoolType::BufferHandle;
	BufferPoolType& bufpool_;
	InternalHandle& handle_;

public:
	PageHandle(BufferPoolType& bp, InternalHandle& h) : bufpool_{bp}, handle_{h} {}
	// remove copy assignment and constructor
	PageHandle(PageHandle& other) = delete;
	PageHandle<BufferPoolType> operator=(PageHandle& other) = delete;
	PageHandle(const PageHandle& other) = delete;
	PageHandle<BufferPoolType> operator=(const PageHandle& other) = delete;

	// remove move assignment and constructor
	PageHandle(PageHandle&& other) = delete;
	PageHandle<BufferPoolType> operator=(PageHandle&& other) = delete;

	void* data() { return handle_.data(); }
	~PageHandle() { bufpool_.release_page(handle_); }
};

template <IsEvictionPolicy EvictionPolicy, size_t PageSize = 4096>
class BufPool
{
public:
	using BufferHandle = _BufferPageHandle<EvictionPolicy>;
	static constexpr size_t PAGESIZE{PageSize};
	using PolicyType = EvictionPolicy;
	using PolicyMetaType = typename EvictionPolicy::Metadata;
	static constexpr uint64_t INVALID_ID{std::numeric_limits<uint64_t>::max()};

private:
	size_t num_pages_{};
	unique_ptr<BufferHandle[]> pages_;
	EvictionPolicy policy_;
	unordered_map<uint64_t, BufferHandle*> lookup_tbl_;

	void evict_callback(PolicyMetaType& meta)
	{
		auto& ph = static_cast<BufferHandle&>(meta);
		lookup_tbl_.erase(ph.page_id);
		ph.page_id = INVALID_ID;
	}

public:
	BufPool(size_t num_pages)
	    : num_pages_{num_pages}, pages_{make_unique<BufferHandle[]>(num_pages)},
	      policy_{num_pages}
	{
		for (size_t i{0}; i < num_pages_; ++i) {
			BufferHandle& pg{pages_[i]};
			// mock buffer pool stores the 'id' of each page in its data
			pg.data_ = reinterpret_cast<void*>(i + 1);
			pg.page_id = INVALID_ID;
			policy_.register_metadata(static_cast<PolicyMetaType&>(pg));
		}
	}
	BufPool(size_t num_pages, EvictionPolicy&& policy)
	    : num_pages_{num_pages}, pages_{make_unique<BufferHandle[]>(num_pages)},
	      policy_{std::move(policy)}
	{
		for (size_t i{0}; i < num_pages_; ++i) {
			BufferHandle& pg{pages_[i]};
			// mock buffer pool stores the 'id' of each page in its data
			pg.data_ = reinterpret_cast<void*>(i + 1);
			pg.page_id = INVALID_ID;
			policy_.register_metadata(static_cast<PolicyMetaType&>(pg));
		}
	}

	[[nodiscard]] size_t capacity() const noexcept { return num_pages_; }
	[[nodiscard]] size_t resident_pages() const noexcept { return lookup_tbl_.size(); }
	[[nodiscard]] bool contains(uint64_t page_id) const
	{
		return lookup_tbl_.contains(page_id);
	}

	BufferHandle& _get_page(DataRequest req)
	{
		if (!lookup_tbl_.contains(req.page_id)) {
			PolicyMetaType& meta{policy_.get(
			    req.page_id, [&](PolicyMetaType& meta) { evict_callback(meta); })};
			auto& res = static_cast<BufferHandle&>(meta);
			assert(res.page_id == INVALID_ID);
			res.page_id = req.page_id;
			lookup_tbl_.insert({req.page_id, &res});
			return res;
		} else {
			auto& res = *lookup_tbl_[req.page_id];
			assert(res.page_id == req.page_id);
			policy_.touch(static_cast<PolicyMetaType&>(res));
			return res;
		}
	}
	PageHandle<BufPool> get_page(DataRequest req)
	{
		return PageHandle<BufPool>{*this, _get_page(req)};
	}
	void release_page(BufferHandle& handle)
	{
		policy_.release(static_cast<PolicyMetaType&>(handle));
	}
};
