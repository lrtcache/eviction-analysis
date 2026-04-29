#pragma once
#include <cstdint>
#include <concepts>

template <class Fn, class Meta>
concept IsEvictCallback = requires { std::is_invocable_r_v<void, Fn, Meta&>; };

template <typename Meta>
struct CallbackType
{
	void operator()(Meta& m);
};

template <class P>
concept IsEvictionPolicy = requires(P policy, typename P::Metadata& meta, uint64_t page_id,
                                    CallbackType<typename P::Metadata&> cb) {
	typename P::Metadata;
	{ policy.get(page_id, cb) } -> std::same_as<typename P::Metadata&>;
	{ policy.touch(meta) } -> std::same_as<void>;
	{ policy.register_metadata(meta) } -> std::same_as<void>;
	{ policy.release(meta) } -> std::same_as<void>;
};
