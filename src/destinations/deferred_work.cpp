#include "destinations/deferred_work.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace multistream {
namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(50);

} // namespace

struct DeferredWork::State {
	std::mutex mutex;
	std::condition_variable changed;
	std::size_t jobs = 0;
	bool abandoned = false;
};

DeferredWork::DeferredWork() : state_(std::make_shared<State>()) {}

DeferredWork::~DeferredWork()
{
	abandon();
}

void DeferredWork::when(Condition condition, Work work, Work on_abandon)
{
	{
		const std::lock_guard<std::mutex> lock(state_->mutex);
		++state_->jobs;
	}

	std::thread([state = state_, condition = std::move(condition), work = std::move(work),
		     on_abandon = std::move(on_abandon)] {
		bool abandoned = false;
		{
			std::unique_lock<std::mutex> lock(state->mutex);
			while (!state->abandoned) {
				lock.unlock();
				const bool ready = condition();
				lock.lock();
				if (ready) {
					break;
				}
				state->changed.wait_for(lock, kPollInterval);
			}
			abandoned = state->abandoned;
		}

		if (!abandoned) {
			work();
		} else if (on_abandon) {
			on_abandon();
		}

		const std::lock_guard<std::mutex> lock(state->mutex);
		--state->jobs;
		state->changed.notify_all();
	}).detach();
}

std::size_t DeferredWork::drain(std::chrono::milliseconds timeout)
{
	std::unique_lock<std::mutex> lock(state_->mutex);
	state_->changed.wait_for(lock, timeout, [this] { return state_->jobs == 0; });
	return state_->jobs;
}

void DeferredWork::abandon()
{
	const std::lock_guard<std::mutex> lock(state_->mutex);
	state_->abandoned = true;
	state_->changed.notify_all();
}

} // namespace multistream
