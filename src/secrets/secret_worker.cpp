#include "secrets/secret_worker.hpp"

#include <deque>
#include <mutex>
#include <thread>

namespace multistream {

struct SecretWorker::Channel {
	std::mutex mutex;
	Dispatcher dispatcher;
};

struct SecretWorker::Queue {
	std::mutex mutex;
	std::deque<std::function<void()>> jobs;
	bool running = false;
};

SecretWorker::SecretWorker(std::shared_ptr<SecretStore> store, Dispatcher dispatcher)
	: store_(std::move(store)),
	  channel_(std::make_shared<Channel>()),
	  queue_(std::make_shared<Queue>())
{
	channel_->dispatcher = std::move(dispatcher);
}

SecretWorker::~SecretWorker()
{
	close();
}

void SecretWorker::close()
{
	// Waits for a post in progress, so nothing reaches the Dispatcher afterwards.
	const std::lock_guard<std::mutex> lock(channel_->mutex);
	channel_->dispatcher = nullptr;
}

void SecretWorker::post(const std::shared_ptr<Channel> &channel, std::function<void()> work)
{
	const std::lock_guard<std::mutex> lock(channel->mutex);
	if (channel->dispatcher) {
		channel->dispatcher(std::move(work));
	}
}

void SecretWorker::enqueue(std::function<void()> job)
{
	{
		const std::lock_guard<std::mutex> lock(queue_->mutex);
		queue_->jobs.push_back(std::move(job));
		if (queue_->running) {
			return;
		}
		queue_->running = true;
	}

	std::thread([queue = queue_] {
		for (;;) {
			std::function<void()> next;
			{
				const std::lock_guard<std::mutex> lock(queue->mutex);
				if (queue->jobs.empty()) {
					queue->running = false;
					return;
				}
				next = std::move(queue->jobs.front());
				queue->jobs.pop_front();
			}
			next();
		}
	}).detach();
}

} // namespace multistream
