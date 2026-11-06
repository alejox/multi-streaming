#include "test_harness.hpp"

#include <cstdio>
#include <exception>
#include <vector>

namespace test_harness {
namespace {

struct TestCase {
	const char *name;
	TestFunction function;
};

std::vector<TestCase> &registry()
{
	static std::vector<TestCase> tests;
	return tests;
}

int failure_count = 0;

} // namespace

Registrar::Registrar(const char *name, TestFunction function)
{
	registry().push_back({name, function});
}

void record_failure(const char *file, int line, const std::string &message)
{
	++failure_count;
	std::fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, message.c_str());
}

} // namespace test_harness

int main()
{
	using namespace test_harness;

	int failed_tests = 0;
	for (const TestCase &test : registry()) {
		const int failures_before = failure_count;
		try {
			test.function();
		} catch (const FatalFailure &) {
		} catch (const std::exception &error) {
			record_failure(test.name, 0, std::string("unexpected exception: ") + error.what());
		}
		if (failure_count != failures_before) {
			++failed_tests;
			std::fprintf(stderr, "[ FAILED ] %s\n", test.name);
		}
	}

	std::printf("%zu tests, %d failed\n", registry().size(), failed_tests);
	return failed_tests == 0 ? 0 : 1;
}
