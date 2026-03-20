#include <iostream>
#include <memory>
#include <thread>
#include <queue>
#include<mutex>
#include <condition_variable>
#include <chrono>
using namespace std;
//线程安全队列
template<typename T>
class thread_safe_queue
{
	condition_variable cv;
	mutable mutex mut;
	queue<T>q;
public:
	thread_safe_queue() = default;
	~thread_safe_queue() = default;
	thread_safe_queue(const thread_safe_queue&) = delete;
	thread_safe_queue& operator=(const thread_safe_queue&) = delete;
	void push(T&& value)
	{
		lock_guard<mutex>lock(mut);
		q.push(move(value));
		cv.notify_one();

	}
	void wait_pop(T& value)
	{
		unique_lock<mutex>lock(mut);
		cv.wait(lock, [this]() {return !this->q.empty(); });
		value = q.front();
		q.pop();
	}
	bool try_pop(T& value)
	{
		lock_guard<mutex>lock(mut);
		if (q.empty())
		{
			return false;
		}
		value = q.front();
		q.pop();
		return true;
	}
	bool wait_for_pop(T& value, chrono::milliseconds timeout)
	{
		unique_lock<mutex>lock(mut);
		if (!cv.wait_for(lock, timeout, [this] {return !this->q.empty();}))
		{
			return false;
		}
		value = q.front();
		q.pop();
		return true;
	}
	bool is_empty()const
	{
		lock_guard<mutex>lock(mut);
		return q.empty();


	}
	size_t size() const
	{
		lock_guard<mutex>lock(mut);
		return q.size();
	}
	void clear()
	{
		lock_guard<mutex>lock(mut);
		while (!q.empty())
		{
			q.pop();
		}
	}
};
