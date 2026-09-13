#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <queue>
using namespace std;

bool NumberJudgment(int threadpoolCount) {
    if (threadpoolCount <= 0) {
        cout << "线程创建数量异常，请重试" << endl;
        return false;
    }
    return true;
}

class ThreadPool {
public:
    ThreadPool(int threadpoolCount) {
        if (!NumberJudgment(threadpoolCount)) {
            running = false;
            return;
        }
        for (int i = 0; i < threadpoolCount; ++i) {
            workers.emplace_back([i, this]() {
                cout << "创造线程:" << i << endl;
                while (true) {
                    unique_lock<mutex> lock(this->mtx);
                    while (this->tasks.empty() && running) {
                        this->condition.wait(lock);
                    }
                    if (this->tasks.empty() && !running) {
                        break;
                    } else {
                        function<void()> temporary;
                        temporary = this->tasks.front();
                        this->tasks.pop();
                        lock.unlock();
                        temporary();
                    }
                }
            });
        }
    }

    bool Submit(const function<void()>& task) {
        unique_lock<mutex> lock(mtx);
        if (running) {
            tasks.push(task);
            lock.unlock();
            condition.notify_one();
            return true;
        } else {
            return false;
        }
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(mtx);
            running = false;
        }
        condition.notify_all();
        for (int i = 0; i < workers.size(); ++i) {
            workers[i].join();
        }
    }

private:
    vector<thread> workers;
    mutex mtx;
    condition_variable condition;
    queue<function<void()>> tasks;
    bool running = true;
};

int main() {
    ThreadPool pool(3);
    for (int i = 0; i < 10; ++i) {
        pool.Submit([i]() {
            cout << "执行任务：" << i << endl;
        });
    }
    return 0;
}
