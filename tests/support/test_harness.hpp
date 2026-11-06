// Minimal test harness: self-registering test cases, non-fatal CHECK and fatal REQUIRE.
#pragma once

#include <sstream>
#include <string>

namespace test_harness {

using TestFunction = void (*)();

struct Registrar {
	Registrar(const char *name, TestFunction function);
};

struct FatalFailure {};

void record_failure(const char *file, int line, const std::string &message);

template<typename T> std::string describe(const T &value)
{
	if constexpr (std::is_enum_v<T>) {
		return std::to_string(static_cast<long long>(value));
	} else {
		std::ostringstream stream;
		stream << value;
		return stream.str();
	}
}

} // namespace test_harness

#define TEST_CASE(name)                                                     \
	static void name();                                                 \
	static const test_harness::Registrar name##_registrar(#name, name); \
	static void name()

#define CHECK(condition)                                                        \
	do {                                                                    \
		if (!(condition)) {                                             \
			test_harness::record_failure(__FILE__, __LINE__, #condition); \
		}                                                               \
	} while (false)

#define CHECK_EQ(actual, expected)                                                                    \
	do {                                                                                          \
		const auto &check_actual = (actual);                                                  \
		const auto &check_expected = (expected);                                              \
		if (!(check_actual == check_expected)) {                                              \
			test_harness::record_failure(__FILE__, __LINE__,                              \
						     std::string(#actual " == " #expected ": got ") + \
							     test_harness::describe(check_actual) +   \
							     ", expected " +                          \
							     test_harness::describe(check_expected)); \
		}                                                                                     \
	} while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                                       \
	do {                                                                                          \
		const double check_actual = (actual);                                                 \
		const double check_expected = (expected);                                             \
		const double check_difference = check_actual - check_expected;                        \
		if (check_difference > (tolerance) || check_difference < -(tolerance)) {              \
			test_harness::record_failure(__FILE__, __LINE__,                              \
						     std::string(#actual " near " #expected ": got ") + \
							     std::to_string(check_actual));           \
		}                                                                                     \
	} while (false)

#define REQUIRE(condition)                                                      \
	do {                                                                    \
		if (!(condition)) {                                             \
			test_harness::record_failure(__FILE__, __LINE__, #condition); \
			throw test_harness::FatalFailure{};                     \
		}                                                               \
	} while (false)
