#include "DispatchQueue.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

DispatchQueue::DispatchQueue(size_t threadCount)
	: mQuitting(false)
{
	if (threadCount < 1)
		threadCount = std::thread::hardware_concurrency();

	mWorkerThreads.reserve(threadCount);
	for (size_t i = 0; i < threadCount; ++i)
		mWorkerThreads.emplace_back(std::bind(&DispatchQueue::workThreadHandler, this));
}
DispatchQueue::~DispatchQueue()
{
	stop();
	join();
}

void DispatchQueue::dispatch(const WorkerFuncType& work)
{
	std::unique_lock<std::mutex> lock(mQueueMutex);
	mWorkQueue.push(work);

	lock.unlock();
	mWorkCV.notify_all();
}
void DispatchQueue::dispatch(WorkerFuncType&& work)
{
	std::unique_lock<std::mutex> lock(mQueueMutex);
	mWorkQueue.push(std::move(work));

	lock.unlock();
	mWorkCV.notify_all();
}

bool DispatchQueue::busy() const
{
	return !mWorkQueue.empty();
}

void DispatchQueue::stop()
{
	mQuitting = true;

	mWorkCV.notify_all();
}

void DispatchQueue::join()
{
	for (size_t i = 0; i < mWorkerThreads.size(); ++i)
		if (mWorkerThreads[i].joinable())
			mWorkerThreads[i].join();
}

void DispatchQueue::wait()
{
	while (!mWorkQueue.empty())
		Sleep(1000);
}

void DispatchQueue::workThreadHandler()
{
	std::unique_lock<std::mutex> queueLock(mQueueMutex);

	do
	{
		mWorkCV.wait(queueLock, [this] {
			return (mWorkQueue.size() > 0 || mQuitting);
		});

		if (mWorkQueue.size() > 0 && !mQuitting)
		{
			auto work = std::move(mWorkQueue.front());
			mWorkQueue.pop();

			queueLock.unlock();

			work();

			queueLock.lock();
		}
	} while (!mQuitting);
}