#pragma once
#include <cstdint>
#include <type_traits>
#include <cassert>

using std::size_t;

template <typename T>
struct ListNode
{
	ListNode<T>*prev, *next;

	ListNode() : prev{this}, next{this} {}

	void push_front(T& _other)
	{
		auto other = static_cast<ListNode<T>*>(&_other);
		assert(other->prev == other && other->next == other);
		next->prev = other;
		other->next = next;

		other->prev = this;
		this->next = other;
	}
	void push_back(T& _other)
	{
		auto other = static_cast<ListNode<T>*>(&_other);
		assert(other->prev == other && other->next == other);
		prev->next = other;
		other->prev = prev;

		other->next = this;
		this->prev = other;
	}
	T& pop_back()
	{
		assert(this->prev != this);
		auto res = this->prev;
		res->prev->next = this;
		this->prev = res->prev;
		res->release();
		return static_cast<T&>(*res);
	}
	void release()
	{
		prev->next = next;
		next->prev = prev;
		prev = next = this;
	}
};

template <typename T>
    requires std::is_base_of_v<ListNode<T>, T>
class List
{
private:
	ListNode<T> dummy_;
	size_t size_;

public:
	List() : dummy_{}, size_{} {}
	bool empty()
	{
		assert(size_ != 0 || (dummy_.next == &dummy_ && dummy_.prev == &dummy_));
		return size_ == 0;
	}
	size_t size() { return size_; }

	void push_front(T& other)
	{
		dummy_.push_front(other);
		size_++;
	}
	void push_back(T& other)
	{
		dummy_.push_back(other);
		size_++;
	}
	T& pop_back()
	{
		assert(size_ > 0);
		size_--;
		return dummy_.pop_back();
	}
	void _release_callback()
	{
		assert(size_ > 0);
		size_--;
	}
};
