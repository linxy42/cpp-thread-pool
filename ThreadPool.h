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


enum class RejectPolicy
{
    Abort,
    CallerRuns,
    Discard
};

class ActiveTaskGuard{
 public:
ActiveTaskGuard(std::atomic<std::size_t> &count);

~ActiveTaskGuard();

private:
std::atomic<std::size_t> &activeCount;
};

class ThreadPool{
public:
ThreadPool(int threadCount,std::size_t queueSize=100,RejectPolicy policy = RejectPolicy::Abort);

template<typename F, typename... Args>
std::future<std::invoke_result_t<F, Args...>>
Submit(F&& task, Args&&... args)
{
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto boundTask = std::bind(
        std::forward<F>(task),
        std::forward<Args>(args)...
    );

    std::packaged_task<ReturnType()> packagedTask(std::move(boundTask));
    std::future<ReturnType> result = packagedTask.get_future();

    auto taskPtr =
        std::make_shared<std::packaged_task<ReturnType()>>(std::move(packagedTask));

    auto wrapperTask = [taskPtr]()
    {
        (*taskPtr)();
    };
        bool callerRuns=false;
        bool discard = false;
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (state!=State::Running)
        {
            throw std::runtime_error("ThreadPool has stopped");
        }
        else{
            if(tasks.size()>=maxQueueSize){
                if (rejectPolicy == RejectPolicy::Abort){
                throw std::runtime_error("ThreadPool task queue is full");
                }
                else if(rejectPolicy == RejectPolicy::CallerRuns){
                    callerRuns=true;
                }
                else if(rejectPolicy == RejectPolicy::Discard){
                    discard=true;
                }
            }
            else{
                tasks.push(wrapperTask);
            }
        }
    }
    if(callerRuns){
        wrapperTask();
        return result;
    }
    if(discard){
        std::promise<ReturnType> rejectedPromise;
        std::future<ReturnType> rejectedFuture=rejectedPromise.get_future();
        rejectedPromise.set_exception(
                std::make_exception_ptr(
                    std::runtime_error("ThreadPool task was discarded")
                )
        );
        return rejectedFuture;
    }

    condition.notify_one();
    return result;
}

std::size_t GetTaskCount ()const;

std::size_t GetActiveCount() const;

std::size_t GetWorkerCount() const;

bool IsRunning() const;

void Shutdown();

~ThreadPool();

private:
enum class State
{
    Running,
    ShuttingDown,
    Stopped
};
std::vector<std::thread> workers;
mutable std::mutex mtx;
std::condition_variable condition;
std::queue <std::function<void()>> tasks;
State state=State::Running;
std::atomic<std::size_t> activeCount{0};
std::condition_variable shutdownCondition;
std::size_t maxQueueSize;
RejectPolicy rejectPolicy;
};
