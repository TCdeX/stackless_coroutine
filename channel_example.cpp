#include "channel.hpp"
#include <array>
#include <future>
#include <iostream>
#include <chrono>

using array_type = std::array<char, 32 * 1024 * 1024>;
using payload_type = std::unique_ptr<array_type>;
using ordered_payload_type = std::pair<int, payload_type>;

goroutine reader(std::shared_ptr<channel<ordered_payload_type>> chan,
                 std::shared_ptr<channel<payload_type>> pool, int count) {
  await_channel_writer<ordered_payload_type> writer{chan};
  await_channel_reader<payload_type> pool_reader{pool};
  for (int i = 0; i < count; ++i) {
    auto res = co_await pool_reader.read();
	// Read data into res.second
    co_await writer.write({i, std::move(res.second)});
  }
  chan->close();
}
goroutine processor (std::shared_ptr<channel<ordered_payload_type>> inchan,
	std::shared_ptr<channel<ordered_payload_type>> outchan) {
	await_channel_reader<ordered_payload_type> reader{ inchan };
	await_channel_writer<ordered_payload_type> writer{ outchan };
	auto out_array = std::make_unique<array_type>();
	for (;;) {
		auto res = co_await reader.read();
		if (res.first == false) {
			outchan->close();
			return;
		}
		// Do something with res.second.second and out_array
		std::this_thread::sleep_for(std::chrono::seconds{ 1 });
		co_await writer.write({ res.second.first, std::move(out_array) });
		out_array = std::move(res.second.second);
	}
};

auto make_processor(std::shared_ptr<channel<ordered_payload_type>> inchan) {
  auto outchan = std::make_shared<channel<ordered_payload_type>>();
  processor(inchan, outchan);
  return outchan;
}

goroutine writer(std::vector<std::shared_ptr<channel<ordered_payload_type>>> inchans,
                 std::shared_ptr<channel<payload_type>> pool,
                 std::shared_ptr<channel<bool>> done_chan) {
  await_channel_writer<bool> done_writer{done_chan};
  std::vector<await_channel_reader<ordered_payload_type>> readers{inchans.begin(),
                                                       inchans.end()};

  await_channel_writer<payload_type> pool_writer{pool};

  std::vector<ordered_payload_type> buffer;
  buffer.reserve(readers.size());
  int current = 0;
  for (;;) {
    if (readers.empty() && buffer.empty()) {
      co_await done_writer.write(true);
      return;
    }
    auto res = co_await select_range(readers, [&](auto &i) {
      buffer.push_back(std::move(i));
      std::push_heap(buffer.begin(), buffer.end(),
                     [](auto &a, auto &b) { return a.first > b.first; });
    });
    if (res.first == false) {
      readers.erase(res.second);
    }

    while (!buffer.empty() && buffer.front().first == current) {
	  // Write data in buffer.front().second to output
      std::cout << buffer.front().first << "\n";
      ++current;
      std::pop_heap(buffer.begin(), buffer.end(),
                    [](auto &a, auto &b) { return a.first > b.first; });
      co_await pool_writer.write(std::move(buffer.back().second));
      buffer.pop_back();
    }
  }
}

int main() {
  int threads;
  int count;
  std::cout << "\nEnter count\n";
  std::cin >> count;
  std::cout << "\nEnter threads\n";
  std::cin >> threads;

  std::vector<channel_runner> runners(threads);

  auto start = std::chrono::steady_clock::now();

  auto inchan = std::make_shared<channel<ordered_payload_type>>();
  auto pool = std::make_shared<channel<payload_type>>(threads);
  auto done_chan = std::make_shared<channel<bool>>();

  reader(inchan, pool, count);

  std::vector<std::shared_ptr<channel<ordered_payload_type>>> ordered_payload_chans;
  for (int i = 0; i < threads; ++i) {
    ordered_payload_chans.push_back(make_processor(inchan));
  }

  writer(std::move(ordered_payload_chans), pool, done_chan);

  thread_suspender sus;
  sync_channel_reader<bool, thread_suspender> done_reader{done_chan, sus};
  sync_channel_writer<payload_type, thread_suspender> pool_writer{pool, sus};

  for (int i = 0; i < threads; ++i) {
    payload_type payload =
        std::make_unique<array_type>();
    pool_writer.write(std::move(payload));
  }

  done_reader.read();
  auto end = std::chrono::steady_clock::now();

  std::cout << "Took "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << " ms\n";
}