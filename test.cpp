#include "coroutine.hpp"
#include <coroutine>
#include <cstdio>
#include <functional>
#include <thread>
#include <unistd.h>

void async_callback(void *args) {
  std::coroutine_handle<async_task<int>::promise_type> handle =
      std::coroutine_handle<async_task<int>::promise_type>::from_address(args);
  handle.promise().return_value_ = 1;
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

async_task_endpoint<int> async_func() {
  printf("async_func begin\n");
  auto handle =
      co_await get_handle_awaitable<async_task_endpoint<int>::promise_type>{};
  async_with_callback(async_callback, handle.promise().caller_.address());

  // should no come here
  printf("[should not come here!!]async_func end\n");
  co_return {false, 0};
  // 剩下的都是没用的了，因为直接调度到了父协程
  /* printf("before suspend\n"); */
  /* co_await std::suspend_always{}; */
  /* printf("async_func done\n"); */
}

async_task<int> emptry() { co_return 2; }
async_task<int> async_func2() {
  printf("emptry coroutine begin\n");
  int ret = co_await emptry();
  printf("emptry coroutine end\n");
  co_return ret;
}
void example2() {
  auto t = async_func2();
  t();
  // polling
  while (!t.done())
    ;
  printf("value: %d\n", t.result());
}

async_task<int> async_wrapper() {
  int ret = co_await async_func();
  co_return ret;
}

void example3() {
  auto t = async_wrapper();
  t();
  // polling
  while (!t.done())
    ;
  printf("value: %d\n", t.result());
}

// 再考虑一下在callback中失败的情况，看看会不会覆盖返回值

int main(int argc, char *argv[]) {
  /* example1(); */
  example2();
  example3();
  return 0;
}
