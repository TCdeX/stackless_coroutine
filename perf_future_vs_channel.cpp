
#define BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
#define BOOST_THREAD_PROVIDES_EXECUTORS
#define BOOST_THREAD_PROVIDES_FUTURE_WHEN_ALL_WHEN_ANY
#include <boost/thread/executor.hpp>
#include <boost/thread/executors/loop_executor.hpp>
#include <boost/thread/future.hpp>
#ifdef _MSC_VER
#include <experimental/coroutine>
#endif
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

auto test_future(int count) {
  boost::executors::loop_executor loop;

  for (int i = 0; i < count; ++i) {
    boost::promise<int> p;
    auto fut = p.get_future();
    bool done = false;
    fut.then(loop, [&](auto &&) { done = true; });
    p.set_value(i);
    while (!done) {
      loop.run_queued_closures();
    }
  }
}
auto test_callback(int count) {
  boost::executors::loop_executor loop;

  for (int i = 0; i < count; ++i) {
    loop.submit([]() {});
    loop.run_queued_closures();
  }
}

#include "channel.hpp"
#include "stackless_coroutine.hpp"

#ifdef _MSC_VER
goroutine writer(std::shared_ptr<channel<int>> chan, int count) {
  await_channel_writer<int> writer{chan};
  for (int i = 0; i < count; ++i) {
    co_await writer.write(i);
  }
  chan->close();
}
goroutine reader(std::shared_ptr<channel<int>> chan, std::atomic<int> &f) {
  await_channel_reader<int> reader{chan};
  for (;;) {
    auto p = co_await reader.read();
    if (p.first == false)
      break;
  }
  f = 1;
}

auto test_channel(int count) {
  std::atomic<int> f;
  f = 0;

  auto chan = std::make_shared<channel<int>>();

  writer(chan, count);
  reader(chan, f);
  while (f == 0)
    ;
}

#endif

auto make_reader(std::shared_ptr<channel<int>> chan, std::atomic<int> &f) {
  struct values {
    channel_reader<int> reader;
    std::atomic<int> *pf;

    values(std::shared_ptr<channel<int>> c, std::atomic<int> &f)
        : reader{c}, pf{&f} {}
  };
  auto co = stackless_coroutine::make_coroutine<values>(
      stackless_coroutine::make_block(stackless_coroutine::make_while_true(
          [](auto &context, values &variables) {
            variables.reader.read(context);
            return context.do_async();

          },
          [](auto &context, values &variables, auto channel, int value,
             bool closed) {
            if (closed) {
              *variables.pf = 1;
              return context.do_break();
            }
            return context.do_continue();

          }

          )

                                          ),
      [](auto &&...) {}, chan, f);

  return co;
}

auto make_writer(std::shared_ptr<channel<int>> chan, int count) {
  struct values {
    channel_writer<int> writer;
    int count;
    int i;
    values(std::shared_ptr<channel<int>> chan, int c)
        : writer{chan}, count{c}, i{0} {}
  };
  auto co = stackless_coroutine::make_coroutine<values>(
      stackless_coroutine::make_block(stackless_coroutine::make_while_true(
          [](auto &context, values &variables) {
            if (variables.i >= variables.count) {
              variables.writer.close();
              return context.do_async_return();
            }
            variables.writer.write(variables.i, context);
            ++variables.i;
            return context.do_async();

          },
          [](auto &context, values &variables, auto channel, bool closed) {}

          )

                                          ),
      [](auto &&...) {}, chan, count);
  return co;
}

auto test_channel_stackless_library(int count) {
  std::atomic<int> f;
  f = 0;

  auto chan = std::make_shared<channel<int>>();

  auto w = make_writer(chan, count);
  auto r = make_reader(chan, f);
  w();
  r();
  while (f == 0)
    ;
}

#ifdef _MSC_VER
goroutine writer_select(std::shared_ptr<channel<int>> chan1,
                        std::shared_ptr<channel<int>> chan2, int count) {
  await_channel_writer<int> writer1{chan1};
  await_channel_writer<int> writer2{chan2};
  for (int i = 0; i < count; ++i) {
    if (i % 2) {
      co_await writer1.write(i);
    } else {

      co_await writer2.write(i);
    }
  }
  chan1->close();
  chan2->close();
}
goroutine reader_select(std::shared_ptr<channel<int>> chan1,
                        std::shared_ptr<channel<int>> chan2,
                        std::atomic<int> &f) {
  await_channel_reader<int> reader1{chan1};
  await_channel_reader<int> reader2{chan2};
  for (;;) {
    auto p = co_await select(reader1, [](auto) {}, reader2, [](auto) {});
    if (p.first == false)
      break;
  }
  f = 1;
}

auto test_channel_select(int count) {
  std::atomic<int> f;
  f = 0;

  auto chan1 = std::make_shared<channel<int>>();
  auto chan2 = std::make_shared<channel<int>>();

  writer_select(chan1, chan2, count);
  reader_select(chan1, chan2, f);
  while (f == 0)
    ;
}
#endif

auto test_future_select(int count) {
  boost::executors::loop_executor loop;
  boost::promise<int> p1;
  boost::promise<int> p2;
  auto fut1 = p1.get_future();
  auto fut2 = p2.get_future();

  for (int i = 0; i < count; ++i) {
    auto selected = boost::when_any(std::move(fut1), std::move(fut2));
    bool done = false;
    selected.then(loop, [&](auto &&f) {
      auto p = f.get();
      if (std::get<0>(p).is_ready()) {
        p1 = boost::promise<int>{};
        fut1 = p1.get_future();
        fut2 = std::move(std::get<1>(p));
      } else {
        p2 = boost::promise<int>{};
        fut2 = p2.get_future();
        fut1 = std::move(std::get<0>(p));
      }
      done = true;
    });
    if (i % 2) {
      p1.set_value(i);
    } else {
      p2.set_value(i);
    }
    while (!done) {
      loop.run_queued_closures();
    }
  }
}

auto make_reader_select(std::shared_ptr<channel<int>> chan1,
                        std::shared_ptr<channel<int>> chan2,
                        std::atomic<int> &f) {
  struct values {
    channel_reader<int> reader1;
    channel_reader<int> reader2;
    channel_selector selector;
    std::mutex mut;
    std::atomic<int> *pf;

    values(std::shared_ptr<channel<int>> c1, std::shared_ptr<channel<int>> c2,
           std::atomic<int> &f)
        : reader1{c1}, reader2{c2}, pf{&f} {}
  };
  auto co = stackless_coroutine::make_coroutine<values>(
      stackless_coroutine::make_block(stackless_coroutine::make_while_true(
          [](auto &context, values &variables) {
            std::unique_lock<std::mutex> lock{variables.mut};
            variables.reader1.read(variables.selector, context);
            variables.reader2.read(variables.selector, context);
            return context.do_async();

          },
          [](auto &context, values &variables, auto... v) {

            std::unique_lock<std::mutex> lock{variables.mut};
            lock.unlock();
            auto sel = get_selector(v...);
            sel.select(variables.reader1, [](auto &&) {})
                .select(variables.reader2, [](auto &&) {});

            if (!sel) {
              *variables.pf = 1;
              return context.do_break();
            }

            return context.do_continue();

          }

          )

                                          ),
      [](auto &&...) {}, chan1, chan2, f);

  return co;
}

auto make_writer_select(std::shared_ptr<channel<int>> chan1,
                        std::shared_ptr<channel<int>> chan2, int count) {
  struct values {
    channel_writer<int> writer1;
    channel_writer<int> writer2;
    int count;
    int i;
    values(std::shared_ptr<channel<int>> chan1,
           std::shared_ptr<channel<int>> chan2, int c)
        : writer1{chan1}, writer2{chan2}, count{c}, i{0} {}
  };
  auto co = stackless_coroutine::make_coroutine<values>(
      stackless_coroutine::make_block(stackless_coroutine::make_while_true(
          [](auto &context, values &variables) {
            if (variables.i >= variables.count) {
              variables.writer1.close();
              variables.writer2.close();
              return context.do_async_return();
            }
            if (variables.i % 2) {

              variables.writer1.write(variables.i, context);
            } else {

              variables.writer2.write(variables.i, context);
            }
            ++variables.i;
            return context.do_async();

          },
          [](auto &context, values &variables, auto channel, bool closed) {}

          )

                                          ),
      [](auto &&...) {}, chan1, chan2, count);
  return co;
}

auto test_channel_stackless_library_select(int count) {
  std::atomic<int> f;
  f = 0;

  auto chan = std::make_shared<channel<int>>();

  auto w = make_writer(chan, count);
  auto r = make_reader(chan, f);
  w();
  r();
  while (f == 0)
    ;
}


#include <boost/fiber/all.hpp>


void fiber_writer(boost::fibers::bounded_channel<int>& chan, int count) {
	for (int i = 0; i < count; ++i) {
		chan.push(i);
	}
	chan.close();
}

void fiber_reader(boost::fibers::bounded_channel<int> &chan) {
  for (;;) {
    int i;
    auto v = chan.pop(i);
    if (v == boost::fibers::channel_op_status::closed) {
      return;
    }
  }
}

void test_fiber(int count) {
	boost::fibers::bounded_channel<int> chan{1};
	boost::fibers::fiber rf{ [&]() {fiber_reader(chan);} };
	boost::fibers::fiber wf{ [&]() {fiber_writer(chan,count);} };

	rf.join();
	wf.join();


}

struct fiber_suspender {
  std::mutex mut;
  boost::fibers::mutex internal_mut;
  boost::fibers::condition_variable cvar;
  std::atomic<bool> suspended{false};

  void suspend() {
    std::unique_lock<boost::fibers::mutex> lock{internal_mut};
    suspended = true;
    while (suspended) {
      cvar.wait(lock);
    }
  }
  void resume() {

	while (!suspended);
    std::unique_lock<boost::fibers::mutex> lock{internal_mut};
    suspended = false;
    cvar.notify_all();
  }
};


void fiber_writer(std::shared_ptr<channel<int>>& chan, int count) {
	fiber_suspender s;
	sync_channel_writer<int, fiber_suspender> writer{ chan,s };


	for (int i = 0; i < count; ++i) {
		writer.write(i);
	}
	chan->close();
}

void fiber_reader(std::shared_ptr<channel<int>> &chan) {
	fiber_suspender s;
	sync_channel_reader<int, fiber_suspender> reader{ chan,s };


	for (;;) {
		auto res = reader.read();
		if (res.first == false) return;
	}
}

void test_fiber_channel(int count) {

  auto chan = std::make_shared<channel<int>>();
	boost::fibers::fiber rf{ [&]() {fiber_reader(chan);} };
	boost::fibers::fiber wf{ [&]() {fiber_writer(chan,count);} };

	rf.join();
	wf.join();


}


void thread_writer(std::shared_ptr<channel<int>>& chan, int count) {
	thread_suspender s;
	sync_channel_writer<int, thread_suspender> writer{ chan,s };


	for (int i = 0; i < count; ++i) {
		writer.write(i);
	}
	chan->close();
}

void thread_reader(std::shared_ptr<channel<int>> &chan) {
	thread_suspender s;
	sync_channel_reader<int, thread_suspender> reader{ chan,s };


	for (;;) {
		auto res = reader.read();
		if (res.first == false) return;
	}
}

void test_thread_channel(int count) {

  auto chan = std::make_shared<channel<int>>();
	std::thread rt{ [&]() {thread_reader(chan);} };
	std::thread wt{ [&]() {thread_writer(chan,count);} };

	rt.join();
	wt.join();


}





int main() {

  auto count = 1000'000;
  {
    auto start = std::chrono::steady_clock::now();
    test_future(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "Did " << count << " iterations for future in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }
  {
    auto start = std::chrono::steady_clock::now();
    test_callback(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "Did " << count << " iterations for callback in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }

#ifdef _MSC_VER
  {

    auto start = std::chrono::steady_clock::now();
    test_channel(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "Did " << count << " channel iterations in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }
#endif
  {

    auto start = std::chrono::steady_clock::now();
    test_channel_stackless_library(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "did " << count << " channel stackless library iterations in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }
  {

    auto start = std::chrono::steady_clock::now();
    test_fiber(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "did " << count << " fiber iterations in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }

  {

    auto start = std::chrono::steady_clock::now();
    test_fiber_channel(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "did " << count << " fiber channel iterations in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }


  {

	  auto start = std::chrono::steady_clock::now();
	  test_thread_channel(count);
	  auto end = std::chrono::steady_clock::now();
	  std::cout << "did " << count << " thread channel iterations in "
		  << std::chrono::duration_cast<std::chrono::duration<double>>(
			  end - start)
		  .count()
		  << "\n";
  }


#ifdef _MSC_VER
  {

    auto start = std::chrono::steady_clock::now();
    test_channel_select(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "Did " << count << " channel_select iterations in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }

#endif
  {
	  auto outer_count = count;
	  auto count = outer_count / 100;
    auto start = std::chrono::steady_clock::now();
    test_future_select(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "Did " << count << " future_select iterations in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }
  {

    auto start = std::chrono::steady_clock::now();
    test_channel_stackless_library_select(count);
    auto end = std::chrono::steady_clock::now();
    std::cout << "Did " << count << " channel stackless library iterations in "
              << std::chrono::duration_cast<std::chrono::duration<double>>(
                     end - start)
                     .count()
              << "\n";
  }
}
