# c++线程池

独立 GitHub 仓库的教学项目：`linxy42/cpp-thread-pool`。项目中文名称为“c++线程池”。

## 当前进度

当前已在 RAII 活动任务计数的基础上，完成统一提交接口：仅保留模板版 `Submit(F&& task, Arges&&... arges)`，支持 void、不同返回类型和带参数任务，统一返回对应 future；已补齐 `GetTaskCount()`、`GetActiveCount()`、`GetWorkerCount()`、`IsRunning()` 四个状态查询接口。此前已完成 `Running / ShuttingDown / Stopped` 三态生命周期及显式 `Shutdown()`，支持幂等和多个外部线程并发关闭，析构统一调用 `Shutdown()`。本阶段完成有界任务队列、构造参数校验和满队列拒绝。当前项目使用 **C++17**。

已实现能力：

- 固定数量的 worker 线程池，使用 `std::queue<std::function<void()>>` 保存任务。
- 有界等待队列：构造参数 `queueSize` 默认为 100，由 `maxQueueSize` 保存；队列满时拒绝新任务，非法线程数或零队列容量在创建 worker 前抛异常。
- 使用 `std::mutex` 保护任务队列和运行状态，配合 `std::condition_variable` 等待与唤醒；worker 取出任务后释放锁，再执行任务。
- 显式 `Shutdown()` 或析构触发优雅关闭：停止接收任务，唤醒所有 worker，执行完已接收的任务，再逐一 `join()`；关闭完成后重复调用直接返回。
- `Submit` 使用可变参数模板、`std::bind` 和 `std::forward` 接收任务及参数，自动推导返回类型，返回对应的 `std::future`；线程池不运行时抛出 `std::runtime_error`。旧 bool 提交接口已移除；int 专用接口此前已移除。void 任务同样返回 `std::future<void>`，用 `get()` 等待完成并接收异常。
- `GetTaskCount()` 读取等待队列中的任务数（queued task count），在锁内调用 `tasks.size()`，不包含已取出的任务。
- `GetActiveCount()` 读取正在执行的任务数（active task count）；使用 `std::atomic<std::size_t> activeCount{0}` 和 `load()`，多个 worker 的递增、递减不会丢失更新。
- `GetWorkerCount()` 返回固定 worker 数量；`IsRunning()` 在锁内读取是否仍接受任务，正常构造为 true，关闭中及关闭后为 false；无效构造参数直接抛异常。
- 模板版 `Submit` 使用 `std::lock_guard`，在同一锁内检查 `state == State::Running`、队列容量并入队，离开作用域释放锁后再通知 worker。
- worker 使用局部 `ActiveTaskGuard` 管理活动数：构造时递增，离开作用域时自动递减；任务异常由 packaged_task 保存，worker 保留 `try/catch` 外层保护，单个任务失败不影响后续任务执行。RAII 将计数收尾绑定到对象生命周期，避免维护任务处理逻辑时遗漏手动递减。

项目结构：

- `ThreadPool.h`：类声明、成员和模板版 `Submit` 的完整定义，使用 pragma once 防止重复包含；模板定义放在头文件中，供调用处实例化。
- `ThreadPool.cpp`：构造函数、worker 循环、四个状态查询接口、Shutdown、析构函数及 `ActiveTaskGuard` 的实现。
- `main.cpp`：37 个独立测试，每组输出 PASS/FAIL；失败返回非零退出码。
- `.vscode/tasks.json`：默认任务同时编译 main.cpp 和 ThreadPool.cpp；run 依赖 build。
- `.vscode/launch.json`：启动前执行完整构建，调试生成的 main。
- `.gitignore`：忽略可执行文件、目标文件和 macOS 调试产物。

验证（2026-09-13）：C++17 多文件编译通过，开启 Wall/Wextra/Werror/pedantic 无警告；5/5 测试通过。
测试分别覆盖空池析构、0 和 -3 拒绝重复提交、1 和 3 个 worker 将 20 个任务各执行一次。每个池离开作用域后才核对结果，主线程不通过 sleep 猜测完成时间。此测试覆盖多 worker 下的正确性，不测吞吐量或证明并行加速。

当前提供独立 `Shutdown()` 接口；调用方仍须在析构开始前停止并等待所有访问线程（包括 Submit、查询及 Shutdown 调用方），任务不得销毁自身线程池。所有提交任务的异常由 packaged_task 保存并经 `future.get()` 传递；空任务尚未在提交时主动拒绝，部分线程创建失败时的回收仍待完善。`Submit` 的任务异常由 packaged_task 保存，并在 `future.get()` 时重新抛出。
int 专用接口阶段验证（2026-09-16）：C++17 严格编译无警告，11/11 测试通过。新增覆盖单个 int 返回值、100 个 int 任务、void/int 混合提交、执行中及排队 int 任务的析构排空、停止池重复提交异常，以及任务异常经 future 传递后 worker 继续执行。停止路径用 0 和 -3 个线程构造的存活对象验证，不在析构后调用成员函数。

模板阶段验证（2026-09-16）：C++17 严格编译无警告，15/15 测试通过。在原有 11 项基础上，新增不同返回值与对应 future 类型检查、旧 bool 接口与模板接口混合提交及析构排空、停止池重复提交异常、任务异常经 future 传递后 worker 继续执行。

带参数阶段验证（2026-09-16）：C++17 严格编译无警告，23/23 测试通过。新增普通函数 add、多参数 lambda、string 返回值和左值/右值参数、std::ref、左值 callable 与仅可移动对象、带参数任务与 Submit 混合执行及析构排空、停止状态重复提交异常、任务异常传递测试。停止状态仍用 0 和 -3 个线程构造的存活对象验证，不调用已析构对象。

任务统计阶段验证（2026-09-17）：C++17 严格编译无警告，当前 23/23 测试通过。移除旧 int 专用接口的 6 项专用测试，新增 2 项队列统计测试和 4 项活动计数、异常隔离测试；其余 Submit/future 回归测试保留。通过 promise 控制任务开始和释放，验证 queued 为 6、单 worker 的 active 为 1、3 个 worker 并发时 active 为 3，以及任务结束后归零；普通 Submit 抛出 runtime_error 或未知异常后，后续 Submit 与带参数 future 任务仍能执行。

RAII 阶段验证（2026-09-18）：C++17 多文件严格编译（Wall/Wextra/Werror/pedantic）无警告，23/23 测试通过。正常任务执行时 active 为 1，3 个 worker 同时执行时 active 为 3，完成后均归零；普通 Submit 抛出 std::runtime_error 或未知异常后计数仍归零，worker 继续执行后续普通和模板任务。GetTaskCount、带参数任务、future 返回值与异常传递等现有回归测试全部通过。复用现有 promise 同步测试，无需修改测试或修复 RAII 实现。

统一接口验证（2026-09-18）：先运行原有 23/23 测试，并单独确认原模板对无参 void 任务返回 `std::future<void>`、`get()` 等待完成及传递异常；之后删除 bool 接口并完成模板重命名。迁移旧 bool 返回值断言和普通异常测试，新增 void lambda / `std::function<void()>` 的 future 类型及 get 测试，最终 C++17 严格编译无警告，24/24 测试通过。覆盖 int/double/string、带参数任务、左值/右值和 std::ref、仅可移动对象、停止池重复拒绝且不入队、析构排空及 RAII 并发计数。模板实现、参数绑定、锁内入队和 worker 逻辑无须修复。该次验证未 commit/push。

统一接口与状态查询收尾验证（2026-09-18）：确认仅保留统一模板 `Submit`，旧 `SubmitInt`、`SubmitNew` 和 bool 提交接口均无代码残留，所有调用点已迁移。C++17 严格编译（Wall/Wextra/Werror/pedantic）无警告，26/26 测试通过。覆盖 void/int/double/string、带参数任务、future.get、停止池拒绝提交、队列计数、单 worker 与多 worker 活动数及归零、标准/未知异常后 worker 继续工作；新增检查 1/3 个 worker 的数量及构造后、任务内、任务完成后 IsRunning 为 true，0/-3 停止池重复拒绝提交前后 IsRunning 为 false 且三项计数均为 0。停止状态测试对象始终存活，析构排空另有回归测试，不在析构开始后查询状态。线程池实现无需修复，本次仅补齐测试与文档，并将本阶段改动一并提交。

以上早期验证记录描述当时的接口行为；当前 API 以统一模板为准。停止状态提交统一抛出 `std::runtime_error("ThreadPool has stopped")`，不再返回 bool。早期测试用 0/-3 个线程构造的存活停止池验证拒绝行为；当前无效线程数在构造时抛异常，停止状态测试已统一改用显式 Shutdown 后仍存活的对象，不在析构开始后调用 Submit。

当前已完成有界队列与满队列抛异常拒绝；下一步可按学习进度完善拒绝策略（Abort / CallerRuns / Discard）、整理测试与文档并准备基础版收尾。空任务主动拒绝和部分线程创建失败时的回收仍待完善，动态扩缩容属于后续进阶内容。

先由学习者写代码，再一起检查、验证、提交；不提前填完后续答案。

起点依据：你在另一个仓库 cpp-learing 中的 StudentManagement2.0 已使用类、构造函数、vector、引用和文件读写。
项目最初以 thread 和 join 为起点，目前已推进到线程数量校验、提交状态检查、析构排空、模板任务返回值和带参数任务提交。

## 第一版的边界

最初第一版采用 C++11 标准库，固定数量的工作线程；当前版本使用 C++17。最终以 queue<std::function<void()>> 保存无参数、无返回值任务；
先学习普通函数，再介绍 lambda 和 std::function，不要求自己编写模板。
提交任务后唤醒工作线程；工作线程等待任务、取任务、释放锁、执行任务，再继续等待。
停止时拒绝新任务，执行完已接收任务，唤醒并 join 所有工作线程。
第一版不加入 future、packaged_task、可变参数模板、动态扩缩容或无锁队列；当前阶段已在第一版基础上加入 future、packaged_task 和模板版 Submit，并已扩展可变参数模板与完美转发，返回值不再限于 int。

## 原定路线与验收

实际学习已完成线程基础、任务队列与等待循环、固定 worker 和析构排空；已补线程数量和提交状态检查，阶段 5 的普通 Submit 任务异常隔离已完成，线程创建失败处理仍未完成；返回值扩展已完成模板版 Submit 和带参数任务提交，已加入任务统计与 RAII 活动计数保护，已完成统一 Submit 和四个状态查询接口，已补齐三态生命周期与并发 Shutdown，本阶段完成有界任务队列与构造参数校验。下表保留最初学习路线，实际进度以上述里程碑为准。

| 阶段 | 你要动手完成的内容 | 要掌握的知识 | 验收后提交信息 |
| --- | --- | --- | --- |
| 0（已完成） | 阅读路线，开始下面的练习 | 线程池的目标和学习顺序 | docs(thread-pool): initialize learning roadmap |
| 1 | 一个普通函数在子线程执行，主线程等待 | thread、函数作为入口、join、生命周期 | feat(thread-pool): complete thread and join exercise |
| 2 | 单线程任务队列；再练习两个线程安全更新计数 | queue、std::function<void()>、lambda、mutex、lock_guard | feat(thread-pool): complete queue and mutex exercises |
| 3 | 一个 worker 等待、取出并执行任务，可结束退出 | unique_lock、condition_variable、带条件的 wait、notify_one | feat(thread-pool): complete single worker loop |
| 4 | 封装 ThreadPool，构造时启动固定数量 worker | `vector<thread>`、构造与析构、共享状态、禁止复制 | feat(thread-pool): complete fixed size thread pool |
| 5（部分完成） | 已实现显式/析构优雅关闭与并发关闭验证，部分线程创建失败回收待完善 | 停止标志、notify_all、排空任务、join、资源释放 | feat(thread-pool): complete graceful shutdown |
| 6（已完成） | 从 int 专用接口扩展为泛型提交接口，获取不同类型任务结果 | C++17、invoke_result_t、packaged_task、future、shared_ptr | feat: generalize task submission with template futures |
| 7（已完成） | 带参数任务提交，支持不同返回类型和参数列表 | Args...、std::bind、std::forward、转发引用 | feat: support parameterized tasks with perfect forwarding |
| 8（已完成） | 等待任务数、活动任务数与普通任务异常隔离 | mutable mutex、lock_guard、atomic、try/catch | feat: track queued and active tasks |
| 9（已完成） | 使用 ActiveTaskGuard 自动管理活动任务计数 | RAII、引用成员、初始化列表、作用域与析构 | refactor: manage active task count with RAII |
| 10（已完成） | 统一 Submit，完善队列数、活动数、worker 数及运行状态查询 | 统一模板 API、const 查询、互斥锁与原子计数、对象生命周期 | refactor: unify submit API and add thread pool status queries |
| 11（已完成） | 三态生命周期、显式及并发 Shutdown、析构统一关闭 | State、条件变量谓词、关闭负责人、thread_local worker 身份 | feat: add thread pool shutdown state machine |
| 12（已完成） | 有界任务队列、构造参数校验、满队列拒绝 | maxQueueSize、std::invalid_argument、锁内容量检查、同步边界测试 | feat: add bounded task queue and input validation |

每阶段流程：你写代码 → 解释关键语句 → 一起检查并运行验收 → 更新这里的真实进度 → commit 并同步 GitHub。
未完成的练习不标记完成；阶段较大时可以保存明确注明 WIP 的进度，但不算验收完成。
任务统计与异常隔离已完成第八阶段，RAII 计数保护已完成第九阶段，统一提交与状态查询已完成第十阶段；后续继续补齐空任务拒绝与部分线程创建失败的回收，第十一阶段已完成显式 Shutdown，第十二阶段已完成有界队列、参数校验和满队列拒绝，动态扩缩容仍未实现。

## 有界任务队列与构造参数校验

构造接口为 `ThreadPool(int threadpoolCount, std::size_t queueSize = 100)`。`queueSize` 通过初始化列表保存到成员 `maxQueueSize`，只限制队列中等待的任务数，不包含 worker 已取出或正在执行的任务；容量固定，不随提交动态增长。

- `threadpoolCount <= 0`：抛出 `std::invalid_argument("threadpoolCount must be greater than 0")`。
- `queueSize == 0`：抛出 `std::invalid_argument("queueSize must be greater than 0")`。
- 两项校验都在创建 worker 前完成；两者同时非法时，先报告线程数错误。
- `Submit` 在 `mtx` 保护下先检查运行状态，再检查 `tasks.size() >= maxQueueSize`。队列满时立即抛出 `std::runtime_error("ThreadPool task queue is full")`，被拒绝的任务不会入队或执行，调用方也不会取得该次提交的 future。
- 当前满队列策略只支持抛异常，不等待空位；worker 出队释放容量后可以继续提交。关闭中或关闭后仍优先抛出 `std::runtime_error("ThreadPool has stopped")`。

```cpp
ThreadPool defaultPool(3); // 3 个 worker，默认等待队列容量 100
ThreadPool smallPool(1, 2); // 1 个 worker，最多 2 个任务等待
// ThreadPool invalidThreads(0); // 抛出 std::invalid_argument
// ThreadPool invalidQueue(3, 0); // 抛出 std::invalid_argument
```

本阶段验证（2026-09-20）：使用 clang++ / C++17 / pthread，开启 Wall/Wextra/Werror/pedantic 严格编译无警告，37/37 测试通过，连续运行 10 次及写回后重新编译运行均通过；提交前再次严格编译并运行，37/37 通过。

- 仓库测试覆盖线程数 0、-1、-3，零队列容量及同时非法参数的异常类型和消息；停止状态回归改用显式 Shutdown 后仍存活的对象。
- 容量 1、2 的测试通过 promise 同步阻塞 worker，再填满等待队列，验证正常提交、满队列重复拒绝、被拒绝任务不执行，以及释放容量后继续提交；不依赖 sleep 猜测满队列时机。
- 本地附加边界测试覆盖 INT_MIN；线程创建接口监测确认非法构造期间调用次数为 0，正常创建 1 个 worker 的对照为 1。附加探针不计入仓库的 37 项测试。

## 三态生命周期与显式关闭

正常生命周期为 `State::Running → State::ShuttingDown → State::Stopped`，不支持重新启动；线程数 <= 0 或 queueSize == 0 时构造抛出 std::invalid_argument，不会得到可查询的线程池对象。

- `Running`：Submit 在锁内检查状态并入队；worker 在队列为空时等待 `condition`。
- `ShuttingDown`：拒绝新任务，但 worker 继续处理已接受任务，只有队列为空且不再 Running 时才退出。
- `Stopped`：所有 worker 已 join，队列和活动数均为 0；GetWorkerCount 仍返回构造的固定线程数，不表示存活线程数。

第一个 Shutdown 调用者在 `mtx` 内将 Running 改为 ShuttingDown，解锁后 `condition.notify_all()` 并负责 join。后来者通过 `shutdownCondition.wait(lock, predicate)` 等待 Stopped，等待期间释放锁，醒来直接返回，不重复 join。负责人 join 完毕后重新加锁写入 Stopped，再解锁并 `shutdownCondition.notify_all()`。两个条件变量分别服务于 worker 等任务和外部调用者等关闭完成。

`~ThreadPool(){ Shutdown(); }` 复用同一路径。重复 Shutdown 是幂等的，且每个外部调用正常返回时，已接受任务都已处理完。各状态访问由同一把 mtx 保护，join 时不持有这把锁。

worker 用 `thread_local` 指针标记自己所属的线程池；Shutdown 在状态等待之前检查该标记，本池 worker 调用会抛出 `std::runtime_error("worker thread cannot call Shutdown")`，可在任务内捕获或由 future.get() 接收。这样不需要读取正在被另一个线程 join 的 std::thread 对象，也避免 worker 在关闭中等待自身退出。任务仍不得销毁自身线程池；并发 Shutdown 不意味着可以并发析构，对象必须活到所有外部调用结束。

```cpp
ThreadPool pool(3);
auto result = pool.Submit([](int n) { return n * 2; }, 21);
pool.Shutdown(); // 等任务完成并回收所有 worker
int value = result.get(); // 42
pool.Shutdown(); // 已关闭，直接返回
// pool.Submit([] {}); // 抛出 runtime_error，线程池已停止
```

三态关闭验证（2026-09-19）：C++17 严格编译（Wall/Wextra/Werror/pedantic）无警告，31/31 测试通过。保留 26 项既有回归，新增显式/重复关闭、关闭中及关闭后拒绝提交、两个外部调用等待队列排空、Running 与 ShuttingDown 时 worker 自调用拒绝，以及 50 轮四线程同时关闭。析构排空、四项状态查询、future 和带参数任务均通过。阻塞任务使用 promise 放行，等待行为通过有限观察窗口检查；测试不访问已析构对象。修复前的可编译旧关闭逻辑在“后来者等待”测试中失败，修复后通过。

附加 ThreadSanitizer 检查：原版在 macOS 标准库 `std::cout` 的并发创建日志路径报告两条竞争告警；在临时副本中仅去掉该日志后，31/31 测试通过且无检测器告警。正式代码保留日志，因此不宣称原版通过完整 TSan 检查。

## 模板任务提交与返回值

`Submit(F&& task, Arges&&... arges)` 接收任务和参数，返回 `std::future<std::invoke_result_t<F, Arges...>>`。`Arges...` 是代码中的类型参数包名称，通常也写作 `Args...`；空参数包仍支持原来的无参任务：

1. `std::invoke_result_t<F, Arges...>` 推导任务的返回类型 `ReturnType`，因此当前编译标准需要 C++17。
2. `std::bind(std::forward<F>(task), std::forward<Arges>(arges)...)` 将函数和参数绑定成无参任务；`F&&` 与 `Arges&&...` 为转发引用，`std::forward` 保留传入 bind 时的值类别。
3. 将 boundTask 用 `std::move` 移入 `std::packaged_task<ReturnType()>`，避免拷贝仅可移动绑定对象；通过 `get_future()` 获取对应的 `std::future<ReturnType>`。
4. packaged_task 不可拷贝，使用 `std::shared_ptr` 持有它，再由捕获该指针的可拷贝 lambda 包装成 `void()` 任务，放入现有队列。
5. worker 执行包装任务，结果或异常写入共享状态；调用方通过 `future.get()` 等待并取得结果，或接收任务异常。

```cpp
ThreadPool pool(3);
auto done = pool.Submit([] { /* 执行无返回值任务 */ });
done.get(); // 等待完成；任务抛异常时在这里重新抛出
auto integer = pool.Submit([] { return 42; });
auto decimal = pool.Submit([] { return 3.25; });
auto text = pool.Submit([] { return std::string("thread pool"); });

int i = integer.get();
double d = decimal.get();
std::string s = text.get();
```

示例需包含 `ThreadPool.h` 和 `<string>`。带参数任务可以直接提交：

```cpp
int add(int a, int b) { return a + b; }

// 在调用函数中：
ThreadPool pool(3);
auto sum = pool.Submit(add, 10, 20); // sum.get() == 30
std::string prefix = "hello";
auto text = pool.Submit([](std::string a, std::string b) {
    return a + " " + b;
}, prefix, std::string("pool")); // text.get() == "hello pool"
```

`std::bind` 默认按值保存衰减后的参数：左值拷贝、右值可移动；要修改原对象，请显式使用 `std::ref`，并保证原对象活到任务结束。完美转发发生在构造绑定对象时，bind 执行时通常把保存的普通参数作为左值传递，因此当前设计不支持所有仅接受右值引用的任务，也不能直接把绑定的 unique_ptr 按值移交给任务；可让任务接收其 const 引用，或使用捕获所有权的 lambda。队列与 worker 设计保持不变。

## 状态查询与异常隔离

- `GetTaskCount() const`：只统计仍在队列中等待的任务，使用 `mutable std::mutex` 支持 const 查询时加锁。
- `GetActiveCount() const`：读取原子活动计数，包含所有 Submit 任务执行；worker 创建局部 guard 时 `++`，正常完成或 catch 处理后离开作用域，由 guard 析构执行 `--`。
- `GetWorkerCount() const`：返回 `workers.size()`；固定线程数设计下，构造完成后容器不再增删，存活对象的正常查询无需额外加锁。它不是空闲线程数，也不是活动任务数。
- `IsRunning() const`：与 Submit 和 Shutdown 使用同一把 `mtx`，在锁内判断 `state == State::Running`；true 表示仍接受任务，不代表当前有任务执行。关闭中、关闭完成时均为 false；无效构造参数会直接抛异常。可在显式 Shutdown 后查询仍存活的对象，不能在析构开始后查询。
- 四个查询都支持 const 对象，是瞬时观察，不构成联合快照。worker 出队、解锁后才增加活动数，因此两个数不能作为“全部任务已经完成”的判断；应使用 future、Shutdown 或析构等待。future 就绪也可能早于 worker 的减计数，测试会另外等待活动数归零。
- 所有任务（包括 void）的标准异常和未知异常均通过 `future.get()` 传递；忽略 future 就不会观察到其中的任务异常。worker 原有 try/catch 保留为外层保护。
- `ActiveTaskGuard` 持有 `std::atomic<std::size_t>&`，通过构造函数初始化列表绑定线程池的计数器；构造时 `activeCount++`，析构时 `activeCount--`。worker 在任务执行的小作用域内创建 `ActiveTaskGuard guard(this->activeCount)`，不再手动配对递增和递减。RAII 负责计数清理；任务异常保存在 future 中，worker 的 try/catch 保留为外层保护。

## 基础复习：创建并等待一个线程

在本目录新建 stage01_thread.cpp，自己完成以下要求：

1. 定义普通函数 void printNumbers()，用 for 循环输出 1 到 5。
2. 在 main 中创建一个 std::thread，让它执行 printNumbers。
3. 主线程调用 join() 等待它完成。
4. join() 返回后，主线程输出 done。

只需要 iostream 和 thread 两个头文件。先不引入 lambda、共享计数器或线程池类。
提示：传给线程的是函数本身；不要把“立即调用函数”误当成“提供线程入口”。

验收：程序输出 1 到 5，最后输出 done，并正常退出。数值可以逐行输出。
完成后用自己的话回答：
- printNumbers 和 main 分别由哪个线程执行？
- join 等待的是谁？为什么 done 一定在最后？
- 如果线程对象销毁时仍可 join，会有什么问题？

## 当前版本编译与运行

VS Code 请直接打开 cpp-thread-pool 文件夹。Cmd+Shift+B 运行默认的 build thread pool；命令面板选择 Tasks: Run Task → run thread pool 会先构建再运行。F5 选择“运行线程池”会先构建再调试。不要选择“生成活动文件”任务来构建本多文件项目。

当前 VS Code 配置针对 macOS，使用 /usr/bin/clang++、LLDB 和 Microsoft C/C++ 扩展；其他系统需按安装位置调整工具链。

macOS / Linux 也可以在仓库根目录执行：

```sh
c++ -std=c++17 -Wall -Wextra -pedantic -pthread main.cpp ThreadPool.cpp -o main
./main
```

Windows 可将两个 cpp 和头文件加入同一个 Visual Studio C++ 控制台项目，使用 C++17。main.cpp 是唯一入口，不要只编译它。

成功时最后输出“测试结束：37/37 通过”，退出码为 0；worker 的创建日志允许交错。

## 后续阶段的正确性要求

- 阶段 2：两个线程各递增 10000 次，join 后检查计数为 20000；每次共享读写都由同一把 mutex 保护。
- 阶段 3：wait 使用条件判断，避免虚假唤醒导致从空队列取任务；等待时释放锁，取完任务后解锁再执行。
- 阶段 4：用同步方式验证多个 worker 可以同时进入任务，不能只靠输出顺序或 sleep 判断并发；每个任务恰好执行一次。
- 阶段 5：检查空池停止、带待处理任务停止、停止后提交被拒绝、重复停止和析构回收。
- 队列和停止标志始终由同一把 mutex 保护；停止且队列为空时 worker 才退出。
- 外部线程可显式调用 Shutdown；本池 worker 调用会抛异常，任务内不得销毁自身线程池。销毁对象前须等待所有外部调用结束。重复和并发关闭已纳入测试。
- 空任务与零线程数要明确拒绝；任务抛异常需在 worker 内捕获并报告，避免异常逃出线程入口。
- 创建部分线程后若后续创建失败，也要通知并回收已启动线程；这部分放到阶段 5 讲解。

## 协作约定

提交只包含本教学项目的相关文件，不混入可执行文件、构建目录和 IDE 临时文件。
在阶段 1 完成前，不创建完整线程池答案。提交历史记录实际学习阶段，文档保留下一步和验收标准。
