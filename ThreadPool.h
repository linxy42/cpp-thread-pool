#pragma once
#include<vector>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<queue>


class ThreadPool{
public:
ThreadPool(int threadpoolCount);

bool Submit(const std::function<void()>& task);

~ThreadPool();

private:
std::vector<std::thread> workers;
std::mutex mtx;
std::condition_variable condition;
std::queue <std::function<void()>> tasks;
bool running=true;
};
