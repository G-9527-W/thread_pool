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
	void push(T value)
	{
		lock_guard<mutex>lock(mut);
		q.push(move(value));
		cv.notify_one();

	}
	void wait_pop(T& value)
	{
		unique_lock<mutex>lock(mut);
		cv.wait(lock, [this]() {return !this->q.empty(); });
		value = move(q.front());
		q.pop();
	}
	bool try_pop(T& value)
	{
		lock_guard<mutex>lock(mut);
		if (q.empty())
		{
			return false;
		}
		value = move(q.front());
		q.pop();
		return true;
	}
	bool wait_for_pop(T& value, chrono::milliseconds timeout)
	{
		unique_lock<mutex>lock(mut);
		if (!cv.wait_for(lock, timeout, [this] {return !this->q.empty(); }))
		{
			return false;
		}
		value = move(q.front());
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
	function_pack& operator=(const function_pack& fp) = delete;


};
class local_deque
{
private:
	mutable mutex mut;
	deque <function_pack>local_d;
public:
	local_deque() = default;
	local_deque(const local_deque& other) = delete;
	local_deque& operator=(const local_deque& othrer) = delete;

	void push_front(function_pack task)
	{
		lock_guard<mutex>lock(mut);
		local_d.push_front(move(task));
	}
	bool empty()const
	{
		lock_guard<mutex> lock(mut);
		return local_d.empty();
	}
	bool try_pop(function_pack &task)
	{
		
		lock_guard<mutex>lock(mut);
		if (local_d.empty())
		{
			return false;
		}
		task = move(local_d.front());
		local_d.pop_front();
		return true;
	}
	bool try_steal(function_pack& task)
	{
		
		lock_guard<mutex>lock(mut);
		if (local_d.empty())
		{
			return false;
		}
		task = move(local_d.back());
		local_d.pop_back();
		return true;
	}

};
class thread_pool
{
private:

	vector<unique_ptr<local_deque>>local_deque_v;
	thread_safe_queue<function_pack> work_q;
	vector<thread>threads;
	atomic <bool> done = false;
	static thread_local unsigned thread_i;
	static thread_local local_deque* local_q;
	unsigned thread_num = thread::hardware_concurrency() > 2 ? thread::hardware_concurrency() : 2;

	bool from_local(function_pack&task)
	{
		if (local_q)
		{
			return local_q->try_pop(task);
	
		}
		return false;
	}
	bool from_pool(function_pack& task)
	{
		if (!work_q.is_empty())
		{

			return work_q.try_pop(task);
		
		}
		return false;
	}
	
	bool steal_other(function_pack&task)
	{
		for (unsigned i = 0; i < thread_num; i++)
		{
			unsigned other_i = (thread_i + i + 1) % local_deque_v.size();
			if (local_deque_v[other_i]->try_steal(task))
			{
				return true;
			}
			
		}
		return false;
	}
	void run_task()
	{
		function_pack task;
		if (from_local(task) || from_pool(task) || steal_other(task))
		{
			task();
		}
		else
		{
			this_thread::yield();
		}
	}
	void do_work(unsigned temp_i)
	{
		thread_i = temp_i;
		local_q = local_deque_v[thread_i].get();


		while (!done)
		{
			run_task();
		}
	}

public:

	template<typename func>
	future<invoke_result_t<func>>submit(func f)
	{

		using type = invoke_result_t<func>;
		packaged_task<type()>task(move(f));
		future<type>res = task.get_future();
		if (local_q)
		{
			local_q->push_front(function_pack(move(task)));

		}
		else
		{
			work_q.push(function_pack(move(task)));
		}
	
		return res;

	}

	void stop()
	{
		if (done == true)
		{
			return;
		}
		done = true;
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

		try
		{
			for (unsigned i = 0; i < thread_num; i++)
			{
				local_deque_v.push_back(make_unique<local_deque>());
				threads.emplace_back(&thread_pool::do_work, this, i);

			}
		}
		catch (...)
		{
			stop();
			throw;
		}
	}
	~thread_pool()
	{

		stop();
	}
};
thread_local unsigned thread_pool::thread_i = 0;
thread_local local_deque* thread_pool::local_q = nullptr;
int main()
{
	return 0;
}