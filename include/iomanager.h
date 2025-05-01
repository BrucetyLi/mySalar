
#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "scheduler.h"
#include "timer.h"

namespace sylar {

/**
 * @brief 基于Epoll的IO协程调度器
 * IO协程调度解决了调度器在idle状态下忙等待导致CPU占用率高的问题。
 * IO协程调度器使用一对管道fd来tickle调度协程，当调度器空闲时，idle协程通过epoll_wait阻塞在管道的读描述符上，等管道的可读事件
 * 添加新任务时，tickle方法写管道，idle协程检测到管道可读后退出，调度器执行调度。
 * 这些事件库的实现原理基本类似，都是先将套接字设置成非阻塞状态，然后将套接字与回调函数绑定，接下来进入一个基于IO多路复用的事件循环，等待事件发生，然后调用对应的回调函数。
 * IO协程调度器在idle时会epoll_wait所有注册的fd，如果有fd满足条件，epoll_wait返回，从私有数据中拿到fd的上下文信息，并且执行其中的回调函数。
 * 实际是idle协程只负责收集所有已触发的fd的回调函数并将其加入调度器的任务队列，真正的执行时机是idle协程退出后，调度器在下一轮调度时执行
 */
class IOManager : public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager> ptr;
    typedef RWMutex RWMutexType;

    /**
     * @brief IO事件
     * 这里只关心socket fd的读和写事件，其他epoll事件会归类到这两类事件中
     */
    enum Event {
        /// 无事件
        NONE    = 0x0,
        /// 读事件(EPOLLIN)
        READ    = 0x1,
        /// 写事件(EPOLLOUT)
        WRITE   = 0x4,
    };
private:
    /**
     * @brief Socket文件描述符上下文
     * 描述符-事件类型-回调函数三元组
     * 由于fd有可读和可写两种事件，每种事件的回调函数也可以不一样，所以每个fd都需要保存两个事件类型-回调函数组合
     */
    struct FdContext {
        typedef Mutex MutexType;
        /**
         * @brief 事件上下文类
         */
        struct EventContext {
            /// 事件执行的调度器
            Scheduler* scheduler = nullptr;
            /// 事件协程
            Fiber::ptr fiber;
            /// 事件的回调函数
            std::function<void()> cb;
        };

        /**
         * @brief 获取事件上下文类
         * @param[in] event 事件类型
         * @return 返回对应事件的上线文
         */
        EventContext& getContext(Event event);

        /**
         * @brief 重置事件上下文
         * @param[in, out] ctx 待重置的上下文类
         */
        void resetContext(EventContext& ctx);

        /**
         * @brief 触发事件
         * @param[in] event 事件类型
         * 根据事件类型调用对应上下文结构中的调度器去调度回调协程或回调函数
         */
        void triggerEvent(Event event);

        /// 读事件上下文
        EventContext read;
        /// 写事件上下文
        EventContext write;
        /// 事件关联的句柄
        int fd = 0;
        /// 当前的事件
        Event events = NONE;
        /// 事件的Mutex
        MutexType mutex;
    };

public:
    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否将调用线程包含进去(也作为一个任务线程，和前面调度器类一致)
     * @param[in] name 调度器的名称
     */
    IOManager(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    ~IOManager();

    /**
     * @brief 添加事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @param[in] cb 事件回调函数
     * @return 添加成功返回0,失败返回-1
     */
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

    bool delEvent(int fd, Event event);

    /**
     * @brief 取消事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @attention 如果事件存在则触发事件
     */
    bool cancelEvent(int fd, Event event);

    /**
     * @brief 取消所有事件
     * @param[in] fd socket句柄
     */
    bool cancelAll(int fd);

    /**
     * @brief 返回当前的IOManager
     */
    static IOManager* GetThis();
protected:
    void tickle() override;  // 唤醒操作，判断是否有空闲线程(如果有则直接返回)，向队列中的第一个文件描述符做一个写操作来唤醒它
    bool stopping() override;
    void idle() override; // （待机操作）其实是epoll的监听:开启循环等待epoll_wait，监听是否有文件描述符被操作;一旦监听到，就切换对应协程上下文进行执行
    void onTimerInsertedAtFront() override;

    /**
     * @brief 重置socket句柄上下文的容器大小
     * @param[in] size 容量大小
     */
    void contextResize(size_t size);

    /**
     * @brief 判断是否可以停止
     * @param[out] timeout 最近要出发的定时器事件间隔
     * @return 返回是否可以停止
     */
    bool stopping(uint64_t& timeout);
private:
    /// epoll 文件句柄
    int m_epfd = 0;
    /// pipe 文件句柄
    int m_tickleFds[2];
    /// 当前等待执行的事件数量
    std::atomic<size_t> m_pendingEventCount = {0};
    /// IOManager的Mutex
    RWMutexType m_mutex;
    /// socket事件上下文的容器
    std::vector<FdContext*> m_fdContexts;
};

}

#endif
