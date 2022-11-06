#include <iostream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <chrono>
#include <random>
#include <future>
#include <array>
#include <queue>

#define NUM_THREADS 4

const int work_count = 100000; 
const int cycles_count = 10;

struct Timer
{
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
    std::chrono::duration<float> duration;
    std::string info;
public:
    Timer(std::string info):info(info)
    {
        start = std::chrono::high_resolution_clock::now();
    }
    ~Timer()
    {
        end = std::chrono::high_resolution_clock::now();
        duration = end - start;

        float ms = duration.count() * 1000.0f;
        std::cout<<info<<" : " << ms/cycles_count << " ms\n";
    }
};

// while loop
class ReusableThread
{
public:
    ReusableThread()
    {
        m_work_assigned = false;
        m_done = false;
        m_t = std::thread([this]()
        {
            while (!m_done)
            {
                while (!m_work_assigned && !m_done);
                if(!m_done) m_work();
                m_work_assigned = false;
            }
        });
    }
    void join()
    {
        m_done = true;
        m_t.join();
    }

    void post_work(std::function<void()> func)
    {
        m_work = func;
        m_work_assigned = true;
    }

    void wait()
    {
        while (m_work_assigned);
    }
private:
    std::thread m_t;
    std::function<void()> m_work;
    std::atomic_bool m_work_assigned;
    bool m_done;
};

// with sleep
class ReusableThreadSleep
{
public:
    ReusableThreadSleep()
    {
        m_work_assigned = false;
        m_done = false;
        m_t = std::thread([this]()
        {
            while (!m_done)
            {
                while (!m_work_assigned && !m_done)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(m_wait_time));
                }
                if(!m_done) m_work();
                m_work_assigned = false;
            }
        });
    }
    void join()
    {
        m_done = true;
        m_t.join();
    }

    void post_work(std::function<void()> func)
    {
        m_work = func;
        m_work_assigned = true;
    }

    void wait()
    {
        while (m_work_assigned)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(m_wait_time));
        }
    }
private:
    std::thread m_t;
    std::function<void()> m_work;
    std::atomic_bool m_work_assigned;
    bool m_done;
    const int m_wait_time = 1;
};

// wait and notify
class ReusableThreadWait
{
public:
    ReusableThreadWait()
    {
        m_work_assigned = false;
        m_done = false;
        m_t = std::thread([this]()
        {
            std::unique_lock<std::mutex> lck(m_mutex);
            while (!m_done)
            {
                while (!m_work_assigned && !m_done)
                {
                    m_cv.wait(lck);
                }
                if(!m_done) m_work();
                m_work_assigned = false;
                m_cv.notify_one();
            }
        });
    }
    void join()
    {
        m_done = true;
        m_cv.notify_one();
        m_t.join();
    }

    void post_work(std::function<void()> func)
    {
        m_work = func;
        m_work_assigned = true;
        m_cv.notify_one();
    }

    void wait()
    {
        std::unique_lock<std::mutex> lck(m_mutex);
        while (m_work_assigned)
        {
            m_cv.wait(lck);
        }
    }
private:
    std::thread m_t;
    std::function<void()> m_work;
    std::atomic_bool m_work_assigned;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done;
};

class ThreadPool
{
public:
    ThreadPool(unsigned int num_threads=0): m_task_count(0), m_join(false)
    {
        m_num_threads = (num_threads > 0) ? num_threads : std::thread::hardware_concurrency();
        for(unsigned int i=0; i<m_num_threads; i++)
        {
            m_threads.emplace_back(std::thread(ThreadPool::thread_work, this));
        }
    }

    void assign(std::function<void()> work)
    {
        m_queue_mutex.lock();
        m_work_queue.push(work);
        m_queue_mutex.unlock();
    }

    void join()
    {
        m_join = true;
        for(auto& t : m_threads)
        {
            if(t.joinable()) t.join();
        }
    }

    void wait_until(const unsigned int task_cout)
    {
        while (m_task_count < task_cout && !m_join) std::this_thread::yield();
        m_task_count = 0;
    }

    ~ThreadPool()
    {
        m_join = true;
        for(auto& t : m_threads)
        {
            if(t.joinable()) t.join();
        }
    }
private:
    static void thread_work(ThreadPool *threadPool)
    {
        std::function<void()> work;
        bool work_assigned = false;
        while (!(threadPool->m_join && threadPool->m_work_queue.empty()))
        {
            if(threadPool->m_work_queue.empty()) std::this_thread::yield();
            else
            {
                threadPool->m_queue_mutex.lock();
                if(!threadPool->m_work_queue.empty())
                {
                    work = threadPool->m_work_queue.front();
                    threadPool->m_work_queue.pop();
                    work_assigned = true;
                }
                threadPool->m_queue_mutex.unlock();
                if(work_assigned) 
                {
                    work();
                    threadPool->m_task_count++;
                    work_assigned = false;
                }
            }
        }
    }

    bool m_join = false;
    std::mutex m_queue_mutex;
    std::queue<std::function<void()>> m_work_queue;
    unsigned int m_num_threads;
    std::vector<std::thread> m_threads;
    unsigned int m_task_count;
};

void print_num(const int seed)
{
    int sum = 0;
    std::srand(seed);
    for(int i=0; i<work_count; i++)
    {
        sum += rand()%100;
    }
    // std::cout<<sum<<std::endl;
}

int main()
{
    {
        Timer t("Serial");
        for(int i=0; i<cycles_count; i++)
        {
            for(int j=0; j<NUM_THREADS; j++)
            {
                print_num(i+j);
            }
        }
    }

    {
        std::array<ReusableThread, NUM_THREADS> threads;
        Timer t("while loop");
        for(int i=0; i<cycles_count; i++)
        {
            for(int j=0; j<NUM_THREADS; j++)
            {
                // threads[j].post_work([&i, &j]{print_num(i+j);});
                threads[j].post_work([i, j]{print_num(i+j);});
            }
            for(auto& t: threads)
            {
                t.wait();
            }
        }
        for(auto& t: threads)
        {
            t.join();
        }
    }

    {
        std::array<ReusableThreadSleep, NUM_THREADS> threads;
        Timer t("with sleep");
        for(int i=0; i<cycles_count; i++)
        {
            for(int j=0; j<NUM_THREADS; j++)
            {
                threads[j].post_work([i, j]{print_num(i+j);});
            }
            for(auto& t: threads)
            {
                t.wait();
            }
        }
        for(auto& t: threads)
        {
            t.join();
        }
    }

    {
        ThreadPool threadPool(NUM_THREADS);
        Timer t("thread pool");
        for(int i=0; i<cycles_count; i++)
        {
            for(int j=0; j<NUM_THREADS; j++)
            {
                threadPool.assign([i, j]{print_num(i+j);});
            }
            threadPool.wait_until(NUM_THREADS);
        }
        threadPool.join();
    }

    {
        std::array<ReusableThreadWait, NUM_THREADS> threads;
        Timer t("wait and notify");
        for(int i=0; i<cycles_count; i++)
        {
            for(int j=0; j<NUM_THREADS; j++)
            {
                threads[j].post_work([i, j]{print_num(i+j);});
            }
            for(auto& t: threads)
            {
                t.wait();
            }
        }
        for(auto& t: threads)
        {
            t.join();
        }
    }

    {
        std::array<std::thread, NUM_THREADS> threads;
        Timer t("threads");
        for(int i=0; i<cycles_count; i++)
        {
            for(int j=0; j<NUM_THREADS; j++)
            {
                threads[j] = std::thread([i, j]{print_num(i+j);});
            }
            for(auto& t: threads)
            {
                t.join();
            }
        }
    }

    {
        std::array<std::future<void>, NUM_THREADS> futures;
        Timer t("async");
        for(int i=0; i<cycles_count; i++)
        {
            for(int j=0; j<NUM_THREADS; j++)
            {
                futures[j] = std::async(std::launch::async, [i, j](){print_num(i+j);});
            }
            for(auto& f: futures)
            {
                f.wait();
            }
        }
    }

    return 0;
}