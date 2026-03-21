#include <iostream>
#include <memory>
#include <thread>
#include <queue>
#include <vector>
#include<mutex>
#include <condition_variable>
#include <chrono>
#include <future>
#include <functional>
#include <type_traits>
using namespace std;
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
		value=move(q.front());
		q.pop();
	}
	bool try_pop(T& value)
	{
		lock_guard<mutex>lock(mut);
		if (q.empty())
		{
			return false;
		}
		value=move(q.front());
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
		value=move(q.front());
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
	void notify_all()
	{
		cv.notify_all();
	}
};


class function_pack
{
private:

	class impl_base
	{
	public:
		virtual void call() = 0;
		virtual ~impl_base() = default;
	};

	unique_ptr <impl_base>impl;
	template<typename F>
	class impl_type :public impl_base
	{
	public:
		F f;
		impl_type(F&& temp_f) :f(move(temp_f))
		{

		}
		void call()
		{
			f();
		}
	};
public:
	template<typename F>
	function_pack(F&& temp_f) :impl(new impl_type<F>(move(temp_f)))
	{
	}
	function_pack() = default;

	void operator()()
	{
		if (impl) impl->call();
		
	}
	function_pack(function_pack&& fp) noexcept :impl(move(fp.impl))
	{
		
	}
	function_pack& operator=(function_pack&& fp)noexcept
	{
		impl = move(fp.impl);
		return *this;
	}

	function_pack(const function_pack& fp) = delete;
	function_pack& operator=(const function_pack &fp) = delete;


};

class thread_pool
{
private:
	thread_safe_queue<function_pack> work_q;
	vector<thread>threads;
	atomic <bool> done = false;
	unsigned thread_num = thread::hardware_concurrency() > 2 ? thread::hardware_concurrency() : 2;
	void do_work()
	{
		while (!done)
		{
			function_pack task;
			work_q.wait_pop(task);
			task();
		}
	}

public:

	template<typename func>
	future<invoke_result_t<func>>submit(func f)
	{
		
		using type = invoke_result_t<func>;
		packaged_task<type()>task(move(f));
		future<type>res = task.get_future();
		work_q.push(function_pack(move(task)));
		return res;
		
	}
	
	void stop()
	{
		if (done = true)
		{
			return;
		}
		done = true;
		for (unsigned i = 0; i < thread_num; i++)
		{
			work_q.push(function_pack{});
		}
		work_q.notify_all();
		for (auto& t : threads)
		{
			if (t.joinable())
			{
				t.join();
			}
		}
		work_q.clear();
	}
	thread_pool()
	{
		
		for (unsigned i = 0; i < thread_num; i++)
		{
			threads.emplace_back(&thread_pool::do_work, this);

		}
	}
	~thread_pool()
	{
		
		stop();
	}
};