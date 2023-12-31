#include "coroutine.hpp"
#include <catch2/catch_test_macros.hpp>
#include <coroutine>
#include <cstdio>
#include <functional>
#include <thread>
#include <unistd.h>

struct wakable {
  bool await_ready() const noexcept {
    printf("await ready\n");
    return false;
  }
  void await_suspend(std::coroutine_handle<> handle) {
    printf("await suspend: %p\n", handle.address());
    _h = handle;
  }
  void await_resume() {}
  std::coroutine_handle<> _h;
  void wakeup() { _h.resume(); }
};

task<int> async_func(wakable &w) {
  printf("async_func begin\n");
  printf("before suspend\n");
  co_await w;
  printf("async_func done\n");
  co_return 1;
}

TEST_CASE("resume from subroutine", "subroutine") {
  wakable w{};
  auto t = async_func(w);
  printf("%pvs%p\n", t._h.address(), w._h.address());
  w.wakeup();
  REQUIRE(t.get().value() == 1);
}

task<int> empty() { co_return 2; }
task<int> async_func2() {
  int ret = co_await empty();
  co_return ret;
}

TEST_CASE("resume from empty", "empty") {
  auto t = async_func2();
  REQUIRE(t.get().value() == 2);
}

task<int> async_wrapper(wakable &w) {
  int ret = co_await async_func(w);
  co_return ret;
}

TEST_CASE("subroutine wrapper", "subroutine") {
  wakable w;
  auto t = async_wrapper(w);
  w.wakeup();
  REQUIRE(t.get().value() == 1);
}
