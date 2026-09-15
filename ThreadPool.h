#pragma once
#include<vector>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<queue>
#include<future>


class ThreadPool{
public:
ThreadPool(int threadpoolCount);

bool Submit(const std::function<void()>& task);
std::future<int> SubmitInt(const std::function<int()>& task);

~ThreadPool();

private:
std::vector<std::thread> workers;
std::mutex mtx;
std::condition_variable condition;
std::queue <std::function<void()>> tasks;
bool running=true;
};
