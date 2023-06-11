#include "coroutine.hpp"
#include <coroutine>
#include <cstdio>
#include <functional>
#include <thread>
#include <unistd.h>

void async_callback(void *args) {
  std::coroutine_handle<task<int>::promise_type> handle =
      std::coroutine_handle<task<int>::promise_type>::from_address(args);
  handle.promise()._value = 0;
}

void async_with_callback(std::function<void(void *)> callback, void *address) {
  std::thread([=]() {
    sleep(1);
    printf("sleep after 1s\n");
    callback(address);
  }).detach();
}

task<int> async_func() {
  auto handle = co_await current{};
  async_with_callback(async_callback, handle.address());
  co_await std::suspend_always{};
  co_return 0;
}

void example1() {
  auto t = async_func();
  // polling
  while (!t._h.promise()._value.has_value())
    ;
  printf("value: %d\n", t.get().value());
}

task<int> emptry() { co_return 0; }
task<int> async_func2() {
  co_await emptry();
  co_return 0;
}
void example2() {
  auto t = async_func2();
  // polling
  while (!t._h.promise()._value.has_value())
    ;
  printf("value: %d\n", t.get().value());
}

int main(int argc, char *argv[]) {
  example1();
  example2();
  return 0;
}
