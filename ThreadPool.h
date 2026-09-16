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

class ThreadPool{
public:
ThreadPool(int threadpoolCount);

bool Submit(const std::function<void()>& task);

std::future<int> SubmitInt(const std::function<int()>& task);

template<typename F,typename... Arges>
std::future<std::invoke_result_t<F,Arges...>> SubmitNew(F&& task,Arges&&...arges)
{
    using ReturnType = std::invoke_result_t<F,Arges...>;
    auto boundTask=std::bind(
                   std::forward<F>(task),
                   std::forward<Arges>(arges)...
    );
    std::packaged_task<ReturnType()>Task(std::move(boundTask));
    std::future<ReturnType> result=Task.get_future();
    std::unique_lock<std::mutex> lock(mtx);
    if(running){
        auto taskptr=std::make_shared<std::packaged_task<ReturnType()>>(std::move(Task));
    auto wrapperTask=[taskptr](){
        (*taskptr)();
    };
    tasks.push(wrapperTask);
    lock.unlock();
    condition.notify_one();
    return result;
    }
    else{
        throw std::runtime_error("ThreadPool has stopped");
    }
}




~ThreadPool();

private:
std::vector<std::thread> workers;
std::mutex mtx;
std::condition_variable condition;
std::queue <std::function<void()>> tasks;
bool running=true;
};
