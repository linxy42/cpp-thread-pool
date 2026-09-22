#include "ThreadPool.h"
#include<iostream>
#include <stdexcept>


// 仅本线程读写，避免检查 worker 身份时与另一个线程的 join 竞争。
namespace {
thread_local const ThreadPool* currentWorkerPool = nullptr;
}


ThreadPool::ThreadPool(int threadCount,std::size_t queueSize,RejectPolicy policy):maxQueueSize(queueSize),rejectPolicy(policy){
   if(threadCount<=0){
   throw std::invalid_argument("threadpoolCount must be greater than 0");
   }
   if (queueSize == 0){
    throw std::invalid_argument("queueSize must be greater than 0");
}
for(int i=0;i<threadCount;++i){
    workers.emplace_back([this](){
        currentWorkerPool = this;
        while(true){
            std::unique_lock<std::mutex> lock(this->mtx);
            while(this->tasks.empty()&&state==State::Running){
                this->condition.wait(lock);
            }
            if(this->tasks.empty()&&state!=State::Running){
            currentWorkerPool = nullptr;
            break;
            }
            else{
            std::function<void()> task;
            task= this->tasks.front();
            this->tasks.pop();
            lock.unlock();
                {
            ActiveTaskGuard guard(this->activeCount);
            try{
            task();
            }
            catch(const std::exception& error){
                std::cerr<< "任务执行异常: " << error.what()<<std::endl;
            }
            catch(...){
                std::cerr<<"任务执行发生未知异常" << std::endl;
            }
                }
            }
        }
    });
}
}

std::size_t ThreadPool::GetTaskCount() const
{
        std::lock_guard<std::mutex> lock(mtx);
        return tasks.size();
}

std::size_t ThreadPool::GetActiveCount() const{
    return activeCount.load();
}

std::size_t ThreadPool::GetWorkerCount() const{
    return workers.size();
}

bool ThreadPool::IsRunning() const{
    std::lock_guard<std::mutex> lock(mtx);
    return state==State::Running;
}

void ThreadPool::Shutdown(){
    if(currentWorkerPool == this){
        throw std::runtime_error("worker thread cannot call Shutdown");
    }

   std::unique_lock<std::mutex> lock(mtx);
    if(state==State::Stopped){
        return;
    }else if(state==State::Running){
        state=State::ShuttingDown;
    }
    else if(state==State::ShuttingDown){
        shutdownCondition.wait(lock, [this]() {
    return state == State::Stopped;
});
        return;
    }
        lock.unlock();
    condition.notify_all();
    for(std::size_t i=0;i<workers.size();++i){
        if(workers[i].joinable()){
            workers[i].join();
        }
    }
    {
    std::lock_guard<std::mutex> lock(mtx);
    state = State::Stopped;
}

shutdownCondition.notify_all();
}

ThreadPool::~ThreadPool(){
     Shutdown();
}


ActiveTaskGuard::ActiveTaskGuard(std::atomic<std::size_t> &count):activeCount(count){
    activeCount++;
}

ActiveTaskGuard::~ActiveTaskGuard(){
    activeCount--;
}
