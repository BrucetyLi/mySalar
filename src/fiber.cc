#include "fiber.h"
#include "config.h"
#include "macro.h"
#include "log.h"
#include "scheduler.h"
#include <atomic>

namespace sylar {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static std::atomic<uint64_t> s_fiber_id {0};
static std::atomic<uint64_t> s_fiber_count {0};

//线程局部变量，当前线程正在运行的协程
static thread_local Fiber* t_fiber = nullptr;
//线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行，智能指针形式
static thread_local Fiber::ptr t_threadFiber = nullptr;

//配合配置系统指定协程的 栈大小
static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
    Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");

//统一的方法来处理协程栈空间的分配
class MallocStackAllocator {
public:
    static void* Alloc(size_t size) {
        return malloc(size);
    }

    static void Dealloc(void* vp, size_t size) {
        return free(vp);
    }
};

using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId() {
    if(t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}


Fiber::Fiber() {
    m_state = EXEC;
    SetThis(this);

    if(getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }

    ++s_fiber_count;

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber main";
}

//创建子协程
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller)
    :m_id(++s_fiber_id)
    ,m_cb(cb) {
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();

    m_stack = StackAllocator::Alloc(m_stacksize);

     //获取当前上下文
    if(getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }
    //没有关联上下文（如果有关联上下文，自身结束时会回到关联上下文执行）
    m_ctx.uc_link = nullptr;
    //指定上下文起始指针
    m_ctx.uc_stack.ss_sp = m_stack;
    //指定栈大小
    m_ctx.uc_stack.ss_size = m_stacksize;
    //创建上下文
    if(!use_caller) {
        makecontext(&m_ctx, &Fiber::MainFunc, 0);
    } else {
        makecontext(&m_ctx, &Fiber::CallerMainFunc, 0);
    }

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << m_id;
}

Fiber::~Fiber() {
    --s_fiber_count;
    if(m_stack) { //子协程会有m_stack
        SYLAR_ASSERT(m_state == TERM
                || m_state == EXCEPT
                || m_state == INIT);
        //回收栈
        StackAllocator::Dealloc(m_stack, m_stacksize);
    } else { //主协程不会有m_stack
        SYLAR_ASSERT(!m_cb);
        SYLAR_ASSERT(m_state == EXEC);

        Fiber* cur = t_fiber;
        if(cur == this) {
            SetThis(nullptr);
        }
    }
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::~Fiber id=" << m_id
                              << " total=" << s_fiber_count;
}

//重置协程函数，并重置状态
//INIT，TERM, EXCEPT
void Fiber::reset(std::function<void()> cb) {
    SYLAR_ASSERT(m_stack);
    SYLAR_ASSERT(m_state == TERM
            || m_state == EXCEPT
            || m_state == INIT);
    m_cb = cb;
    if(getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = INIT;
}

void Fiber::call() {
    SetThis(this);
    m_state = EXEC;
    if(swapcontext(&t_threadFiber->m_ctx, &m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

// 个人理解是由任务协程到 线程的主协程
void Fiber::back() {
    SetThis(t_threadFiber.get());
    if(swapcontext(&m_ctx, &t_threadFiber->m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

//切换到当前协程执行(一般是由主协程切换到子协程)
void Fiber::swapIn() {
    SetThis(this);
    SYLAR_ASSERT(m_state != EXEC);
    m_state = EXEC;
    if(swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

//切换到后台执行 (一般是由子协程切换到主协程). 个人理解是由任务协程到线程的调度协程
void Fiber::swapOut() {
    SetThis(Scheduler::GetMainFiber());
    if(swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

//设置当前协程
void Fiber::SetThis(Fiber* f) {
    t_fiber = f;
}

//返回当前协程
Fiber::ptr Fiber::GetThis() {
    if(t_fiber) {
        return t_fiber->shared_from_this();
    }
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_threadFiber = main_fiber;
    return t_fiber->shared_from_this();
}

//协程切换到后台，并且设置为Ready状态
void Fiber::YieldToReady() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur->m_state == EXEC);
    cur->m_state = READY;
    cur->swapOut();
}

//协程切换到后台，并且设置为Hold状态
void Fiber::YieldToHold() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur->m_state == EXEC);
    //cur->m_state = HOLD;
    cur->swapOut();
}

//总协程数
uint64_t Fiber::TotalFibers() {
    return s_fiber_count;
}

void Fiber::MainFunc() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
            << " fiber_id=" << cur->getId()
            << std::endl;
        //    << sylar::BacktraceToString();
    } catch (...) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except"
            << " fiber_id=" << cur->getId()
            << std::endl;
        //    << sylar::BacktraceToString();
    }

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->swapOut();

    SYLAR_ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
}

void Fiber::CallerMainFunc() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
            << " fiber_id=" << cur->getId()
            << std::endl;
        //    << sylar::BacktraceToString();
    } catch (...) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except"
            << " fiber_id=" << cur->getId()
            << std::endl;
        //    << sylar::BacktraceToString();
    }

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->back();  // 和MainFunc的不同之处！！！！！！！
    SYLAR_ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));

}

}
