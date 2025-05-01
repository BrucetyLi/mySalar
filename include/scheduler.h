
#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <memory>
#include <vector>
#include <list>
#include <iostream>
#include "fiber.h"
#include "thread.h"


// 个人理解，很像一个线程池，std::list<FiberAndThread> m_fibers就是任务队列，只不过每个任务函数被包装在一个协程里面，
// 如果任务队列空了，调度协程会不停地检测任务队列，看有没有新任务，俗称忙等待，CPU使用率爆表。
// idle协程在协程调度器未停止的情况下只会yield to hold，而调度协程又会将idle协程重新swapIn，相当于idle啥也不做直接返回
// 个人理解，当use_caller时  有三类协程，线程的主协程，线程的调度协程，线程的任务协程(会有多个)。
namespace sylar {

/**
 * @brief 协程调度器
 * @details 封装的是N-M的协程调度器
 *          内部有一个线程池,支持协程在线程池里面切换
 */
class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;

    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否使用当前调用线程
     * @param[in] name 协程调度器名称
     */
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    virtual ~Scheduler();

    const std::string& getName() const { return m_name;}
    static Scheduler* GetThis();

    /**
     * @brief 返回当前协程调度器的调度协程
     */
    static Fiber* GetMainFiber();
    
    void start();

    void stop();

    /**
     * @brief 调度协程
     * @param[in] fc 协程或函数
     * @param[in] thread 协程执行的线程id,-1标识任意线程
     */
    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            need_tickle = scheduleNoLock(fc, thread);
        }

        if(need_tickle) {
            tickle();
        }
    }

    /**
     * @brief 批量调度协程
     * @param[in] begin 协程数组的开始
     * @param[in] end 协程数组的结束
     */
    template<class InputIterator>
    void schedule(InputIterator begin, InputIterator end) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            while(begin != end) {
                need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
                ++begin;
            }
        }
        if(need_tickle) {
            tickle();
        }
    }

    void switchTo(int thread = -1);
    std::ostream& dump(std::ostream& os);
protected:
    /**
     * @brief 通知协程调度器有任务了
     */

    virtual void tickle();
    /**
     * @brief 协程调度函数
     */
    void run();

    /**
     * @brief 返回是否可以停止
     */
    virtual bool stopping();

    /**
     * @brief 协程无任务可调度时执行idle协程
     */
    virtual void idle();

    /**
     * @brief 设置当前的协程调度器
     */
    void setThis();

    /**
     * @brief 是否有空闲线程
     */
    bool hasIdleThreads() { return m_idleThreadCount > 0;}
private:
    /**
     * @brief 协程调度启动(无锁)
     */
    template<class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        bool need_tickle = m_fibers.empty();  // 目的是通知有新的协程来了。
        FiberAndThread ft(fc, thread);
        if(ft.fiber || ft.cb) {
            m_fibers.push_back(ft);
        }
        return need_tickle;
    }
private:
    /**
     * @brief 协程/函数/线程组
     */
    struct FiberAndThread {
        /// 协程
        Fiber::ptr fiber;
        /// 协程执行函数
        std::function<void()> cb;
        /// 线程id
        int thread;

        FiberAndThread(Fiber::ptr f, int thr)
            :fiber(f), thread(thr) {
        }

        FiberAndThread(Fiber::ptr* f, int thr)
            :thread(thr) {
            fiber.swap(*f);
        }

        FiberAndThread(std::function<void()> f, int thr)
            :cb(f), thread(thr) {
        }

        FiberAndThread(std::function<void()>* f, int thr)
            :thread(thr) {
            cb.swap(*f);
        }

        FiberAndThread()
            :thread(-1) {
        }

        void reset() {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };
private:
    /// Mutex
    MutexType m_mutex;
    /// 线程池
    std::vector<Thread::ptr> m_threads;
    /// 待执行的协程队列
    std::list<FiberAndThread> m_fibers;
    /// use_caller为true时有效, 调度协程
    Fiber::ptr m_rootFiber;
    /// 协程调度器名称
    std::string m_name;
protected:
    /// 协程下的线程id数组
    std::vector<int> m_threadIds;
    /// 线程数量
    size_t m_threadCount = 0;
    /// 工作线程数量
    std::atomic<size_t> m_activeThreadCount = {0};
    /// 空闲线程数量
    std::atomic<size_t> m_idleThreadCount = {0};
    /// 是否正在停止
    bool m_stopping = true;
    /// 是否自动停止
    bool m_autoStop = false;
    /// 主线程id(use_caller)
    int m_rootThread = 0;
};

class SchedulerSwitcher : public Noncopyable {
public:
    SchedulerSwitcher(Scheduler* target = nullptr);
    ~SchedulerSwitcher();
private:
    Scheduler* m_caller;
};

}

#endif
