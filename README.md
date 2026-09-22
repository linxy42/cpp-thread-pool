# c++线程池

基于 **C++17 标准库**实现的固定大小线程池，独立仓库：[linxy42/cpp-thread-pool](https://github.com/linxy42/cpp-thread-pool)。通过复用 worker 线程执行任务，练习任务调度、线程同步、泛型编程和资源管理。

**当前版本：1.0（发布标签 `v1.0.0`）。** 基础功能、独立测试和使用说明已整理完成。当前能力与已知边界如下，不承诺生产环境下的完整容错或性能指标。

## 功能特性

- 固定数量的 worker，使用 FIFO 等待队列；任务出队后在锁外执行。
- 泛型 `Submit(F&& task, Args&&... args)`，支持普通函数、lambda、带参数任务，以及 `void`、`int`、`double`、`std::string` 等返回类型。
- 通过 `std::future` 获取结果和任务异常；使用 `std::packaged_task` 包装任务，使用 `std::promise` 表示丢弃结果。
- 四个状态查询接口：等待任务数、worker 活动任务数、固定 worker 数量及是否接受任务。
- 有界等待队列，容量默认 100；支持 `Abort`、`CallerRuns`、`Discard` 三种拒绝策略。
- `Running → ShuttingDown → Stopped` 三态关闭，支持显式、重复及多个外部线程并发 `Shutdown()`。
- RAII 管理锁、活动任务计数和线程池生命周期；析构时排空已入队任务并回收 worker。
- 构造参数校验、任务异常隔离，以及按功能分区的 **45 项独立检查**；不依赖第三方测试框架。

## 项目结构

```text
cpp-thread-pool/
├── ThreadPool.h                 # 类声明、状态和泛型 Submit 定义
├── ThreadPool.cpp               # worker、Shutdown、查询和 RAII 实现
├── main.cpp                     # 简洁使用示例
├── tests/
│   └── ThreadPoolTests.cpp       # 独立测试入口
├── .vscode/
│   ├── tasks.json               # 示例与测试的构建、运行任务
│   ├── launch.json              # 示例调试配置
│   └── c_cpp_properties.json    # macOS / C++17 IntelliSense 配置
├── .gitignore                   # 忽略可执行文件、目标文件和调试产物
└── README.md
```

`Submit` 是模板，完整定义放在头文件中，供调用处实例化。示例和测试各自有 `main()`，需要分别编译链接。

## 编译与运行

需要支持 C++17 的编译器和标准库。当前验证环境为 macOS Apple Silicon、Apple clang 21，使用 pthread；仓库没有引入 CMake 或第三方依赖。

在仓库根目录执行：

```sh
# 编译并运行基础示例
clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -pthread main.cpp ThreadPool.cpp -o main
./main

# 编译并运行完整测试
clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -pthread tests/ThreadPoolTests.cpp ThreadPool.cpp -o tests/ThreadPoolTests
./tests/ThreadPoolTests
```

示例输出 `10 + 20 = 30`。测试每项输出 PASS/FAIL，全部通过时最后输出 `测试结束：45/45 通过`，退出码为 0；检查失败返回 1。

Linux 可使用支持 C++17 的 clang++ 或将命令中的编译器替换为 g++；本次发布未在 Linux/Windows 实机验证。Windows 可在 Visual Studio 中建立两个 C++17 控制台目标，分别将示例或测试与 `ThreadPool.cpp` 链接。

### VS Code

直接打开 `cpp-thread-pool` 文件夹。当前配置使用 `/usr/bin/clang++`、LLDB 和 Microsoft C/C++ 扩展；其他平台需调整工具链路径及 IntelliSense 配置。

| 操作 / 任务 | 用途 |
| --- | --- |
| `Cmd+Shift+B` / `build thread pool` | 默认构建示例，生成 `main` |
| Tasks: Run Task → `run thread pool` | 先构建，再运行示例 |
| Tasks: Run Task → `build thread pool tests` | 严格编译测试，生成 `tests/ThreadPoolTests` |
| Tasks: Run Task → `run thread pool tests` | 先构建，再运行完整测试 |
| F5 → “运行线程池” | 先构建，再调试示例 |

不要用“生成活动文件”任务构建这个多文件项目，也不要把示例和测试的两个入口链接到同一个可执行文件。

## 示例用法

### 基础提交

以下完整示例与 `main.cpp` 的用法一致：

```cpp
#include "ThreadPool.h"
#include <iostream>

int main() {
    ThreadPool pool(4);
    auto result = pool.Submit([](int a, int b) {
        return a + b;
    }, 10, 20);
    std::cout << "10 + 20 = " << result.get() << '\n';
    return 0; // 析构等待已入队任务完成，并 join 所有 worker。
}
```

### 无返回值、引用参数与异常

下面片段放在调用函数中；除 `ThreadPool.h` 外，显式包含 `<functional>`、`<iostream>` 和 `<stdexcept>`：

```cpp
int value = 10; // 先于 pool 构造，确保被引用对象的生命周期足够长。
ThreadPool pool(2, 100, RejectPolicy::Abort);

auto done = pool.Submit([](int& n) { ++n; }, std::ref(value));
done.get(); // future<void>：等待完成，此时 value == 11。

auto failed = pool.Submit([]() -> int {
    throw std::runtime_error("task failed");
});
try {
    failed.get();
} catch (const std::runtime_error& error) {
    std::cerr << error.what() << '\n';
}

auto next = pool.Submit([] { return 42; });
pool.Shutdown(); // 排空队列并等待 worker 退出。
std::cout << next.get() << '\n'; // 42，单个任务失败不会终止 worker。
pool.Shutdown(); // 重复关闭直接返回。
```

## 核心设计

### worker、任务队列与同步

构造函数创建 `std::vector<std::thread>`，worker 数量在对象存活期间固定。等待队列为 `std::queue<std::function<void()>>`，不同返回类型的任务先包装为统一的 `void()` 调用入口。

```text
Submit → 包装任务 → 加锁检查状态与容量 → 入队 → 解锁 → notify_one
                                                    ↓
worker → 加锁等待 → 取队首任务 → 出队 → 解锁 → 执行 → 回到循环
```

`mtx` 同时保护等待队列和运行状态。worker 在“队列为空且仍为 Running”时循环调用 `condition.wait(lock)`：等待会释放锁，唤醒后重新加锁并检查条件，避免虚假唤醒导致访问空队列。任务执行期间不持有队列锁，因此其他线程可以继续提交、查询或开始关闭。

FIFO 表示按入队顺序取出任务；多个 worker 的实际开始、完成顺序不保证与提交顺序一致。

### RAII

- `std::lock_guard` / `std::unique_lock` 管理加锁与解锁。
- worker 在执行作用域内创建 `ActiveTaskGuard`，构造时增加原子活动数，离开作用域时自动减少。
- `~ThreadPool()` 复用 `Shutdown()`，正常销毁时等待队列排空并回收线程。

## 泛型 Submit 与 future

公开提交接口：

```cpp
template<typename F, typename... Args>
std::future<std::invoke_result_t<F, Args...>>
Submit(F&& task, Args&&... args);
```

一次普通提交的执行过程：

1. `std::invoke_result_t` 推导返回类型；`std::forward` 将任务和参数转发给 `std::bind`，形成无参调用对象。
2. 将绑定对象移入 `std::packaged_task<ReturnType()>`，取得对应的 `std::future<ReturnType>`。
3. 因为 packaged_task 不可拷贝，用 `std::shared_ptr` 持有，再用捕获该指针的可拷贝 lambda 适配 `std::function<void()>`。
4. 在同一把锁内检查运行状态和容量。正常入队后解锁并唤醒一个 worker；满队列走配置的拒绝策略。
5. worker 执行 packaged_task，将返回值或任务异常写入共享状态；调用方通过 `future.get()` 等待并取得结果，或接收异常。

`future<void>` 同样需要 `get()` 以等待完成并观察异常。普通 future 的结果只能 `get()` 一次；忽略 future 会使其中保存的任务异常不被调用方观察到。

`std::bind` 默认按值保存衰减后的参数：左值拷贝、右值可移动。要引用原对象需使用 `std::ref`，并保证对象在任务完成前有效。转发发生在构造绑定对象时，bind 执行时通常将保存的普通参数作为左值传递，因此 **不支持任意右值引用调用形式，也不能直接将绑定的 unique_ptr 按值移交给任务**。可使用接收 const 引用的任务，或捕获所有权的 lambda；现有测试覆盖这些仅可移动对象的用法。

## 状态查询接口

| 接口（均为 const） | 实际含义 | 同步方式 |
| --- | --- | --- |
| `GetTaskCount()` | 等待队列中的任务数，不包含已取出的任务 | 加锁读取 `tasks.size()` |
| `GetActiveCount()` | worker 正在执行的任务数，不包含 CallerRuns | 原子计数 `load()` |
| `GetWorkerCount()` | 构造时固定的 worker 数量，关闭后仍保持原值 | 读取构造后不再增删的 `workers.size()` |
| `IsRunning()` | 是否处于 Running、仍接受任务 | 加锁读取状态 |

它们是瞬时观察，不构成联合快照。`IsRunning()` 为 true 不保证下一次 Submit 一定成功，期间可能关闭或队列变满。worker 出队、解锁后才增加活动数，因此即使观察到 queued 和 active 都为 0，也不能据此判断所有任务完成。应使用 future 等待具体任务，或用 Shutdown 等待 worker 排空；future 就绪也可能早于活动数递减。

## 有界队列与 RejectPolicy

构造函数：

```cpp
ThreadPool(int threadCount, std::size_t queueSize = 100,
           RejectPolicy policy = RejectPolicy::Abort);
```

`threadCount <= 0` 或 `queueSize == 0` 时，在创建任何 worker 前抛出 `std::invalid_argument`。线程数校验优先；为保持已有测试与行为兼容，消息分别为 `threadpoolCount must be greater than 0` 和 `queueSize must be greater than 0`。容量类型为无符号 `std::size_t`，调用方应提供正容量，不要传负数后依赖隐式转换进行校验。

容量只限制**等待队列**，不包括 worker 正在执行的任务，也不限制 CallerRuns 并行调用数。构造后不提供修改容量或策略的接口。

三种策略仅在 Running 且队列已满时生效；未满时均正常入队：

| 策略 | 满队列行为 | 返回值 / 异常 |
| --- | --- | --- |
| `Abort`（默认） | 不入队、不执行，立即拒绝 | Submit 抛出 `std::runtime_error("ThreadPool task queue is full")`，不返回 future |
| `CallerRuns` | 不入队，解锁后由提交线程同步执行 | Submit 等执行结束才返回已就绪的 future；结果及任务异常由 get 获取 |
| `Discard` | 不入队、不执行，丢弃本次新任务 | 返回已就绪的 future，get 抛出 `std::runtime_error("ThreadPool task was discarded")`，也适用于 void |

Discard 使用 `std::promise<ReturnType>::set_exception()` 构造明确的拒绝结果，返回这个 promise 对应的 future，而非原 packaged_task 的 future。CallerRuns 可以减慢提交线程，但会增加 Submit 的耗时；任务异常由 packaged_task 保存，不直接作为任务异常从 Submit 抛出。

关闭中或关闭后，三种策略都优先拒绝提交并抛出 `std::runtime_error("ThreadPool has stopped")`，不会转为 CallerRuns。实现会先绑定任务、创建 packaged_task，再检查池状态，因此绑定或分配失败仍可能先抛异常。

## 三态 Shutdown 状态机

```text
Running ──首个外部 Shutdown()──> ShuttingDown ──全部 worker join 完成──> Stopped
   接受任务                         拒绝新任务、排空队列                       已关闭
```

| 状态 | Submit | worker 与 Shutdown |
| --- | --- | --- |
| `Running` | 根据容量入队或执行拒绝策略 | worker 等待或执行任务；首个关闭者切换状态 |
| `ShuttingDown` | 抛出停止异常 | worker 继续排空；后续关闭者等待完成 |
| `Stopped` | 抛出停止异常 | worker 均已 join；重复 Shutdown 直接返回 |

第一个外部调用者在锁内改为 ShuttingDown，解锁后 `condition.notify_all()` 并负责 join，避免持有队列锁等待 worker。其他外部调用者通过 `shutdownCondition.wait(lock, predicate)` 等待 Stopped，等待期间释放锁；负责人 join 完毕后加锁写入 Stopped，再唤醒所有关闭等待者。两个条件变量分别负责“等待任务”和“等待关闭完成”。

正常返回的 Shutdown 保证入队任务已完成、worker 已退出，等待队列和 worker 活动数为 0；不支持重新启动。**Shutdown 不等待其他提交线程上正在执行的 CallerRuns 任务**，这些 Submit 调用需要调用方另行等待。

worker 使用 `thread_local` 指针标记所属线程池。本池 worker 调用 Shutdown 会在状态等待前抛出 `std::runtime_error("worker thread cannot call Shutdown")`，避免等待自身退出；异常可在任务内捕获或由 future.get 接收。

## 线程安全、异常处理与 1.0 边界

- 对象构造完成且保持存活时，支持多个外部线程提交、查询和显式关闭；队列及状态由同一 mutex 保护，活动数使用 atomic。任务访问的业务数据仍需调用方自行同步。
- 析构开始前，必须停止并等待所有访问线程，包括 Submit（特别是 CallerRuns）、查询及 Shutdown 调用方。并发 Shutdown 不等于并发析构安全；任务不得销毁自身线程池。
- 任务的标准异常和非标准异常由 packaged_task 保存并通过 future 传播，worker 保留外层 try/catch，活动计数由 RAII 收尾。
- 当前未在提交时主动检查空 callable；不要提交空函数指针。空 `std::function` 的调用错误通常在 future.get 时观察到。不要将“任务异常隔离”理解为能捕获无效内存访问等未定义行为。
- **部分 worker 创建成功、后续线程创建失败时的构造回滚尚未实现**；此类资源耗尽路径可能因销毁仍可 join 的线程而终止进程。任务出队复制等内部资源分配失败也没有完整恢复保证。1.0 不承诺基础设施故障下的强异常安全。
- 不提供任务取消、关闭超时或强制终止。任务若一直阻塞，Shutdown/析构也会一直等待；worker 内等待同池尚未执行的任务可能耗尽 worker 并死锁。
- ThreadPool 含 mutex 等不可复制成员，当前不支持复制或移动。没有动态扩缩容、优先级调度、work stealing、无锁队列或性能基准承诺。

## 测试覆盖与发布验证

`tests/ThreadPoolTests.cpp` 复用原有测试并按功能分区，当前共 45 项运行检查：

| 分区 | 项数 | 覆盖内容 |
| --- | --- | --- |
| Constructor / 生命周期 | 9 | 空池析构、非法线程数、零容量及校验优先级、1/3 worker 各执行任务一次 |
| Submit | 9 | void/int/double/string、普通函数、多参数 lambda、混合任务、std::ref、仅可移动对象、析构后取结果 |
| Status | 6 | 等待数、单/多 worker 活动数及归零、固定 worker 数、运行/停止状态、const 查询 |
| Shutdown | 7 | 停止后拒绝、显式/重复关闭、两个外部关闭者等待、worker 自调用拒绝、50 轮四线程同时关闭 |
| Exception | 4 | 返回值/带参数/void 任务的标准及非标准异常、后续任务继续执行、活动数归零 |
| BoundedQueue | 2 | 容量 1/2、满队列重复拒绝、释放容量后恢复 |
| RejectPolicy | 8 | 三策略满/未满行为、关闭优先、CallerRuns 线程身份和锁外执行、Discard void、CallerRuns 异常 |

测试使用 promise/future 控制 worker 的进入与放行，避免用 sleep 猜测队列已满；部分关闭等待检查使用有限观察窗口。它们验证功能与并发场景，不构成吞吐量测试或对所有线程交错的证明。

1.0 发布验证（2026-09-22）：示例与测试均使用 C++17、`-Wall -Wextra -Werror -pedantic -pthread` 完整编译运行；示例输出 `10 + 20 = 30`，测试 **45/45 通过**，退出码均为 0。本次不宣称已完成 ThreadSanitizer、资源耗尽注入或跨平台验证。

## 后续 2.0 规划

以下均为展望，尚未实现；优先完善可靠性，再评估调度扩展：

1. 补齐部分线程创建失败的回滚、空任务校验和内部异常路径测试。
2. 增加任务等待/关闭超时及可协作的取消机制，明确排队任务与执行中任务的取消语义。
3. 动态扩缩容，定义最小/最大 worker 数量和空闲回收规则。
4. 优先级任务队列，并处理公平性与低优先级任务饥饿问题。
5. 探索 work stealing，配套吞吐量、延迟和锁竞争基准后再评估收益。
6. 增加 CMake、跨平台 CI 和并发检测，让构建与验证更容易复现。

提交历史保留从线程基础、泛型任务、RAII、状态查询到关闭与拒绝策略的学习过程；本 README 统一描述当前 1.0 行为。
