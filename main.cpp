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

bool TestInvalidCount(int threadCount) {
    bool executed = false;
    bool rejected = true;
    {
        ThreadPool pool(threadCount);
        // 连续提交两次，也检查失败返回后锁是否自动释放。
        for (int i = 0; i < 2; ++i) {
            bool accepted = pool.Submit([&executed]() { executed = true; });
            if (accepted) {
                rejected = false;
            }
        }
    }
    bool passed = rejected && !executed;
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << "线程数 " << threadCount << " 拒绝提交\n";
    return passed;
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
            bool accepted = pool.Submit([i, &executionCounts, &resultMutex]() {
                std::lock_guard<std::mutex> lock(resultMutex);
                ++executionCounts[i];
            });
            if (!accepted) {
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

bool TestTemplateResults() {
    ThreadPool pool(3);
    auto integer = pool.SubmitNew([] { return 42; });
    auto decimal = pool.SubmitNew([] { return 3.25; });
    auto text = pool.SubmitNew([] { return std::string("thread pool"); });
    static_assert(std::is_same_v<decltype(integer), std::future<int>>);
    static_assert(std::is_same_v<decltype(decimal), std::future<double>>);
    static_assert(std::is_same_v<decltype(text), std::future<std::string>>);
    const bool intOk = integer.get() == 42;
    const bool doubleOk = decimal.get() == 3.25;
    const bool stringOk = text.get() == "thread pool";
    return Report(intOk && doubleOk && stringOk,
                  "SubmitNew 正确返回 int、double、string 及对应 future 类型");
}

bool TestTemplateMixedSubmissions() {
    std::atomic<int> voidCount{0};
    std::vector<std::future<int>> results;
    bool passed = true;
    {
        ThreadPool pool(3);
        for (int i = 0; i < 50; ++i) {
            if (!pool.Submit([&voidCount] { ++voidCount; })) passed = false;
            results.push_back(pool.SubmitNew([i] { return i * 2; }));
        }
        // 析构排空队列后，再读取任务结果。
    }
    for (int i = 0; i < 50; ++i) {
        if (results[i].get() != i * 2) passed = false;
    }
    return Report(passed && voidCount == 50,
                  "Submit 与 SubmitNew 混合执行，析构后 future 可取");
}

bool TestStoppedTemplateSubmission() {
    bool passed = true;
    bool executed = false;
    for (int count : {0, -3}) {
        // 没有公开 Stop 接口，用无效线程数构造仍存活的停止池。
        ThreadPool pool(count);
        for (int i = 0; i < 2; ++i) {
            try {
                pool.SubmitNew([&executed] { executed = true; return 1; });
                passed = false;
            } catch (const std::runtime_error& error) {
                if (std::string(error.what()) != "ThreadPool has stopped") passed = false;
            } catch (...) {
                passed = false;
            }
        }
    }
    return Report(passed && !executed,
                  "停止池重复 SubmitNew 抛异常且不执行任务");
}

bool TestTemplateTaskException() {
    ThreadPool pool(1);
    auto failed = pool.SubmitNew([]() -> std::string {
        throw std::runtime_error("template task failed");
    });
    auto next = pool.SubmitNew([] { return 99; });
    bool caught = false;
    try {
        failed.get();
    } catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "template task failed";
    }
    const bool continued = next.get() == 99;
    return Report(caught && continued,
                  "SubmitNew 异常由 future 传递，worker 继续执行");
}


int add(int a, int b) { return a + b; }

bool TestParameterizedFunction() {
    ThreadPool pool(2);
    auto result = pool.SubmitNew(add, 10, 20);
    static_assert(std::is_same_v<decltype(result), std::future<int>>);
    return Report(result.get() == 30, "普通函数 add 接收两个参数");
}

bool TestParameterizedLambda() {
    ThreadPool pool(2);
    auto result = pool.SubmitNew([](int a, double b, int c) {
        return a * b + c;
    }, 4, 2.5, 3);
    static_assert(std::is_same_v<decltype(result), std::future<double>>);
    return Report(result.get() == 13.0, "lambda 接收多个不同类型参数");
}

bool TestParameterizedStrings() {
    ThreadPool pool(2);
    std::string prefix = "hello";
    auto result = pool.SubmitNew([](std::string a, std::string b) {
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
    auto copied = pool.SubmitNew(increment, value);
    const bool copyOk = copied.get() == 11 && value == 10;
    auto referenced = pool.SubmitNew(increment, std::ref(value));
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
    auto lvalue = pool.SubmitNew(lvalueCallable, 8);
    auto result = pool.SubmitNew(std::move(callable), 2);
    auto argument = pool.SubmitNew([](const std::unique_ptr<int>& n) {
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
            if (!pool.Submit([&count] { ++count; })) accepted = false;
            results.push_back(pool.SubmitNew(add, i, 10));
        }
        voidResult = pool.SubmitNew([](std::atomic<int>& n, int amount) {
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
    for (int count : {0, -3}) {
        ThreadPool pool(count);
        for (int i = 0; i < 2; ++i) {
            try {
                pool.SubmitNew([&executed](int n) { executed = true; return n; }, i);
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
    auto failed = pool.SubmitNew([](std::string message) -> int {
        throw std::runtime_error(message);
    }, std::string("parameterized failure"));
    auto next = pool.SubmitNew(add, 2, 3);
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
        auto active = pool.SubmitNew([&started, gate] {
            started.set_value();
            gate.wait();
        });
        startedFuture.wait();
        // 唯一 worker 正在执行任务，但队列为空。
        passed = (view.GetTaskCount() == 0) && passed;
        passed = (active.wait_for(std::chrono::milliseconds(0))
                  == std::future_status::timeout) && passed;
        for (int i = 0; i < 5; ++i) {
            passed = pool.Submit([&completed] { ++completed; }) && passed;
        }
        auto queued = pool.SubmitNew([] { return 42; });
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
    for (int count : {0, -3}) {
        ThreadPool pool(count);
        passed = (pool.GetTaskCount() == 0 && pool.GetActiveCount() == 0) && passed;
        passed = !pool.Submit([] {}) && passed;
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
    std::vector<std::future<void>> results;
    for (int i = 0; i < workerCount; ++i) {
        auto task = [&, i, gate] {
            started[i].set_value();
            gate.wait();
            ++completed;
        };
        if (i % 2 == 0) passed = pool.Submit(task) && passed;
        else results.push_back(pool.SubmitNew(task));
    }
    for (auto& signal : entered) {
        passed = (signal.wait_for(std::chrono::seconds(5))
                  == std::future_status::ready) && passed;
    }
    passed = (view.GetActiveCount() == static_cast<std::size_t>(workerCount)) && passed;
    passed = (view.GetTaskCount() == 0) && passed;
    for (int i = 0; i < 6; ++i) {
        passed = pool.Submit([&completed] { ++completed; }) && passed;
    }
    passed = (view.GetTaskCount() == 6) && passed;
    passed = (view.GetActiveCount() == static_cast<std::size_t>(workerCount)) && passed;
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

bool TestPlainTaskException(bool unknownException) {
    std::promise<void> started;
    auto entered = started.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::promise<void> nextDone;
    auto nextResult = nextDone.get_future();
    ThreadPool pool(1);
    bool passed = pool.Submit([&started, gate, unknownException] {
        started.set_value();
        gate.wait();
        if (unknownException) throw 7;
        throw std::runtime_error("expected Submit exception");
    });
    passed = (entered.wait_for(std::chrono::seconds(5))
              == std::future_status::ready) && passed;
    passed = (pool.GetActiveCount() == 1) && passed;
    passed = pool.Submit([&nextDone] { nextDone.set_value(); }) && passed;
    auto futureResult = pool.SubmitNew([](int x) { return x + 1; }, 41);
    release.set_value();
    passed = (nextResult.wait_for(std::chrono::seconds(5))
              == std::future_status::ready) && passed;
    const bool ready = futureResult.wait_for(std::chrono::seconds(5))
        == std::future_status::ready;
    passed = ready && passed;
    if (ready) passed = (futureResult.get() == 42) && passed;
    passed = WaitForIdleCount(pool) && passed;
    passed = (pool.GetTaskCount() == 0) && passed;
    return Report(passed, unknownException
        ? "Submit 未知异常被隔离，后续 Submit/future 正常，active 归零"
        : "Submit runtime_error 被隔离，后续 Submit/future 正常，active 归零");
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
    check(TestTaskCompletion(1));
    check(TestTaskCompletion(3));

    // SubmitNew：返回类型、混合提交、停止状态与异常传递。
    check(TestTemplateResults());
    check(TestTemplateMixedSubmissions());
    check(TestStoppedTemplateSubmission());
    check(TestTemplateTaskException());

    // SubmitNew：可变参数、引用与仅可移动对象。
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
    check(TestPlainTaskException(false));
    check(TestPlainTaskException(true));

    std::cout << "测试结束：" << (total - failures) << "/" << total << " 通过\n";
    return failures == 0 ? 0 : 1;
}
