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

const int work_count = 1000; 
const int cycles_count = 1000;

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

        float ms = duration.count() * 1000000.0f;
        std::cout<<info<<" : " << ms/cycles_count << " us\n";
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

class ThreadPool_Yield
{
public:
    ThreadPool_Yield(unsigned int num_threads=0): m_task_count(0), m_join(false)
    {
        m_num_threads = (num_threads > 0) ? num_threads : std::thread::hardware_concurrency();
        for(unsigned int i=0; i<m_num_threads; i++)
        {
            m_threads.emplace_back(std::thread(ThreadPool_Yield::thread_work, this));
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
        // wait only if task count not completed and join not called
        while (m_task_count < task_cout && !m_join) std::this_thread::yield();
        m_task_count = 0;
    }

    ~ThreadPool_Yield()
    {
        join();
    }
private:
    static void thread_work(ThreadPool_Yield *threadPool)
    {
        std::function<void()> work;
        bool work_assigned = false;
        std::unique_lock<std::mutex> queue_lck(threadPool->m_queue_mutex, std::defer_lock);
        while (!(threadPool->m_join && threadPool->m_work_queue.empty())) //break the loop if only join is called and queue is empty 
        {
            if(threadPool->m_work_queue.empty()) std::this_thread::yield();
            else
            {
                queue_lck.lock();
                if(!threadPool->m_work_queue.empty())
                {
                    work = threadPool->m_work_queue.front();
                    threadPool->m_work_queue.pop();
                    work_assigned = true;
                }
                queue_lck.unlock();
                if(work_assigned) 
                {
                    work();
                    threadPool->m_task_count++;
                    work_assigned = false;
                }
            }
        }
    }

    std::atomic_bool m_join;
    std::mutex m_queue_mutex;
    std::queue<std::function<void()>> m_work_queue;
    unsigned int m_num_threads;
    std::vector<std::thread> m_threads;
    std::atomic_uint16_t m_task_count;
};

class ThreadPool_Wait
{
public:
    ThreadPool_Wait(unsigned int num_threads=0): m_task_count(0), m_join(false)
    {
        m_num_threads = (num_threads > 0) ? num_threads : std::thread::hardware_concurrency();
        for(unsigned int i=0; i<m_num_threads; i++)
        {
            m_threads.emplace_back(std::thread(ThreadPool_Wait::thread_work, this));
        }
    }

    void assign(std::function<void()> work)
    {
        m_queue_mutex.lock();
        m_work_queue.push(work);
        m_queue_mutex.unlock();
        m_cv.notify_one();
    }

    void join()
    {
        m_join = true;
        m_cv.notify_all();
        for(auto& t : m_threads)
        {
            if(t.joinable()) t.join();
        }
    }

    void wait_until(const unsigned int task_cout)
    {
        // wait only if task count not completed and join not called
        while (m_task_count < task_cout && !m_join) std::this_thread::yield();
        m_task_count = 0;
    }

    ~ThreadPool_Wait()
    {
        join();
    }
private:
    static void thread_work(ThreadPool_Wait *threadPool)
    {
        std::function<void()> work;
        bool work_assigned = false;
        std::unique_lock<std::mutex> cv_lck(threadPool->m_cv_mutex, std::defer_lock);
        std::unique_lock<std::mutex> queue_lck(threadPool->m_queue_mutex, std::defer_lock);
        while (!(threadPool->m_join && threadPool->m_work_queue.empty())) //break the loop if only join is called and queue is empty 
        {
            cv_lck.lock();
            if(!threadPool->m_join && threadPool->m_work_queue.empty()) //call wait only if join is not called and queue is empty 
                threadPool->m_cv.wait(cv_lck, [&threadPool]()
                {return !(!threadPool->m_join && threadPool->m_work_queue.empty());}); //wait only if join is not called and queue is empty
            cv_lck.unlock();
 
            queue_lck.lock();
            if(!threadPool->m_work_queue.empty())
            {
                work = threadPool->m_work_queue.front();
                threadPool->m_work_queue.pop();
                work_assigned = true;
            }
            queue_lck.unlock();
            if(work_assigned) 
            {
                work();
                threadPool->m_task_count++;
                work_assigned = false;
            }
        }
    }
    std::vector<std::thread> m_threads;
    std::mutex m_queue_mutex;
    std::mutex m_cv_mutex;
    std::condition_variable m_cv;
    std::atomic_bool m_join;
    unsigned int m_num_threads;
    std::atomic_uint16_t m_task_count;
    std::queue<std::function<void()>> m_work_queue;
};

static std::atomic_int org_result = 0;
static std::atomic_int result = 0;
void print_num(const int seed)
{
    int sum = 0;
    std::srand(seed);
    for(int i=0; i<work_count; i++)
    {
        sum += rand()%100;
    }
    result += sum;
}

void comp_reset_results()
{
    if(result != org_result)
        std::cerr<<"Result mismatch\n";
    
    result = 0;
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
    org_result = result;
    result = 0;

/*
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
*/

    {
        ThreadPool_Yield threadPool(NUM_THREADS);
        Timer t("thread pool yield");
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
    comp_reset_results();

    {
        ThreadPool_Wait threadPool(NUM_THREADS);
        Timer t("thread pool wait");
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
    comp_reset_results();

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
    comp_reset_results();

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
    comp_reset_results();

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
    comp_reset_results();

    return 0;
}