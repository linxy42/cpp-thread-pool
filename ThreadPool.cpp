#include<iostream>
#include<vector>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<queue>
using namespace std;

class ThreadPool{
public:
ThreadPool(int threadpoolCount){
for(int i=0;i<threadpoolCount;++i){
    workers.emplace_back([i,this](){
        cout<<"创造线程:"<<i<<endl;
        while(true){
            unique_lock<mutex> lock(this->mtx);
            while(this->tasks.empty()&&judgment){
                this->condition.wait(lock);
            }
            if(this->tasks.empty()&&!judgment){
            break;
            }
            else{
            function<void()> temporary;
            temporary= this->tasks.front();
            this->tasks.pop();
            lock.unlock();
            temporary();
            }
        }
    });
}
}

void Submit(const function<void()>& task){
    unique_lock<mutex> lock(mtx);
    tasks.push(task);
    lock.unlock();
    condition.notify_one();
}

~ThreadPool(){
     {
        unique_lock<mutex> lock(mtx);
     judgment=false;
     }
    condition.notify_all();
    for(int i=0;i<workers.size();++i){
    workers[i].join();
}
}

private:
vector<thread> workers;
mutex mtx;
condition_variable condition;
queue <function<void()>> tasks;
bool judgment=true;
};


int main()
{
    ThreadPool pool(3);

    for (int i = 0; i < 10; ++i)
    {
        pool.Submit([i]() {
            cout << "执行任务：" << i << endl;
        });
    }

    return 0;
}