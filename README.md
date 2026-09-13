# c++线程池

独立 GitHub 仓库的教学项目：`linxy42/cpp-thread-pool`。项目中文名称为“c++线程池”。

## 当前进度

阶段 0、第一和第二个代码里程碑已完成。当前完成第三个代码里程碑：线程数量校验与提交状态检查。

本次保留「制定线程池项目」中学习者确认的第三版实现（NumberJudgment 版本），只整理排版和聊天转义；该版本未附新的 main，因此沿用上一版 3 个 worker、10 个任务的演示入口。

新增能力：
- `NumberJudgment()` 检查线程数；零或负数时输出提示，不创建 worker，并将 `running` 设为 false。
- 状态名由 `judgment` 改为 `running`，明确表达线程池是否运行。
- `Submit()` 在锁内检查状态：接受任务返回 true；不运行时返回 false，不入队。
- 保留原有等待唤醒、解锁执行和析构排空逻辑。

验证结果（C++11，2026-09-13）：0、-3、INT_MIN 三种非法线程数均连续拒绝 3 次提交，任务没有执行，且正常析构；1 和 3 个 worker 的线程池分别接受并在析构前将 1000 个任务各执行一次；空的正常线程池也能析构退出。
编译仍保留一处 `int` 与 `workers.size()` 比较的符号警告，供后续学习调整。

使用边界：目前没有独立 Stop 接口，不能调用已经开始析构或已销毁对象的 Submit。状态检查不替代对象生命周期管理；调用方仍须先停止并等待所有提交线程，再销毁线程池。任务中不得销毁线程池。
已知待完善项：空任务、任务抛异常、部分线程创建失败时的回收。非法线程数现在产生一个不接收任务的对象，不抛异常。
下一步按学习进度拆分头文件、实现文件和 main，并继续补齐基础边界；本次不加入 future 或 packaged_task。

先由学习者写代码，再一起检查、验证、提交；不提前填完后续答案。

起点依据：你在另一个仓库 cpp-learing 中的 StudentManagement2.0 已使用类、构造函数、vector、引用和文件读写。
项目最初以 thread 和 join 为起点，目前已推进到线程数量校验、提交状态检查和析构排空。

## 第一版的边界

采用 C++11 标准库，固定数量的工作线程。最终以 queue<std::function<void()>> 保存无参数、无返回值任务；
先学习普通函数，再介绍 lambda 和 std::function，不要求自己编写模板。
提交任务后唤醒工作线程；工作线程等待任务、取任务、释放锁、执行任务，再继续等待。
停止时拒绝新任务，执行完已接收任务，唤醒并 join 所有工作线程。
第一版不加入 future、packaged_task、可变参数模板、动态扩缩容或无锁队列。

## 原定路线与验收

实际学习已完成线程基础、任务队列与等待循环、固定 worker 和析构排空；已补线程数量和提交状态检查，阶段 5 的任务异常及线程创建失败处理仍未完成。下表保留最初学习路线，实际进度以上述里程碑为准。

| 阶段 | 你要动手完成的内容 | 要掌握的知识 | 验收后提交信息 |
| --- | --- | --- | --- |
| 0（已完成） | 阅读路线，开始下面的练习 | 线程池的目标和学习顺序 | docs(thread-pool): initialize learning roadmap |
| 1 | 一个普通函数在子线程执行，主线程等待 | thread、函数作为入口、join、生命周期 | feat(thread-pool): complete thread and join exercise |
| 2 | 单线程任务队列；再练习两个线程安全更新计数 | queue、std::function<void()>、lambda、mutex、lock_guard | feat(thread-pool): complete queue and mutex exercises |
| 3 | 一个 worker 等待、取出并执行任务，可结束退出 | unique_lock、condition_variable、带条件的 wait、notify_one | feat(thread-pool): complete single worker loop |
| 4 | 封装 ThreadPool，构造时启动固定数量 worker | `vector<thread>`、构造与析构、共享状态、禁止复制 | feat(thread-pool): complete fixed size thread pool |
| 5 | 完成停止边界和验证 | 停止标志、notify_all、排空任务、join、资源释放 | feat(thread-pool): complete graceful shutdown |

每阶段流程：你写代码 → 解释关键语句 → 一起检查并运行验收 → 更新这里的真实进度 → commit 并同步 GitHub。
未完成的练习不标记完成；阶段较大时可以保存明确注明 WIP 的进度，但不算验收完成。
之后是否学习返回值和泛化，由实际掌握情况决定。

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

## 当前版本编译方式

### Windows / Visual Studio

之前的学生管理项目使用 Visual Studio 工程，可继续用熟悉的环境。
新建独立的 C++ 控制台项目，加入 ThreadPool.cpp，确保项目中只有一个 main。
使用支持 C++11 及以上的编译器；现代 MSVC 的默认 C++14 模式即可。用 Ctrl+F5 运行。

### macOS / Linux

在本目录执行：

```sh
c++ -std=c++11 -Wall -Wextra -pedantic -pthread ThreadPool.cpp -o /tmp/thread_pool_stage03
/tmp/thread_pool_stage03
```

运行时创建 3 个 worker 并提交编号 0～9 的 10 个任务。main 不 sleep，pool 在作用域结束时析构，处理完已提交任务后退出。输出顺序不保证，文字可能交错。

## 后续阶段的正确性要求

- 阶段 2：两个线程各递增 10000 次，join 后检查计数为 20000；每次共享读写都由同一把 mutex 保护。
- 阶段 3：wait 使用条件判断，避免虚假唤醒导致从空队列取任务；等待时释放锁，取完任务后解锁再执行。
- 阶段 4：用同步方式验证多个 worker 可以同时进入任务，不能只靠输出顺序或 sleep 判断并发；每个任务恰好执行一次。
- 阶段 5：检查空池停止、带待处理任务停止、停止后提交被拒绝、重复停止和析构回收。
- 队列和停止标志始终由同一把 mutex 保护；停止且队列为空时 worker 才退出。
- 第一版 stop 由拥有线程池的主线程调用；任务内不调用 stop，也不销毁线程池，避免线程 join 自身。
- 空任务与零线程数要明确拒绝；任务抛异常需在 worker 内捕获并报告，避免异常逃出线程入口。
- 创建部分线程后若后续创建失败，也要通知并回收已启动线程；这部分放到阶段 5 讲解。

## 协作约定

提交只包含本教学项目的相关文件，不混入可执行文件、构建目录和 IDE 临时文件。
在阶段 1 完成前，不创建完整线程池答案。提交历史记录实际学习阶段，文档保留下一步和验收标准。
