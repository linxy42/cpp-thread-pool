#include "ThreadPool.h"
#include <iostream>
#include <mutex>
#include <vector>

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

int main() {
    int failures = 0;
    if (!TestEmptyPool()) ++failures;
    if (!TestInvalidCount(0)) ++failures;
    if (!TestInvalidCount(-3)) ++failures;
    if (!TestTaskCompletion(1)) ++failures;
    if (!TestTaskCompletion(3)) ++failures;

    std::cout << "测试结束：" << (5 - failures) << "/5 通过\n";
    return failures == 0 ? 0 : 1;
}
