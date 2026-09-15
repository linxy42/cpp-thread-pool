#include "ThreadPool.h"
#include<iostream>
#include <stdexcept>


bool NumberJudgment(int threadpoolCount){

    if(threadpoolCount<=0){
    std::cout<<"线程创建数量异常，请重试"<<std::endl;
    return false;
}
    return true;
}

ThreadPool::ThreadPool(int threadpoolCount){
   if(!NumberJudgment(threadpoolCount)){
    running=false;
    return;
   }
for(int i=0;i<threadpoolCount;++i){
    workers.emplace_back([i,this](){
        std::cout<<"创造线程:"<<i<<std::endl;
        while(true){
            std::unique_lock<std::mutex> lock(this->mtx);
            while(this->tasks.empty()&&running){
                this->condition.wait(lock);
            }
            if(this->tasks.empty()&&!running){
            break;
            }
            else{
            std::function<void()> temporary;
            temporary= this->tasks.front();
            this->tasks.pop();
            lock.unlock();
            temporary();
            }
        }
    });
}
}

 bool ThreadPool::Submit(const std::function<void()>& task){
    std::unique_lock<std::mutex> lock(mtx);
    if(running){
    tasks.push(task);
    lock.unlock();
    condition.notify_one();
    return true;
    }
    else{
        return false;
    }
}

std::future<int> ThreadPool::SubmitInt(const std::function<int()>&task){
    std::packaged_task<int()>Task(task);
   std::future<int> result=Task.get_future();
   std::unique_lock<std::mutex> lock(mtx);
   if(running){
    auto taskptr=std::make_shared<std::packaged_task<int()>>(std::move(Task));
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

ThreadPool::~ThreadPool(){
     {
        std::unique_lock<std::mutex> lock(mtx);
     running=false;
     }
    condition.notify_all();
    for(std::size_t i=0;i<workers.size();++i){
        if(workers[i].joinable()){
        workers[i].join();
        }
}
}
