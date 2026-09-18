#pragma once
#include<vector>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<queue>
#include<future>
#include<type_traits>
#include <memory>
#include <utility>
#include <stdexcept>
#include <cstddef>
#include <atomic>

class ThreadPool{
public:
ThreadPool(int threadpoolCount);



template<typename F, typename... Arges>
std::future<std::invoke_result_t<F, Arges...>>
Submit(F&& task, Arges&&... arges)
{
    using ReturnType = std::invoke_result_t<F, Arges...>;

    auto boundTask = std::bind(
        std::forward<F>(task),
        std::forward<Arges>(arges)...
    );

    std::packaged_task<ReturnType()> Task(std::move(boundTask));
    std::future<ReturnType> result = Task.get_future();

    auto taskptr =
        std::make_shared<std::packaged_task<ReturnType()>>(std::move(Task));

    auto wrapperTask = [taskptr]()
    {
        (*taskptr)();
    };

    {
        std::lock_guard<std::mutex> lock(mtx);

        if (!running)
        {
            throw std::runtime_error("ThreadPool has stopped");
        }
        else{
        tasks.push(wrapperTask);
        }
    }

    condition.notify_one();
    return result;
}

std::size_t GetTaskCount ()const;

std::size_t GetActiveCount() const;

std::size_t GetWorkerCount() const;

bool IsRunning() const;

~ThreadPool();

private:
std::vector<std::thread> workers;
mutable std::mutex mtx;
std::condition_variable condition;
std::queue <std::function<void()>> tasks;
bool running=true;
std::atomic<std::size_t> activeCount{0};
};


class ActiveTaskGuard{
 public:
ActiveTaskGuard(std::atomic<std::size_t> &Count);

~ActiveTaskGuard();

private:
std::atomic<std::size_t> &activeCount;
};