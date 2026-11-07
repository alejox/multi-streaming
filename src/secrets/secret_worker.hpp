// Runs secret store calls off the UI thread. A Keychain prompt or a locked Secret Service
// collection can block a call for as long as the user leaves it, which would freeze OBS.
//
// Jobs run one at a time, in submission order, on a worker thread that exists only while
// jobs are queued, so a put followed by a get of the same secret sees the put. Each result
// goes back through the Dispatcher, which runs it on the UI thread.
//
// Lifetime: the owner calls close() (or destroys the worker) on the UI thread before the
// Dispatcher's target goes away. After close() returns, no result reaches the Dispatcher.
// Queued jobs still run to the end, so a key or a rotated token being stored is not lost;
// they hold the store, never the owner. This mirrors StreamingEngine's key reads.
#pragma once

#include "destinations/secret_store.hpp"

#include <functional>
#include <memory>
#include <utility>

namespace multistream {

class SecretWorker {
public:
	using Dispatcher = std::function<void(std::function<void()>)>;

	SecretWorker(std::shared_ptr<SecretStore> store, Dispatcher dispatcher);
	~SecretWorker();
	SecretWorker(const SecretWorker &) = delete;
	SecretWorker &operator=(const SecretWorker &) = delete;

	// job runs on the worker thread with the store; done runs through the Dispatcher with
	// its result, unless the worker was closed first. Neither may capture the secret in a
	// log or message.
	template<typename Result>
	void run(std::function<Result(SecretStore &)> job, std::function<void(Result)> done)
	{
		enqueue([store = store_, channel = channel_, job = std::move(job), done = std::move(done)] {
			Result result = job(*store);
			post(channel, [done, result = std::move(result)] { done(result); });
		});
	}

	void close();

private:
	struct Channel;
	struct Queue;

	void enqueue(std::function<void()> job);
	static void post(const std::shared_ptr<Channel> &channel, std::function<void()> work);

	std::shared_ptr<SecretStore> store_;
	std::shared_ptr<Channel> channel_;
	std::shared_ptr<Queue> queue_;
};

} // namespace multistream
