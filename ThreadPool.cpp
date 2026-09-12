#include <iostream>
#include <vector>
#include <thread>
using namespace std;

class ThreadPool {
public:
    ThreadPool(int threadpoolCount) {
        for (int i = 0; i < threadpoolCount; ++i) {
            works.emplace_back([i]() {
                cout << "创造线程:" << i << endl;
            });
        }
    }

    ~ThreadPool() {
        for (int i = 0; i < works.size(); ++i) {
            if (works[i].joinable()) {
                works[i].join();
            }
        }
    }

private:
    vector<thread> works;
};

int main() {
    ThreadPool(4);
    return 0;
}
