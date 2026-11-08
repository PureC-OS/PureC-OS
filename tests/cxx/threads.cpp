#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <vector>

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);           \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static thread_local int tls_counter = 10;
static std::atomic<int> destroyed{0};
struct Destroyer { ~Destroyer() { destroyed++; } };
static thread_local Destroyer tls_destroyer;

int main() {
    std::mutex lock;
    long counter = 0;
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; i++)
        workers.emplace_back([&] {
            for (int n = 0; n < 5000; n++) { std::lock_guard<std::mutex> guard(lock); counter++; }
        });
    for (auto &worker : workers) worker.join();
    CHECK(counter == 20000);

    std::atomic<int> atomic_counter{0};
    std::vector<std::thread> atomics;
    for (int i = 0; i < 4; i++)
        atomics.emplace_back([&] { for (int n = 0; n < 10000; n++) atomic_counter++; });
    for (auto &worker : atomics) worker.join();
    CHECK(atomic_counter == 40000);

    std::mutex cv_lock;
    std::condition_variable cv;
    bool ready = false;
    int observed = 0;
    std::thread waiter([&] {
        std::unique_lock<std::mutex> hold(cv_lock);
        cv.wait(hold, [&] { return ready; });
        observed = 99;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    { std::lock_guard<std::mutex> hold(cv_lock); ready = true; }
    cv.notify_all();
    waiter.join();
    CHECK(observed == 99);

    std::mutex timed_lock;
    std::condition_variable timed_cv;
    std::unique_lock<std::mutex> hold(timed_lock);
    auto start = std::chrono::steady_clock::now();
    bool woke = timed_cv.wait_for(hold, std::chrono::milliseconds(80), [] { return false; });
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start).count();
    hold.unlock();
    CHECK(!woke && elapsed >= 60 && elapsed < 2000);

    std::future<int> answer = std::async(std::launch::async, [] { return 6 * 7; });
    CHECK(answer.get() == 42);

    std::promise<std::string> promise;
    std::future<std::string> future = promise.get_future();
    std::thread producer([&] { promise.set_value("from thread"); });
    CHECK(future.get() == "from thread");
    producer.join();

    std::future<int> failing = std::async(std::launch::async, []() -> int { throw std::runtime_error("boom"); });
    try { failing.get(); CHECK(false); } catch (const std::runtime_error &error) { CHECK(std::string(error.what()) == "boom"); }

    int tls_in_thread = 0;
    std::thread local_thread([&] { tls_counter += 5; tls_in_thread = tls_counter; (void)&tls_destroyer; });
    local_thread.join();
    CHECK(tls_in_thread == 15 && tls_counter == 10);
    CHECK(destroyed == 1);

    std::shared_mutex rw;
    int shared_value = 0;
    std::vector<std::thread> rw_threads;
    for (int i = 0; i < 3; i++)
        rw_threads.emplace_back([&] { std::shared_lock<std::shared_mutex> read(rw); (void)shared_value; });
    rw_threads.emplace_back([&] { std::unique_lock<std::shared_mutex> write(rw); shared_value = 5; });
    for (auto &worker : rw_threads) worker.join();
    CHECK(shared_value == 5);

    std::once_flag flag;
    int once_runs = 0;
    std::vector<std::thread> once_threads;
    for (int i = 0; i < 4; i++) once_threads.emplace_back([&] { std::call_once(flag, [&] { once_runs++; }); });
    for (auto &worker : once_threads) worker.join();
    CHECK(once_runs == 1);

    CHECK(std::thread::hardware_concurrency() >= 1);
    std::thread::id main_id = std::this_thread::get_id();
    std::thread::id other_id;
    std::thread id_thread([&] { other_id = std::this_thread::get_id(); });
    id_thread.join();
    CHECK(main_id != other_id);

    std::printf("cxx threads done failures=%d\n", failures);
    return failures ? 1 : 0;
}
