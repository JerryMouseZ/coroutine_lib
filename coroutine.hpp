#pragma once
#ifndef _COROUTINE_HPP_
#define _COROUTINE_HPP_

#include <concepts>
#include <coroutine>
#include <cstdio>
#include <optional>

#ifndef NDEBUG
#define DEBUG_PRINTF(...)                                                      \
  do {                                                                         \
    fprintf(stderr, "[%s:%d]: ", __FILE__, __LINE__);                          \
    fprintf(stderr, __VA_ARGS__);                                              \
  } while (0);
#else
#define DEBUG_PRINTF(...) void(0)
#endif // !NDEBUG

// 协程类定义
// 协程类支持自动的调度，在创建协程对象的时候，就会自动开始运行
// 创建子协程的方法是co_await async_func()，async_func()返回一个协程对象
// 协程内部创建其它协程对象的时候也会自动开始运行
// 当协程最内部，因为co_await而无法继续执行的时候，控制权开始组层往上层传递
// 也就是回到刚刚调用co_await async_func的位置
// 这时我们重载了task::operator
// co_await，在子协程的handle中保存了当前协程的handle
// 以此往上直到最外层，所有的子协程都保存了父协程的handle
// 此时协程无法继续运行，将控制权提交给最外层调用协程的地方
// 等到polling时，callback恢复最内层的协程，这时最内层的协程会将控制权交给父协程
// 我们定义了final_suspend的返回值，让其在suspend的时候恢复父协程
// 以此类推，直到最外层的协程，这时整个协程运行结束
// 然后可以通过get()获取返回值，如果还没有返回值，get()会返回std::nullopt，出现错误

// 在协程内部，我们可以使用co_await current{}来获取当前协程的handle
// for get current coroutine handle
// usage: auto handle = co_await current{};
/* template<coroutine_type T> */
struct current {
  bool await_ready() const noexcept { return false; }
  // await_suspend返回的是一个coroutine_handle，表示会恢复这个handle，这里直接返回传入的handle，相当于不会暂停
  auto await_suspend(std::coroutine_handle<> handle) noexcept {
    _handle = handle;
    return handle;
  }
  auto await_resume() noexcept { return _handle; }
  std::coroutine_handle<> _handle;
};

// 每个协程都有一个caller，当协程结束的时候，final_suspend返回caller
template <class T> struct task {
  struct promise_type {
    std::suspend_never initial_suspend() { return {}; }
    // 协程对象的返回值应该被使用
    [[nodiscard]] task<T> get_return_object() { return task<T>(this); }
    // 避免协程在子协程中设置了返回值，但是父协程返回的是默认值
    void return_value(T value) {
      if (!_value.has_value())
        _value = value;
    }

    [[noreturn]] static void unhandled_exception() { throw; }
    // 在协程结束的时候，恢复caller
    struct resume_awaiter {
      bool await_ready() const noexcept { return false; }
      auto await_suspend(std::coroutine_handle<> callee) noexcept {
        return _caller;
      }
      void await_resume() noexcept {}
      std::coroutine_handle<> _caller;
      explicit resume_awaiter(std::coroutine_handle<> caller)
          : _caller(caller) {}
      resume_awaiter() = delete;
    };
    resume_awaiter final_suspend() noexcept {
      DEBUG_PRINTF("final_suspend\n");
      return resume_awaiter(_caller);
    }
    // 禁止在其中使用co_await未知的类型
    /* void await_transform() = delete; */
    std::coroutine_handle<> _caller = std::noop_coroutine();
    // 因为协程可能没有返回值，所以用optional来区分没有值的情况
    std::optional<T> _value = std::nullopt;
  };

  using handle = std::coroutine_handle<promise_type>;
  handle _h;

  explicit task(promise_type *p) : _h(handle::from_promise(*p)) {}
  task(task &) = delete;
  task(task &&t) : _h(t._coro) { t._h = nullptr; }
  ~task() {
    DEBUG_PRINTF("destroy\n");
    if (_h)
      _h.destroy();
  }

  // 把当前coroutine的handle传给await_suspend，让子协程结束的时候恢复当前协程
  auto operator co_await() {
    struct awaitable {
      handle _callee;
      // 有可能co_await的时候，协程已经结束了，这种情况下如果co_await等待，将没有人能唤醒它
      bool await_ready() const noexcept { return _callee.done(); }
      /* bool await_ready() const noexcept { return false; } */

      // 这里的callee是当前协程的handle，因为先构造了子协程的对象，然后
      // 调用callee::operator co_await()，然后因为在caller中调用了co_await
      // 所以这里的caller就是父协程的handle
      void await_suspend(std::coroutine_handle<> caller) noexcept {
        _callee.promise()._caller = caller;
        // 因为我们将initial_suspend的返回值设置成了suspend_never，所以我们不需要将控制权转移到callee中
        // 事实上调用这个函数的时候callee已经执行完了
      }

      // 这个是co_await的返回值，也就是子协程的返回值，可以保证await_resume的时候子协程已经结束了，所以返回值一定是有效的
      T await_resume() {
        DEBUG_PRINTF("resume\n");
        return _callee.promise()._value.value();
      }
    };
    return awaitable{_h};
  }

  // 还是不要定义这个函数了，因为协程很可能因为暂停了没有执行完，这样返回值是无效的
  T operator()() = delete;
  /* T operator()() { */
  /*   // _h.resume(); */
  /*   return _h.promise()._value.value(); */
  /* } */
  std::optional<T> get() { return _h.promise()._value; }
};

#endif // _COROUTINE_HPP_
