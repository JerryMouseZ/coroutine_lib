#include "coroutine.hpp"
#include <catch2/catch_test_macros.hpp>
#include <coroutine>
#include <cstdio>
#include <functional>
#include <thread>
#include <unistd.h>

void async_callback(void *args) {
  std::coroutine_handle<task<int>::promise_type> handle =
      std::coroutine_handle<task<int>::promise_type>::from_address(args);
  handle.promise()._value = 1;
  handle.resume();
}

// 严格来说callback要写在polling中，不过这样可以简单很多
void async_with_callback(std::function<void(void *)> callback, void *address) {
  std::thread([=]() {
    sleep(1);
    printf("sleep after 1s\n");
    callback(address);
  }).detach();
}

task<int> async_func() {
  printf("async_func begin\n");
  auto handle = co_await current{};
  async_with_callback(async_callback, handle.address());
  printf("before suspend\n");
  co_await std::suspend_always{};
  printf("async_func done\n");
  co_return 0;
}

TEST_CASE("resume from subroutine", "subroutine") {
  auto t = async_func();
  t.start();
  // polling
  while (!t._h.promise()._value.has_value())
    ;
  REQUIRE(t.get().value() == 1);
}

task<int> empty() { co_return 2; }
task<int> async_func2() {
  int ret = co_await empty();
  co_return ret;
}

TEST_CASE("resume from empty", "empty") {
  auto t = async_func2();
  t.start();
  REQUIRE(t.get().value() == 2);
}

task<int> async_wrapper() {
  int ret = co_await async_func();
  co_return ret;
}

TEST_CASE("subroutine wrapper", "subroutine") {
  auto t = async_wrapper();
  t.start();
  // polling
  while (!t._h.promise()._value.has_value())
    ;
  REQUIRE(t.get().value() == 1);
}
