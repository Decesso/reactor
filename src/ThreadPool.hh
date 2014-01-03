#ifndef REACTOR_THREADPOOL_HEADER
#define REACTOR_THREADPOOL_HEADER

#include "Thread.hh"

#include <vector>
#include <cstddef>

class ThreadPool {
	typedef std::vector<Thread *> Threads;

	Runnable &runnable_;
	Threads threads_;

public:
	ThreadPool(Runnable &runnable, size_t count);
	~ThreadPool();
};

#endif // REACTOR_THREADPOOL_HEADER
