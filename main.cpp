#include "ThreadPool.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <string>
#include <type_traits>


// 每个测试返回 bool：失败时 main 返回 1，方便构建工具判断结果。
bool TestEmptyPool() {
    {
        ThreadPool pool(3);
        // 没有任务，离开作用域时也应该能唤醒并回收所有线程。
    }
    std::cout << "[PASS] 空线程池正常析构\n";
    return true;
}

bool TestInvalidArguments(int count, std::size_t capacity, const char* message) {
    bool passed = false;
    try {
        ThreadPool pool(count, capacity);
    } catch (const std::invalid_argument& error) {
        passed = std::string(error.what()) == message;
    } catch (...) {}
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << "构造参数 (" << count << ", " << capacity
              << ") 异常类型及消息校验\n";
    return passed;
}

bool TestInvalidCount(int count) {
    return TestInvalidArguments(count, 100,
        "threadpoolCount must be greater than 0");
}

bool TestTaskCompletion(int threadCount) {
    const int taskCount = 20;
    // 这些变量先构造、后销毁，保证所有任务执行时它们仍然有效。
    std::vector<int> executionCounts(taskCount, 0);
    std::mutex resultMutex;
    bool allAccepted = true;
    {
        ThreadPool pool(threadCount);
        for (int i = 0; i < taskCount; ++i) {
            auto accepted = pool.Submit([i, &executionCounts, &resultMutex]() {
                std::lock_guard<std::mutex> lock(resultMutex);
                ++executionCounts[i];
            });
            if (!accepted.valid()) {
                allAccepted = false;
            }
        }
        // 不 sleep：离开作用域，验证析构会等已提交任务完成。
    }

    bool passed = allAccepted;
    for (int count : executionCounts) {
        if (count != 1) {
            passed = false;
        }
    }
    // 此时 worker 都已 join，主线程可以直接读取结果。
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << threadCount << " 个 worker：20 个任务各执行一次\n";
    return passed;
}


bool Report(bool passed, const char* name) {
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
    return passed;
}

bool TestVoidFuture() {
    int count = 0;
    ThreadPool pool(1);
    auto result = pool.Submit([&count] { ++count; });
    static_assert(std::is_same_v<decltype(result), std::future<void>>);
    const bool valid = result.valid();
    result.get();
    std::function<void()> callable = [&count] { ++count; };
    auto wrapped = pool.Submit(callable);
    static_assert(std::is_same_v<decltype(wrapped), std::future<void>>);
    wrapped.get();
    return Report(valid && !result.valid() && count == 2,
                  "void lambda 和 std::function 均返回 future<void>，get 等待完成");
}

bool TestTemplateResults() {
    ThreadPool pool(3);
    auto integer = pool.Submit([] { return 42; });
    auto decimal = pool.Submit([] { return 3.25; });
    auto text = pool.Submit([] { return std::string("thread pool"); });
    static_assert(std::is_same_v<decltype(integer), std::future<int>>);
    static_assert(std::is_same_v<decltype(decimal), std::future<double>>);
    static_assert(std::is_same_v<decltype(text), std::future<std::string>>);
    const bool intOk = integer.get() == 42;
    const bool doubleOk = decimal.get() == 3.25;
    const bool stringOk = text.get() == "thread pool";
    return Report(intOk && doubleOk && stringOk,
                  "Submit 正确返回 int、double、string 及对应 future 类型");
}

bool TestTemplateMixedSubmissions() {
    std::atomic<int> voidCount{0};
    std::vector<std::future<void>> voidResults;
    std::vector<std::future<int>> results;
    bool passed = true;
    {
        ThreadPool pool(3);
        for (int i = 0; i < 50; ++i) {
            voidResults.push_back(pool.Submit([&voidCount] { ++voidCount; }));
            results.push_back(pool.Submit([i] { return i * 2; }));
        }
        // 析构排空队列后，再读取任务结果。
    }
    for (int i = 0; i < 50; ++i) {
        if (results[i].get() != i * 2) passed = false;
    }
    for (auto& result : voidResults) result.get();
    return Report(passed && voidCount == 50,
                  "void 与 int 任务混合执行，析构后 future 可取");
}

bool TestStoppedTemplateSubmission() {
    bool passed = true;
    bool executed = false;
    for (int count : {1, 3}) {
        // 用显式关闭构造仍存活的停止池。
        ThreadPool pool(count);
        pool.Shutdown();
        for (int i = 0; i < 2; ++i) {
            try {
                pool.Submit([&executed] { executed = true; return 1; });
                passed = false;
            } catch (const std::runtime_error& error) {
                if (std::string(error.what()) != "ThreadPool has stopped") passed = false;
            } catch (...) {
                passed = false;
            }
        }
    }
    return Report(passed && !executed,
                  "停止池重复 Submit 抛异常且不执行任务");
}

bool TestTemplateTaskException() {
    ThreadPool pool(1);
    auto failed = pool.Submit([]() -> std::string {
        throw std::runtime_error("template task failed");
    });
    auto next = pool.Submit([] { return 99; });
    bool caught = false;
    try {
        failed.get();
    } catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "template task failed";
    }
    const bool continued = next.get() == 99;
    return Report(caught && continued,
                  "Submit 异常由 future 传递，worker 继续执行");
}


int add(int a, int b) { return a + b; }

bool TestParameterizedFunction() {
    ThreadPool pool(2);
    auto result = pool.Submit(add, 10, 20);
    static_assert(std::is_same_v<decltype(result), std::future<int>>);
    return Report(result.get() == 30, "普通函数 add 接收两个参数");
}

bool TestParameterizedLambda() {
    ThreadPool pool(2);
    auto result = pool.Submit([](int a, double b, int c) {
        return a * b + c;
    }, 4, 2.5, 3);
    static_assert(std::is_same_v<decltype(result), std::future<double>>);
    return Report(result.get() == 13.0, "lambda 接收多个不同类型参数");
}

bool TestParameterizedStrings() {
    ThreadPool pool(2);
    std::string prefix = "hello";
    auto result = pool.Submit([](std::string a, std::string b) {
        return a + " " + b;
    }, prefix, std::string("pool"));
    static_assert(std::is_same_v<decltype(result), std::future<std::string>>);
    const auto value = result.get();
    return Report(value == "hello pool" && prefix == "hello",
                  "string 返回值，左值与右值参数");
}

bool TestBoundReference() {
    int value = 10;
    ThreadPool pool(1);
    auto increment = [](int& n) { return ++n; };
    auto copied = pool.Submit(increment, value);
    const bool copyOk = copied.get() == 11 && value == 10;
    auto referenced = pool.Submit(increment, std::ref(value));
    const bool refOk = referenced.get() == 11 && value == 11;
    return Report(copyOk && refOk, "bind 默认拷贝左值，std::ref 修改原值");
}

struct LvalueCallable {
    std::shared_ptr<int> copies = std::make_shared<int>(0);
    LvalueCallable() = default;
    LvalueCallable(const LvalueCallable& other) : copies(other.copies) { ++*copies; }
    LvalueCallable(LvalueCallable&&) = default;
    int operator()(int n) { return n + 1; }
};

bool TestMoveOnlyBinding() {
    ThreadPool pool(1);
    auto callable = [owned = std::make_unique<int>(40)](int n) {
        return *owned + n;
    };
    LvalueCallable lvalueCallable;
    auto lvalue = pool.Submit(lvalueCallable, 8);
    auto result = pool.Submit(std::move(callable), 2);
    auto argument = pool.Submit([](const std::unique_ptr<int>& n) {
        return *n;
    }, std::make_unique<int>(7));
    const bool callableOk = result.get() == 42;
    const bool argumentOk = argument.get() == 7;
    return Report(callableOk && argumentOk && lvalue.get() == 9 && *lvalueCallable.copies == 1,
                  "左值 callable、仅可移动 callable 与绑定参数");
}

bool TestParameterizedMixed() {
    std::atomic<int> count{0};
    std::vector<std::future<int>> results;
    std::future<void> voidResult;
    bool accepted = true;
    {
        ThreadPool pool(3);
        for (int i = 0; i < 50; ++i) {
            if (!pool.Submit([&count] { ++count; }).valid()) accepted = false;
            results.push_back(pool.Submit(add, i, 10));
        }
        voidResult = pool.Submit([](std::atomic<int>& n, int amount) {
            n.fetch_add(amount);
        }, std::ref(count), 5);
    }
    voidResult.get();
    for (int i = 0; i < 50; ++i) {
        if (results[i].get() != i + 10) accepted = false;
    }
    return Report(accepted && count == 55,
                  "带参数任务与 Submit 混合执行，含 future<void> 和析构排空");
}

bool TestStoppedParameterized() {
    bool passed = true;
    bool executed = false;
    for (int count : {1, 3}) {
        ThreadPool pool(count);
        pool.Shutdown();
        for (int i = 0; i < 2; ++i) {
            try {
                pool.Submit([&executed](int n) { executed = true; return n; }, i);
                passed = false;
            } catch (const std::runtime_error& error) {
                if (std::string(error.what()) != "ThreadPool has stopped") passed = false;
            } catch (...) { passed = false; }
        }
    }
    return Report(passed && !executed, "停止状态重复提交带参数任务抛异常");
}

bool TestParameterizedException() {
    ThreadPool pool(1);
    auto failed = pool.Submit([](std::string message) -> int {
        throw std::runtime_error(message);
    }, std::string("parameterized failure"));
    auto next = pool.Submit(add, 2, 3);
    bool caught = false;
    try { failed.get(); }
    catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "parameterized failure";
    }
    const bool continued = next.get() == 5;
    return Report(caught && continued, "带参数任务异常经 future 传递，worker 继续执行");
}

// 用 promise 控制 worker，而不是用 sleep 猜测队列状态。
bool TestTaskCount() {
    std::promise<void> started;
    std::promise<void> release;
    auto startedFuture = started.get_future();
    auto gate = release.get_future().share();
    std::atomic<int> completed{0};
    bool passed = true;
    {
        ThreadPool pool(1);
        const ThreadPool& view = pool;
        passed = view.GetTaskCount() == 0;
        auto active = pool.Submit([&started, gate] {
            started.set_value();
            gate.wait();
        });
        startedFuture.wait();
        // 唯一 worker 正在执行任务，但队列为空。
        passed = (view.GetTaskCount() == 0) && passed;
        passed = (active.wait_for(std::chrono::milliseconds(0))
                  == std::future_status::timeout) && passed;
        for (int i = 0; i < 5; ++i) {
            passed = pool.Submit([&completed] { ++completed; }).valid() && passed;
        }
        auto queued = pool.Submit([] { return 42; });
        passed = (view.GetTaskCount() == 6) && passed;
        // 无论检查成功与否，都先放行 worker，避免析构一直等待。
        release.set_value();
        active.get();
        const bool resultOk = queued.get() == 42;
        passed = resultOk && (completed == 5) && passed;
        passed = (view.GetTaskCount() == 0) && passed;
    }
    return Report(passed, "GetTaskCount：空池 0、执行中 0、排队 6、完成后 0（含 const 调用）");
}

bool TestStoppedTaskCount() {
    bool passed = true;
    for (int count : {1, 3}) {
        ThreadPool pool(count);
        pool.Shutdown();
        passed = (pool.GetTaskCount() == 0 && pool.GetActiveCount() == 0) && passed;
        try {
            pool.Submit([] {});
            passed = false;
        } catch (const std::runtime_error& error) {
            passed = (std::string(error.what()) == "ThreadPool has stopped") && passed;
        } catch (...) { passed = false; }
        passed = (pool.GetTaskCount() == 0) && passed;
    }
    return Report(passed, "停止池拒绝任务后 GetTaskCount 仍为 0");
}

// future 就绪发生在 worker 执行 activeCount-- 之前，因此另外等待计数归零。
// 超时用于报告失败；任务是否已经开始由 promise 确认，不靠 sleep 猜测。
bool WaitForIdleCount(const ThreadPool& pool) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (pool.GetActiveCount() != 0) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

bool TestActiveCount(int workerCount) {
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::vector<std::promise<void>> started(workerCount);
    std::vector<std::future<void>> entered;
    for (auto& signal : started) entered.push_back(signal.get_future());
    std::atomic<int> completed{0};
    ThreadPool pool(workerCount);
    const ThreadPool& view = pool;
    bool passed = view.GetActiveCount() == 0 && view.GetTaskCount() == 0;
    passed = view.IsRunning() &&
        view.GetWorkerCount() == static_cast<std::size_t>(workerCount) && passed;
    std::vector<std::future<void>> results;
    for (int i = 0; i < workerCount; ++i) {
        auto task = [&, i, gate] {
            started[i].set_value();
            gate.wait();
            ++completed;
        };
        results.push_back(pool.Submit(task));
    }
    for (auto& signal : entered) {
        passed = (signal.wait_for(std::chrono::seconds(5))
                  == std::future_status::ready) && passed;
    }
    passed = (view.GetActiveCount() == static_cast<std::size_t>(workerCount)) && passed;
    passed = (view.GetTaskCount() == 0) && passed;
    for (int i = 0; i < 6; ++i) {
        passed = pool.Submit([&completed] { ++completed; }).valid() && passed;
    }
    passed = (view.GetTaskCount() == 6) && passed;
    passed = (view.GetActiveCount() == static_cast<std::size_t>(workerCount)) && passed;
    passed = view.IsRunning() &&
        view.GetWorkerCount() == static_cast<std::size_t>(workerCount) && passed;
    release.set_value();
    for (auto& result : results) result.get();
    // 完成数确认所有普通任务已进入任务体末尾，再检查 worker 的计数收尾。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (completed != workerCount + 6 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    passed = (completed == workerCount + 6) && passed;
    passed = WaitForIdleCount(view) && passed;
    passed = (view.GetTaskCount() == 0) && passed;
    return Report(passed, workerCount == 1
        ? "单 worker：active 0→1→0，queued 0→6→0（含 const 调用）"
        : "3 个 worker 同时执行：active 为 3，queued 为 6，结束后均归零");
}

bool TestVoidTaskException(bool unknownException) {
    std::promise<void> started;
    auto entered = started.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::promise<void> nextDone;
    auto nextResult = nextDone.get_future();
    ThreadPool pool(1);
    auto failed = pool.Submit([&started, gate, unknownException] {
        started.set_value();
        gate.wait();
        if (unknownException) throw 7;
        throw std::runtime_error("expected Submit exception");
    });
    bool passed = (entered.wait_for(std::chrono::seconds(5))
              == std::future_status::ready);
    passed = (pool.GetActiveCount() == 1) && passed;
    auto next = pool.Submit([&nextDone] { nextDone.set_value(); });
    auto futureResult = pool.Submit([](int x) { return x + 1; }, 41);
    release.set_value();
    passed = (nextResult.wait_for(std::chrono::seconds(5))
              == std::future_status::ready) && passed;
    const bool ready = futureResult.wait_for(std::chrono::seconds(5))
        == std::future_status::ready;
    passed = ready && passed;
    if (ready) passed = (futureResult.get() == 42) && passed;
    bool caught = false;
    try { failed.get(); }
    catch (const std::runtime_error& error) {
        caught = !unknownException && std::string(error.what()) == "expected Submit exception";
    }
    catch (int error) { caught = unknownException && error == 7; }
    catch (...) {}
    next.get();
    passed = caught && passed;
    passed = WaitForIdleCount(pool) && passed;
    passed = (pool.GetTaskCount() == 0) && passed;
    return Report(passed, unknownException
        ? "void 未知异常由 get() 传递，后续 Submit/future 正常，active 归零"
        : "void runtime_error 由 get() 传递，后续 Submit/future 正常，active 归零");
}

bool TestRunningStatus() {
    bool passed = true;
    for (int count : {1, 3}) {
        ThreadPool pool(count);
        const ThreadPool& view = pool;
        passed = (view.IsRunning() &&
                  view.GetWorkerCount() == static_cast<std::size_t>(count)) && passed;
        auto result = pool.Submit([&view, count] {
            return view.IsRunning() &&
                view.GetWorkerCount() == static_cast<std::size_t>(count);
        });
        const bool taskStatus = result.get();
        passed = taskStatus && WaitForIdleCount(view) && passed;
        passed = (view.IsRunning() && view.GetTaskCount() == 0 &&
                  view.GetWorkerCount() == static_cast<std::size_t>(count)) && passed;
    }
    return Report(passed, "1/3 个 worker：构造后、任务内、完成后运行状态及线程数正确");
}

bool TestStoppedStatus() {
    bool passed = true;
    for (int count : {1, 3}) {
        // 关闭后对象仍存活，可以安全查询状态。
        ThreadPool pool(count);
        pool.Shutdown();
        const ThreadPool& view = pool;
        for (int attempt = 0; attempt < 2; ++attempt) {
            passed = (!view.IsRunning() && view.GetWorkerCount() == static_cast<std::size_t>(count) &&
                      view.GetTaskCount() == 0 && view.GetActiveCount() == 0) && passed;
            try {
                auto result = pool.Submit([] { return 42; });
                passed = false;
            } catch (const std::runtime_error& error) {
                passed = (std::string(error.what()) == "ThreadPool has stopped") && passed;
            } catch (...) { passed = false; }
        }
        passed = (!view.IsRunning() && view.GetWorkerCount() == static_cast<std::size_t>(count) &&
                  view.GetTaskCount() == 0 && view.GetActiveCount() == 0) && passed;
    }
    return Report(passed, "1/3 worker 显式关闭后：重复拒绝提交前后 running 为 false，四项状态一致");
}


// Shutdown 回归：去掉拒绝、排空、等待或 worker 自调用检查均应失败。
bool RejectsSubmission(ThreadPool& pool) {
    try { pool.Submit([] { return 42; }); }
    catch (const std::runtime_error&) { return true; }
    return false;
}

bool WaitForClosing(const ThreadPool& pool) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (pool.IsRunning()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

bool TestExplicitShutdown() {
    ThreadPool pool(3);
    std::vector<std::future<int>> results;
    for (int i = 0; i < 100; ++i) results.push_back(pool.Submit(add, i, 1));
    pool.Shutdown();
    bool passed = !pool.IsRunning() && pool.GetTaskCount() == 0 &&
        pool.GetActiveCount() == 0 && pool.GetWorkerCount() == 3;
    for (int i = 0; i < 100; ++i) passed = (results[i].get() == i + 1) && passed;
    passed = RejectsSubmission(pool) && passed;
    pool.Shutdown();
    passed = RejectsSubmission(pool) && !pool.IsRunning() && passed;
    return Report(passed, "显式 Shutdown 排空任务、状态归零、拒绝提交且重复调用安全");
}

bool TestConcurrentShutdown() {
    std::promise<void> entered, release, secondEntered;
    auto gate = release.get_future().share();
    auto started = entered.get_future();
    auto secondStarted = secondEntered.get_future();
    ThreadPool pool(1);
    auto active = pool.Submit([&entered, gate] { entered.set_value(); gate.wait(); });
    started.wait();
    std::vector<std::future<int>> queued;
    for (int i = 0; i < 20; ++i) queued.push_back(pool.Submit(add, i, 10));
    auto first = std::async(std::launch::async, [&pool] { pool.Shutdown(); });
    bool passed = WaitForClosing(pool);
    passed = RejectsSubmission(pool) && passed;
    auto second = std::async(std::launch::async, [&] {
        secondEntered.set_value();
        pool.Shutdown();
        return pool.GetActiveCount() == 0 && pool.GetTaskCount() == 0 && !pool.IsRunning();
    });
    secondStarted.wait();
    // promise 保证阻塞任务已进入；有限观察窗口检查 Shutdown 没有提前返回。
    passed = (first.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout) && passed;
    passed = (second.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout) && passed;
    passed = (pool.GetTaskCount() == 20 && pool.GetActiveCount() == 1) && passed;
    release.set_value();
    first.get();
    passed = second.get() && passed;
    active.get();
    for (int i = 0; i < 20; ++i) passed = (queued[i].get() == i + 10) && passed;
    pool.Shutdown();
    return Report(passed, "两个外部 Shutdown 等待活动及队列任务，关闭中拒绝提交，关闭后均返回");
}

bool TestWorkerShutdown(bool closing) {
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto started = entered.get_future();
    ThreadPool pool(1);
    auto rejected = pool.Submit([&] {
        entered.set_value();
        gate.wait();
        pool.Shutdown();
    });
    started.wait();
    auto next = pool.Submit([] { return 7; });
    bool passed = true;
    std::future<void> external;
    if (closing) {
        external = std::async(std::launch::async, [&] { pool.Shutdown(); });
        passed = WaitForClosing(pool);
    }
    release.set_value();
    bool caught = false;
    try { rejected.get(); }
    catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "worker thread cannot call Shutdown";
    }
    passed = caught && (next.get() == 7) && passed;
    if (closing) external.get();
    else passed = pool.IsRunning() && passed;
    pool.Shutdown();
    return Report(passed, closing ? "关闭中的 worker 自调用被拒绝，不与外部 join 死锁"
                                  : "运行中的 worker 自调用由 future 传递异常，池继续工作");
}

bool TestSimultaneousShutdown() {
    bool passed = true;
    for (int round = 0; round < 50; ++round) {
        ThreadPool pool(3);
        std::promise<void> release;
        auto gate = release.get_future().share();
        auto result = pool.Submit([] { return 42; });
        std::vector<std::future<void>> callers;
        for (int i = 0; i < 4; ++i) callers.push_back(std::async(std::launch::async, [&pool, gate] {
            gate.wait(); pool.Shutdown();
        }));
        release.set_value();
        for (auto& caller : callers) caller.get();
        passed = (result.get() == 42 && !pool.IsRunning() &&
            pool.GetTaskCount() == 0 && pool.GetActiveCount() == 0) && passed;
    }
    return Report(passed, "50 轮四个外部线程同时 Shutdown，无重复 join 或死锁");
}


// 先等待 worker 进入任务，再填充队列；不依赖 sleep 或调度速度。
bool TestQueueCapacity(std::size_t capacity) {
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto gate = release.get_future().share();
    std::atomic<int> executed{0}, rejectedExecuted{0};
    ThreadPool pool(1, capacity);
    // 位于 pool 之后，异常展开时先放行 worker，再析构线程池。
    struct ReleaseGuard {
        std::promise<void>& release;
        bool done = false;
        void open() { if (!done) { release.set_value(); done = true; } }
        ~ReleaseGuard() { open(); }
    } guard{release};
    auto active = pool.Submit([&] {
        entered.set_value();
        gate.wait();
        ++executed;
        return 42;
    });
    bool passed = started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    if (!passed) return Report(false, "worker 启动超时");
    passed = pool.GetTaskCount() == 0 && pool.GetActiveCount() == 1;
    std::vector<std::future<int>> queued;
    for (std::size_t i = 0; i < capacity; ++i) {
        queued.push_back(pool.Submit([&, i] { ++executed; return static_cast<int>(i); }));
        passed = queued.back().valid() && pool.GetTaskCount() == i + 1 && passed;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool rejected = false;
        try { pool.Submit([&] { ++rejectedExecuted; }); }
        catch (const std::runtime_error& error) {
            rejected = std::string(error.what()) == "ThreadPool task queue is full";
        } catch (...) {}
        passed = rejected && pool.GetTaskCount() == capacity && passed;
    }
    guard.open();
    passed = active.get() == 42 && passed;
    for (std::size_t i = 0; i < capacity; ++i)
        passed = queued[i].get() == static_cast<int>(i) && passed;
    auto recovered = pool.Submit([&] { ++executed; return 99; });
    passed = recovered.get() == 99 && passed;
    pool.Shutdown();
    passed = executed == static_cast<int>(capacity + 2) && rejectedExecuted == 0
        && pool.GetTaskCount() == 0 && passed;
    return Report(passed, capacity == 1
        ? "容量 1：正常提交、满队列重复拒绝、执行结果及释放后恢复"
        : "容量 2：正常提交、满队列重复拒绝、执行结果及释放后恢复");
}

int main() {
    int total = 0;
    int failures = 0;
    auto check = [&](bool passed) {
        ++total;
        if (!passed) ++failures;
    };

    // Submit：基础提交、无效线程数与析构排空。
    check(TestEmptyPool());
    check(TestInvalidCount(0));
    check(TestInvalidCount(-3));
    check(TestInvalidCount(-1));
    check(TestInvalidArguments(1, 0, "queueSize must be greater than 0"));
    check(TestInvalidArguments(4, 0, "queueSize must be greater than 0"));
    check(TestInvalidArguments(0, 0, "threadpoolCount must be greater than 0"));
    check(TestQueueCapacity(1));
    check(TestQueueCapacity(2));
    check(TestTaskCompletion(1));
    check(TestTaskCompletion(3));

    // Submit：返回类型、混合提交、停止状态与异常传递。
    check(TestVoidFuture());
    check(TestTemplateResults());
    check(TestTemplateMixedSubmissions());
    check(TestStoppedTemplateSubmission());
    check(TestTemplateTaskException());

    // Submit：可变参数、引用与仅可移动对象。
    check(TestParameterizedFunction());
    check(TestParameterizedLambda());
    check(TestParameterizedStrings());
    check(TestParameterizedMixed());
    check(TestParameterizedException());
    check(TestBoundReference());
    check(TestMoveOnlyBinding());
    check(TestStoppedParameterized());

    // GetTaskCount：只统计等待中的任务。
    check(TestTaskCount());
    check(TestStoppedTaskCount());
    check(TestActiveCount(1));
    check(TestActiveCount(3));
    check(TestVoidTaskException(false));
    check(TestVoidTaskException(true));
    check(TestRunningStatus());
    check(TestStoppedStatus());

    check(TestExplicitShutdown());
    check(TestConcurrentShutdown());
    check(TestWorkerShutdown(false));
    check(TestWorkerShutdown(true));
    check(TestSimultaneousShutdown());

    std::cout << "测试结束：" << (total - failures) << "/" << total << " 通过\n";
    return failures == 0 ? 0 : 1;
}
