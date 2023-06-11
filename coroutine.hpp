#pragma once
#ifndef SPDK_INCLUDE_ASYNC_TASK_HPP
#define SPDK_INCLUDE_ASYNC_TASK_HPP

#include <coroutine>
#include <utility>

template <class T> struct get_handle_awaitable {
  using handle_type = std::coroutine_handle<T>;

  bool await_ready() { return false; }

  bool await_suspend(handle_type me) {
    me_ = me;
    return false;
  }

  handle_type await_resume() { return me_; }

private:
  handle_type me_;
};

template <typename R> struct async_task_base {
  using generic_handle = std::coroutine_handle<>;

  struct promise_type_base {
    std::suspend_always initial_suspend() { return {}; }

    void unhandled_exception() {}
  };

  struct final_suspend_awaitable_base {
    bool await_ready() noexcept { return false; }

    void await_resume() noexcept {}
  };
};

template <typename R> struct async_task : public async_task_base<R> {
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;
  using generic_handle = typename async_task_base<R>::generic_handle;

  struct promise_type : public async_task_base<R>::promise_type_base {
    async_task get_return_object() {
      return async_task(handle_type::from_promise(*this));
    }

    template <std::convertible_to<R> From> void return_value(From &&f) {
      return_value_ = std::forward<R>(f);
    }

    auto final_suspend() noexcept;

    generic_handle caller_;
    R return_value_;
    bool task_destroyed_ = false;
  };

  async_task(handle_type handle) : handle_(handle) {}

  ~async_task();

  void operator()();

  bool done() { return handle_.done(); }

  R result() { return std::move(handle_.promise().return_value_); }

  auto operator co_await();

private:
  handle_type handle_;
};

template <typename R> async_task<R>::~async_task() {
  if (handle_.done()) {
    handle_.destroy();
  } else {
    handle_.promise().task_destroyed_ = true;
  }
}

template <typename R> void async_task<R>::operator()() {
  if (handle_ && !handle_.done()) {
    handle_();
  }
}

template <typename R> auto async_task<R>::operator co_await() {
  struct relay_awaitable {
    bool await_ready() { return false; }

    generic_handle await_suspend(handle_type caller) {
      callee_.promise().caller_ = caller;
      return callee_;
    }

    R await_resume() { return std::move(callee_.promise().return_value_); }

    handle_type callee_;
  };

  return relay_awaitable{handle_};
}

template <typename R>
auto async_task<R>::promise_type::final_suspend() noexcept {
  struct resume_caller_if_present_or_direct_return
      : async_task_base<R>::final_suspend_awaitable_base {
    generic_handle await_suspend(handle_type callee) noexcept {
      if (callee.promise().caller_) {
        return callee.promise().caller_;
      }

      if (callee.promise().task_destroyed_) {
        callee.destroy();
      }

      return std::noop_coroutine();
    }
  };

  return resume_caller_if_present_or_direct_return{};
}

template <typename R> struct async_task_endpoint : public async_task_base<R> {
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;
  using caller_handle =
      std::coroutine_handle<typename async_task<R>::promise_type>;
  using generic_handle = typename async_task_base<R>::generic_handle;

  struct async_op_commit {
    bool success_;
    R return_value_;
  };

  struct promise_type : public async_task_base<R>::promise_type_base {
    async_task_endpoint get_return_object() {
      return async_task_endpoint(handle_type::from_promise(*this));
    }

    void return_value(async_op_commit &&cmt) {
      success_ = cmt.success_;
      caller_.promise().return_value_ = std::move(cmt.return_value_);
    }

    auto final_suspend() noexcept;

    caller_handle caller_;
    bool success_;
  };

  async_task_endpoint(handle_type handle) : handle_(handle) {}

  ~async_task_endpoint();

  auto operator co_await();

  bool success() { return handle_.promise().success_; }

  template <std::convertible_to<R> From>
  static async_op_commit success(From &&f) {
    return {true, std::forward<R>(f)};
  }

  template <std::convertible_to<R> From> static async_op_commit fail(From &&f) {
    return {false, std::forward<R>(f)};
  }

private:
  handle_type handle_;
};

template <typename R> async_task_endpoint<R>::~async_task_endpoint() {
  if (!handle_.done()) {
    // TODO: error log
  }

  handle_.destroy();
}

template <typename R> auto async_task_endpoint<R>::operator co_await() {
  struct relay_awaitable {
    bool await_ready() { return false; }

    generic_handle await_suspend(typename async_task<R>::handle_type caller) {
      callee_.promise().caller_ = caller;
      return callee_;
    }

    R await_resume() {
      return std::move(callee_.promise().caller_.promise().return_value_);
    }

    handle_type callee_;
  };

  return relay_awaitable{handle_};
}

template <typename R>
auto async_task_endpoint<R>::promise_type::final_suspend() noexcept {
  struct suspend_or_resume_caller
      : async_task_base<R>::final_suspend_awaitable_base {
    generic_handle await_suspend(handle_type callee) noexcept {
      generic_handle ret;

      /*
        Ternary operator complains their types mimatched...
        It seems ternary operator cannot recognize they have
        generic_handle as common base class.
      */
      if (callee.promise().success_) {
        ret = std::noop_coroutine();
      } else {
        ret = callee.promise().caller_;
      }

      return ret;
    }
  };

  return suspend_or_resume_caller{};
}

#endif
