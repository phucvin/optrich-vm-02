#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace optrich_test {

struct TestCase {
    std::string           name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back(TestCase{name, std::move(fn)});
    }
};

#define TEST(name) \
    static void test_fn_##name(); \
    static ::optrich_test::Registrar test_reg_##name(#name, test_fn_##name); \
    static void test_fn_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_TRUE failed: " #cond " at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(_oss.str()); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (!(_a == _b)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_EQ failed: " #a " (" << _a << ") != " #b " (" << _b << ") at " \
                 << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(_oss.str()); \
        } \
    } while (0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (std::fabs(_a - _b) > (eps)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_NEAR failed: " #a " (" << _a << ") vs " #b " (" << _b << ") at " \
                 << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(_oss.str()); \
        } \
    } while (0)

#define ASSERT_THROWS(expr, ExcType) \
    do { \
        bool _caught = false; \
        try { \
            (void)(expr); \
        } catch (const ExcType&) { \
            _caught = true; \
        } \
        if (!_caught) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_THROWS failed: expected " #ExcType " for " #expr " at " \
                 << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(_oss.str()); \
        } \
    } while (0)

inline int run_all_tests() {
    int passed = 0;
    int failed = 0;
    for (const auto& t : registry()) {
        try {
            t.fn();
            std::cout << "  [PASS] " << t.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cerr << "  [FAIL] " << t.name << ": " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "Summary: " << passed << " passed, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}

} // namespace optrich_test
