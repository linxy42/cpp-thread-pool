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

bool TestIntResult() {
    ThreadPool pool(2);
    auto result = pool.SubmitInt([] { return 42; });
    return Report(result.get() == 42, "SubmitInt 返回 42");
}

bool TestMultipleIntResults() {
    ThreadPool pool(4);
    std::vector<std::future<int>> results;
    for (int i = 0; i < 100; ++i) {
        results.push_back(pool.SubmitInt([i] { return i * i - 7; }));
    }
    bool passed = true;
    for (int i = 0; i < 100; ++i) {
        if (results[i].get() != i * i - 7) passed = false;
    }
    return Report(passed, "100 个 int 任务全部返回正确结果");
}

bool TestMixedSubmissions() {
    std::atomic<int> voidCount{0};
    std::vector<std::future<int>> results;
    bool passed = true;
    {
        ThreadPool pool(3);
        for (int i = 0; i < 50; ++i) {
            if (!pool.Submit([&voidCount] { ++voidCount; })) passed = false;
            results.push_back(pool.SubmitInt([i] { return i + 10; }));
        }
    }
    for (int i = 0; i < 50; ++i) {
        if (results[i].get() != i + 10) passed = false;
    }
    return Report(passed && voidCount == 50, "50 个 void 与 50 个 int 混合执行");
}

bool TestPendingIntShutdown() {
    std::promise<void> started;
    std::promise<void> release;
    auto gate = release.get_future().share();
    auto pool = std::make_unique<ThreadPool>(1);
    auto active = pool->SubmitInt([&started, gate] {
        started.set_value();
        gate.wait();
        return 123;
    });
    started.get_future().wait();
    // 唯一 worker 被 gate 阻塞，后面的任务必定还在队列中。
    auto queued = pool->SubmitInt([] { return 456; });
    bool pending = active.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout
        && queued.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    std::promise<void> destroying;
    auto destructionStarted = destroying.get_future();
    auto shutdown = std::async(std::launch::async,
        [owned = std::move(pool), &destroying]() mutable {
            destroying.set_value();
            owned.reset();
        });
    destructionStarted.wait();
    bool waited = shutdown.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
    release.set_value();
    shutdown.get();
    bool ready = active.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready
        && queued.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    int activeValue = active.get();
    int queuedValue = queued.get();
    return Report(pending && waited && ready && activeValue == 123 && queuedValue == 456,
                  "析构等待执行中任务、排空队列，析构后 future 可取");
}

bool TestStoppedIntSubmission() {
    bool passed = true;
    bool executed = false;
    for (int count : {0, -3}) {
        ThreadPool pool(count);
        for (int i = 0; i < 2; ++i) {
            try {
                pool.SubmitInt([&executed] { executed = true; return 1; });
                passed = false;
            } catch (const std::runtime_error&) {
                // 再次提交也能抛异常，验证异常退出会释放锁。
            } catch (...) {
                passed = false;
            }
        }
    }
    return Report(passed && !executed, "停止池重复 SubmitInt 抛出 runtime_error");
}

bool TestIntTaskException() {
    ThreadPool pool(1);
    auto failed = pool.SubmitInt([]() -> int { throw std::runtime_error("task failed"); });
    auto next = pool.SubmitInt([] { return 99; });
    bool caught = false;
    try {
        failed.get();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    return Report(caught && next.get() == 99, "int 任务异常由 future 传递，worker 继续执行");
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

int main() {
    int failures = 0;
    if (!TestEmptyPool()) ++failures;
    if (!TestInvalidCount(0)) ++failures;
    if (!TestInvalidCount(-3)) ++failures;
    if (!TestTaskCompletion(1)) ++failures;
    if (!TestTaskCompletion(3)) ++failures;

    if (!TestIntResult()) ++failures;
    if (!TestMultipleIntResults()) ++failures;
    if (!TestMixedSubmissions()) ++failures;
    if (!TestPendingIntShutdown()) ++failures;
    if (!TestStoppedIntSubmission()) ++failures;
    if (!TestIntTaskException()) ++failures;

    if (!TestTemplateResults()) ++failures;
    if (!TestTemplateMixedSubmissions()) ++failures;
    if (!TestStoppedTemplateSubmission()) ++failures;
    if (!TestTemplateTaskException()) ++failures;

    if (!TestParameterizedFunction()) ++failures;
    if (!TestParameterizedLambda()) ++failures;
    if (!TestParameterizedStrings()) ++failures;
    if (!TestParameterizedMixed()) ++failures;
    if (!TestParameterizedException()) ++failures;
    if (!TestBoundReference()) ++failures;
    if (!TestMoveOnlyBinding()) ++failures;
    if (!TestStoppedParameterized()) ++failures;

    std::cout << "测试结束：" << (23 - failures) << "/23 通过\n";
    return failures == 0 ? 0 : 1;
}
