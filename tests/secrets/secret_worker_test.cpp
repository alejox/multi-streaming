#include "secrets/secret_worker.hpp"

#include "secrets/in_memory_secret_store.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

using namespace multistream;

namespace {

// Stands in for the UI thread: work posted by the worker waits here until the test runs it.
class ManualDispatcher {
public:
	SecretWorker::Dispatcher dispatcher()
	{
		return [this](std::function<void()> work) {
			const std::lock_guard<std::mutex> lock(mutex_);
			posted_.push_back(std::move(work));
			changed_.notify_all();
		};
	}

	// Runs the next posted work on the calling thread. False after waiting in vain.
	bool run_next(std::chrono::milliseconds timeout = std::chrono::seconds(5))
	{
		std::function<void()> work;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			if (!changed_.wait_for(lock, timeout, [this] { return !posted_.empty(); })) {
				return false;
			}
			work = std::move(posted_.front());
			posted_.pop_front();
		}
		work();
		return true;
	}

private:
	std::mutex mutex_;
	std::condition_variable changed_;
	std::deque<std::function<void()>> posted_;
};

// Blocks every call until released, like a Keychain prompt left open.
class BlockingStore final : public SecretStore {
public:
	std::optional<SecretError> put(const std::string &id, const std::string &secret) override
	{
		wait();
		const std::lock_guard<std::mutex> lock(inner_mutex_);
		return inner_.put(id, secret);
	}
	SecretLookup get(const std::string &id) override
	{
		wait();
		const std::lock_guard<std::mutex> lock(inner_mutex_);
		return inner_.get(id);
	}
	std::optional<SecretError> remove(const std::string &id) override
	{
		wait();
		const std::lock_guard<std::mutex> lock(inner_mutex_);
		return inner_.remove(id);
	}

	void release()
	{
		const std::lock_guard<std::mutex> lock(mutex_);
		released_ = true;
		changed_.notify_all();
	}

private:
	void wait()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		changed_.wait(lock, [this] { return released_; });
	}

	std::mutex mutex_;
	std::condition_variable changed_;
	bool released_ = false;
	std::mutex inner_mutex_;
	InMemorySecretStore inner_;
};

} // namespace

TEST_CASE(a_result_arrives_through_the_dispatcher_not_on_the_worker_thread)
{
	auto store = std::make_shared<InMemorySecretStore>();
	store->put("a", "live_a");
	ManualDispatcher ui;
	SecretWorker worker(store, ui.dispatcher());

	std::optional<std::string> seen;
	std::thread::id done_thread;
	worker.run<SecretLookup>([](SecretStore &secrets) { return secrets.get("a"); },
				 [&](SecretLookup lookup) {
					 seen = lookup.secret;
					 done_thread = std::this_thread::get_id();
				 });

	REQUIRE(ui.run_next());
	CHECK_EQ(seen.value_or(""), std::string("live_a"));
	CHECK(done_thread == std::this_thread::get_id());
}

TEST_CASE(jobs_run_in_the_order_they_were_submitted)
{
	auto store = std::make_shared<InMemorySecretStore>();
	ManualDispatcher ui;
	SecretWorker worker(store, ui.dispatcher());

	worker.run<std::optional<SecretError>>([](SecretStore &secrets) { return secrets.put("a", "first"); },
					       [](std::optional<SecretError>) {});
	worker.run<std::optional<SecretError>>([](SecretStore &secrets) { return secrets.put("a", "second"); },
					       [](std::optional<SecretError>) {});
	std::optional<std::string> seen;
	worker.run<SecretLookup>([](SecretStore &secrets) { return secrets.get("a"); },
				 [&](SecretLookup lookup) { seen = lookup.secret; });

	REQUIRE(ui.run_next());
	REQUIRE(ui.run_next());
	REQUIRE(ui.run_next());
	CHECK_EQ(seen.value_or(""), std::string("second"));
}

TEST_CASE(a_blocked_store_never_blocks_the_caller)
{
	auto store = std::make_shared<BlockingStore>();
	ManualDispatcher ui;
	SecretWorker worker(store, ui.dispatcher());

	bool done = false;
	worker.run<std::optional<SecretError>>([](SecretStore &secrets) { return secrets.put("a", "live_a"); },
					       [&](std::optional<SecretError>) { done = true; });

	// run returned while the store is still blocked.
	CHECK(!ui.run_next(std::chrono::milliseconds(50)));
	CHECK(!done);
	store->release();
	REQUIRE(ui.run_next());
	CHECK(done);
}

TEST_CASE(after_close_the_job_still_runs_but_its_result_is_dropped)
{
	auto store = std::make_shared<BlockingStore>();
	ManualDispatcher ui;
	bool done = false;
	{
		SecretWorker worker(store, ui.dispatcher());
		worker.run<std::optional<SecretError>>(
			[](SecretStore &secrets) { return secrets.put("a", "live_a"); },
			[&](std::optional<SecretError>) { done = true; });
		worker.close();
	}
	store->release();

	// The put completes on its own: a rotated token or a new key is not lost.
	bool stored = false;
	for (int attempt = 0; attempt < 500 && !stored; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		stored = store->get("a").secret.has_value();
	}
	CHECK(stored);
	CHECK(!ui.run_next(std::chrono::milliseconds(50)));
	CHECK(!done);
}
