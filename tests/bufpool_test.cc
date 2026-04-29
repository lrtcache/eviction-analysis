#include <gtest/gtest.h>
#include <array>
#include <map>
#include <set>

#include "2q_policy.hh"
#include "bufpool.hh"
#include "fifo_policy.hh"
#include "lru_policy.hh"
#include "lru2_policy.hh"
#include "random_policy.hh"
#include "arc_policy.hh"

namespace
{

template <IsEvictionPolicy Policy>
class BufPoolTest : public ::testing::Test
{
public:
	using BufferPool = BufPool<Policy>;
};

TYPED_TEST_SUITE_P(BufPoolTest);

TYPED_TEST_P(BufPoolTest, BasicTest)
{
	using BufferPool = typename TestFixture::BufferPool;
	constexpr std::array<uint64_t, 4> page_ids{11, 22, 33, 44};
	BufferPool bp{page_ids.size()};
	std::map<uint64_t, void*> pg_data;

	for (auto pageid : page_ids) {
		auto page = bp.get_page(DataRequest{pageid});
		void* data{page.data()};
		pg_data[pageid] = data;
	}
	ASSERT_EQ(pg_data.size(), page_ids.size());

	for (auto pageid : page_ids) {
		auto page = bp.get_page(DataRequest{pageid});
		void* data{page.data()};
		ASSERT_EQ(pg_data[pageid], data) << "page data shouldn't change";
	}
}

TYPED_TEST_P(BufPoolTest, ResidentPagesUseDistinctFrames)
{
	using BufferPool = typename TestFixture::BufferPool;
	constexpr std::array<uint64_t, 5> page_ids{1, 2, 3, 4, 5};
	BufferPool bp{page_ids.size()};
	std::set<void*> resident_frames;

	for (auto pageid : page_ids) {
		auto page = bp.get_page(DataRequest{pageid});
		resident_frames.insert(page.data());
	}

	ASSERT_EQ(resident_frames.size(), page_ids.size())
	    << "distinct resident pages should occupy distinct frames";
}

TYPED_TEST_P(BufPoolTest, EvictedPagesAreRemovedFromLookup)
{
	using BufferPool = typename TestFixture::BufferPool;
	BufferPool bp{2};

	{
		auto page = bp.get_page(DataRequest{1});
		ASSERT_TRUE(bp.contains(1));
	}

	{
		auto page = bp.get_page(DataRequest{2});
		ASSERT_TRUE(bp.contains(2));
	}

	{
		auto page = bp.get_page(DataRequest{3});
		ASSERT_FALSE(bp.contains(1));
		ASSERT_TRUE(bp.contains(2));
		ASSERT_TRUE(bp.contains(3));
	}

	{
		auto page = bp.get_page(DataRequest{1});
		ASSERT_TRUE(bp.contains(1));
		ASSERT_EQ(bp.resident_pages(), bp.capacity());
	}
}

REGISTER_TYPED_TEST_SUITE_P(BufPoolTest, BasicTest, ResidentPagesUseDistinctFrames,
                            EvictedPagesAreRemovedFromLookup);

using EvPolTypes =
    ::testing::Types<FifoPolicy, RandomPolicy, LRUPolicy, S2QPolicy, LRU2Policy, ARCPolicy>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, BufPoolTest, EvPolTypes);

TEST(LRU2PolicyTest, MissesPastEvictionThresholdDoNotCorruptFreeList)
{
	BufPool<LRU2Policy> bp{21};

	for (uint64_t page_id = 0; page_id < 23; ++page_id) {
		auto page = bp.get_page(DataRequest{page_id});
		ASSERT_NE(page.data(), nullptr);
	}

	ASSERT_LE(bp.resident_pages(), bp.capacity());
}

} // namespace
