#include <condition_variable>
#include <mutex>
#include <queue>

class blocking_int_queue
{
private:
	std::queue<int> data;
	std::mutex m;
	std::condition_variable cond;

public:
	void push(int i)
	{
		std::unique_lock<std::mutex> l(m);
		data.push(i);
		std::size_t s = data.size();
		l.unlock();
		if(s == 1)
		{
			cond.notify_one();
		}
	}
	int pop()
	{
		std::unique_lock<std::mutex> l(m);
		while(data.empty())
		{
			cond.wait(l);
		}
		int ret = data.front();
		l.unlock();
		data.pop();
		return ret;
	}
};
