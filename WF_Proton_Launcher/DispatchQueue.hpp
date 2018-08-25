#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <cstdio>
#include <cstdint>

class DispatchQueue
{
public:
	typedef std::function<void()> WorkerFuncType;

	DispatchQueue(size_t threadCount = 0);
	~DispatchQueue();

	DispatchQueue(const DispatchQueue&) = delete;
	DispatchQueue(DispatchQueue&&) = delete;
	DispatchQueue& operator=(const DispatchQueue&) = delete;
	DispatchQueue& operator=(DispatchQueue&&) = delete;

	void dispatch(const WorkerFuncType& work);
	void dispatch(WorkerFuncType&& work);

	bool busy() const;

	void stop();
	void join();
	void wait();

private:
	void workThreadHandler();

	std::mutex mQueueMutex;
	std::vector<std::thread> mWorkerThreads;
	std::queue<WorkerFuncType> mWorkQueue;
	std::condition_variable mWorkCV;
	bool mQuitting;
};