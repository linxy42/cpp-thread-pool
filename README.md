# c++线程池

独立 GitHub 仓库的教学项目：`linxy42/cpp-thread-pool`。项目中文名称为“c++线程池”。

## 当前进度

当前已在多文件结构、SubmitInt 与 future<int> 的基础上，完成第六阶段：模板版 `SubmitNew(F task)`，支持 `int`、`double`、`std::string` 等不同返回类型。当前项目使用 **C++17**。

已实现能力：

- 固定数量的 worker 线程池，使用 `std::queue<std::function<void()>>` 保存任务。
- 使用 `std::mutex` 保护任务队列和运行状态，配合 `std::condition_variable` 等待与唤醒；worker 取出任务后释放锁，再执行任务。
- 析构时设置停止状态，唤醒所有 worker，执行完已接收的任务，再逐一 `join()`，实现优雅停止。
- `Submit` 支持无参数的 `void` 任务：接受任务返回 `true`，线程池不运行时返回 `false`。
- `SubmitNew` 自动推导返回类型，返回对应的 `std::future`；线程池不运行时抛出 `std::runtime_error`。原有 `SubmitInt` 接口仍保留。

项目结构：

- `ThreadPool.h`：类声明、成员和模板版 `SubmitNew` 的完整定义，使用 pragma once 防止重复包含；模板定义放在头文件中，供调用处实例化。
- `ThreadPool.cpp`：构造函数、worker 循环、`Submit`、`SubmitInt` 和析构函数的实现。
- `main.cpp`：15 个独立测试，每组输出 PASS/FAIL；失败返回非零退出码。
- `.vscode/tasks.json`：默认任务同时编译 main.cpp 和 ThreadPool.cpp；run 依赖 build。
- `.vscode/launch.json`：启动前执行完整构建，调试生成的 main。
- `.gitignore`：忽略可执行文件、目标文件和 macOS 调试产物。

验证（2026-09-13）：C++17 多文件编译通过，开启 Wall/Wextra/Werror/pedantic 无警告；5/5 测试通过。
测试分别覆盖空池析构、0 和 -3 拒绝重复提交、1 和 3 个 worker 将 20 个任务各执行一次。每个池离开作用域后才核对结果，主线程不通过 sleep 猜测完成时间。此测试覆盖多 worker 下的正确性，不测吞吐量或证明并行加速。

目前没有独立 Stop 接口，调用方须在析构开始前停止并等待所有提交线程；任务不得销毁自身线程池。未处理空任务、void Submit 任务异常、部分线程创建失败；这些边界仍待完善。`SubmitInt` 和 `SubmitNew` 的任务异常已由 packaged_task 保存，并在 `future.get()` 时重新抛出。
SubmitInt 阶段验证（2026-09-16）：C++17 严格编译无警告，11/11 测试通过。新增覆盖单个 int 返回值、100 个 int 任务、void/int 混合提交、执行中及排队 int 任务的析构排空、停止池重复提交异常，以及任务异常经 future 传递后 worker 继续执行。停止路径用 0 和 -3 个线程构造的存活对象验证，不在析构后调用成员函数。

模板阶段验证（2026-09-16）：C++17 严格编译无警告，15/15 测试通过。在原有 11 项基础上，新增不同返回值与对应 future 类型检查、Submit/SubmitNew 混合提交及析构排空、停止池重复提交异常、任务异常经 future 传递后 worker 继续执行。

下一步按学习进度完善空任务、普通 Submit 任务异常和部分线程创建失败时的回收；独立 Stop 接口、可变参数模板和动态扩缩容尚未实现。

先由学习者写代码，再一起检查、验证、提交；不提前填完后续答案。

起点依据：你在另一个仓库 cpp-learing 中的 StudentManagement2.0 已使用类、构造函数、vector、引用和文件读写。
项目最初以 thread 和 join 为起点，目前已推进到线程数量校验、提交状态检查、析构排空和模板任务返回值。

## 第一版的边界

最初第一版采用 C++11 标准库，固定数量的工作线程；当前版本使用 C++17。最终以 queue<std::function<void()>> 保存无参数、无返回值任务；
先学习普通函数，再介绍 lambda 和 std::function，不要求自己编写模板。
提交任务后唤醒工作线程；工作线程等待任务、取任务、释放锁、执行任务，再继续等待。
停止时拒绝新任务，执行完已接收任务，唤醒并 join 所有工作线程。
第一版不加入 future、packaged_task、可变参数模板、动态扩缩容或无锁队列；当前阶段已在第一版基础上加入 future、packaged_task 和模板版 SubmitNew，返回值不再限于 int。

## 原定路线与验收

实际学习已完成线程基础、任务队列与等待循环、固定 worker 和析构排空；已补线程数量和提交状态检查，阶段 5 的普通 Submit 任务异常及线程创建失败处理仍未完成；返回值扩展已完成模板版 SubmitNew。下表保留最初学习路线，实际进度以上述里程碑为准。

| 阶段 | 你要动手完成的内容 | 要掌握的知识 | 验收后提交信息 |
| --- | --- | --- | --- |
| 0（已完成） | 阅读路线，开始下面的练习 | 线程池的目标和学习顺序 | docs(thread-pool): initialize learning roadmap |
| 1 | 一个普通函数在子线程执行，主线程等待 | thread、函数作为入口、join、生命周期 | feat(thread-pool): complete thread and join exercise |
| 2 | 单线程任务队列；再练习两个线程安全更新计数 | queue、std::function<void()>、lambda、mutex、lock_guard | feat(thread-pool): complete queue and mutex exercises |
| 3 | 一个 worker 等待、取出并执行任务，可结束退出 | unique_lock、condition_variable、带条件的 wait、notify_one | feat(thread-pool): complete single worker loop |
| 4 | 封装 ThreadPool，构造时启动固定数量 worker | `vector<thread>`、构造与析构、共享状态、禁止复制 | feat(thread-pool): complete fixed size thread pool |
| 5（部分完成） | 已实现析构排空与回收，继续完善停止边界和验证 | 停止标志、notify_all、排空任务、join、资源释放 | feat(thread-pool): complete graceful shutdown |
| 6（已完成） | 从 SubmitInt 扩展为模板版 SubmitNew，获取不同类型任务结果 | C++17、invoke_result_t、packaged_task、future、shared_ptr | feat: generalize task submission with template futures |

每阶段流程：你写代码 → 解释关键语句 → 一起检查并运行验收 → 更新这里的真实进度 → commit 并同步 GitHub。
未完成的练习不标记完成；阶段较大时可以保存明确注明 WIP 的进度，但不算验收完成。
返回值与模板泛化已推进到第六阶段，后续扩展仍按实际掌握情况决定。

## 模板任务提交与返回值

`SubmitNew(F task)` 接收一个可无参数调用的任务，返回 `std::future<std::invoke_result_t<F>>`：

1. `std::invoke_result_t<F>` 推导任务的返回类型 `ReturnType`，因此当前编译标准需要 C++17。
2. `std::packaged_task<ReturnType()>` 包装任务，通过 `get_future()` 获取对应的 `std::future<ReturnType>`。
3. packaged_task 不可拷贝，使用 `std::shared_ptr` 持有它，再由捕获该指针的可拷贝 lambda 包装成 `void()` 任务，放入现有队列。
4. worker 执行包装任务，结果或异常写入共享状态；调用方通过 `future.get()` 等待并取得结果，或接收任务异常。

```cpp
ThreadPool pool(3);
auto integer = pool.SubmitNew([] { return 42; });
auto decimal = pool.SubmitNew([] { return 3.25; });
auto text = pool.SubmitNew([] { return std::string("thread pool"); });

int i = integer.get();
double d = decimal.get();
std::string s = text.get();
```

示例需包含 `ThreadPool.h` 和 `<string>`。当前接口不直接接收额外参数，需要时可通过 lambda 捕获参数；尚未实现可变参数与完美转发。

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

成功时最后输出“测试结束：15/15 通过”，退出码为 0；worker 的创建日志允许交错。

## 后续阶段的正确性要求

- 阶段 2：两个线程各递增 10000 次，join 后检查计数为 20000；每次共享读写都由同一把 mutex 保护。
- 阶段 3：wait 使用条件判断，避免虚假唤醒导致从空队列取任务；等待时释放锁，取完任务后解锁再执行。
- 阶段 4：用同步方式验证多个 worker 可以同时进入任务，不能只靠输出顺序或 sleep 判断并发；每个任务恰好执行一次。
- 阶段 5：检查空池停止、带待处理任务停止、停止后提交被拒绝、重复停止和析构回收。
- 队列和停止标志始终由同一把 mutex 保护；停止且队列为空时 worker 才退出。
- 当前由拥有线程池的线程触发析构并回收 worker；任务内不销毁线程池，避免线程 join 自身。若后续增加独立 Stop 接口，再验证重复停止等行为。
- 空任务与零线程数要明确拒绝；任务抛异常需在 worker 内捕获并报告，避免异常逃出线程入口。
- 创建部分线程后若后续创建失败，也要通知并回收已启动线程；这部分放到阶段 5 讲解。

## 协作约定

提交只包含本教学项目的相关文件，不混入可执行文件、构建目录和 IDE 临时文件。
在阶段 1 完成前，不创建完整线程池答案。提交历史记录实际学习阶段，文档保留下一步和验收标准。
