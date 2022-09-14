#include <iostream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <chrono>
#include <random>
#include <future>
#include <array>

#define NUM_THREADS 2

const int work_count = 100000; 
const int cycles_count = 100;

struct Timer
{
private:
    std::chrono::time_point<std::chrono::steady_clock> start, end;
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
    }

    return 0;
}