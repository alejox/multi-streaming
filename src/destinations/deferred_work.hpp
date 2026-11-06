// Runs work on its own thread once a condition holds, so that nothing waiting for libobs
// ever sits on the UI thread, and one stuck job cannot hold up another.
//
// Jobs run on detached threads holding their own state, so they can safely outlive this
// object: OBS never unloads a module's code (obs-module.c:873 at OBS 33.0.1).
#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

namespace multistream {

class DeferredWork {
public:
	using Condition = std::function<bool()>;
	using Work = std::function<void()>;

	DeferredWork();

	// Abandons what is still waiting.
	~DeferredWork();

	DeferredWork(const DeferredWork &) = delete;
	DeferredWork &operator=(const DeferredWork &) = delete;

	// Runs work when the condition first holds. A job abandoned before that runs
	// on_abandon instead. Both run on the job's thread; the condition is polled there.
	void when(Condition condition, Work work, Work on_abandon = {});

	// Waits for the jobs to finish, and returns how many did not within the timeout.
	std::size_t drain(std::chrono::milliseconds timeout);

	// Jobs still waiting for their condition give up. Work already running goes on.
	void abandon();

private:
	struct State;
	std::shared_ptr<State> state_;
};

} // namespace multistream
