#include "destinations/deferred_work.hpp"

#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using multistream::DeferredWork;
using namespace std::chrono_literals;

TEST_CASE(work_runs_on_another_thread)
{
	DeferredWork work;
	std::thread::id ran_on;
	work.when([] { return true; }, [&] { ran_on = std::this_thread::get_id(); });

	CHECK_EQ(work.drain(5s), 0u);
	CHECK(ran_on != std::thread::id{});
	CHECK(ran_on != std::this_thread::get_id());
}

TEST_CASE(work_waits_for_its_condition)
{
	DeferredWork work;
	std::atomic<bool> open{false};
	std::atomic<bool> ran{false};
	work.when([&] { return open.load(); }, [&] { ran = true; });

	CHECK_EQ(work.drain(150ms), 1u);
	CHECK(!ran);

	open = true;
	CHECK_EQ(work.drain(5s), 0u);
	CHECK(ran);
}

TEST_CASE(a_stuck_job_does_not_hold_up_another)
{
	DeferredWork work;
	auto release = std::make_shared<std::atomic<bool>>(false);
	std::atomic<bool> other_ran{false};

	work.when([] { return true; }, [release] {
		while (!*release) {
			std::this_thread::sleep_for(5ms);
		}
	});
	work.when([] { return true; }, [&] { other_ran = true; });

	CHECK_EQ(work.drain(300ms), 1u);
	CHECK(other_ran);

	*release = true;
	CHECK_EQ(work.drain(5s), 0u);
}

TEST_CASE(abandoning_runs_the_fallback_instead_of_the_work)
{
	auto ran = std::make_shared<std::atomic<bool>>(false);
	auto abandoned = std::make_shared<std::atomic<bool>>(false);

	DeferredWork work;
	work.when([] { return false; }, [ran] { *ran = true; }, [abandoned] { *abandoned = true; });
	work.abandon();

	CHECK_EQ(work.drain(5s), 0u);
	CHECK(!*ran);
	CHECK(*abandoned);
}

TEST_CASE(destroying_abandons_what_still_waits_without_blocking)
{
	auto abandoned = std::make_shared<std::atomic<bool>>(false);
	const auto start = std::chrono::steady_clock::now();
	{
		DeferredWork work;
		work.when([] { return false; }, [] {}, [abandoned] { *abandoned = true; });
	}
	CHECK(std::chrono::steady_clock::now() - start < 1s);

	for (int i = 0; i < 400 && !*abandoned; ++i) {
		std::this_thread::sleep_for(5ms);
	}
	CHECK(*abandoned);
}

TEST_CASE(work_added_after_abandoning_is_abandoned_too)
{
	auto ran = std::make_shared<std::atomic<bool>>(false);
	auto abandoned = std::make_shared<std::atomic<bool>>(false);

	DeferredWork work;
	work.abandon();
	work.when([] { return true; }, [ran] { *ran = true; }, [abandoned] { *abandoned = true; });

	CHECK_EQ(work.drain(5s), 0u);
	CHECK(!*ran);
	CHECK(*abandoned);
}
