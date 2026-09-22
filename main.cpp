#include "ThreadPool.h"
#include <iostream>

int main() {
    ThreadPool pool(4);

    // 提交带参数的任务，通过 future 获取结果。
    auto result = pool.Submit([](int a, int b) {
        return a + b;
    }, 10, 20);

    std::cout << "10 + 20 = " << result.get() << '\n';
    return 0; // pool 析构时等待任务完成并回收 worker。
}
