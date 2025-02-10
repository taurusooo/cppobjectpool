#ifndef __CPPOBJECTPOOL_HPP__
#define __CPPOBJECTPOOL_HPP__

#include <iostream>
#include <mutex>
#include <memory>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>
#include <queue>
#include <chrono>

namespace cppobjectpool
{
// 为 C++11 提供手动实现的 index_sequence 和 make_index_sequence
// 如果使用 C++14 或更高版本，则直接使用标准库中的实现
#if __cplusplus < 201402L
    template <std::size_t... Ints>
    struct index_sequence
    {
    }; // 用于展开参数包的索引序列

    template <std::size_t N, std::size_t... Ints>
    struct make_index_sequence : make_index_sequence<N - 1, N - 1, Ints...>
    {
    }; // 递归生成索引序列

    template <std::size_t... Ints>
    struct make_index_sequence<0, Ints...> : index_sequence<Ints...>
    {
    }; // 基本情况

#else
#include <utility> // C++14 及以上版本包含此头文件
    using std::index_sequence;      // 使用标准库中的 index_sequence
    using std::make_index_sequence; // 使用标准库中的 make_index_sequence
#endif

    // 对象池模板类
    template <typename T, typename... Args>
    class ObjectPool
    {
    public:
        // 定义预处理、后处理和最终处理函数的类型
        using PreProcess = std::function<void(std::shared_ptr<T>)>;
        using PostProcess = std::function<void(std::shared_ptr<T>)>;
        using FinalProcess = std::function<void(T *)>;

        // 构造函数，初始化对象池
        ObjectPool(size_t initialSize = 10,                             // 初始对象数量
                   size_t maxSize = std::numeric_limits<size_t>::max(), // 最大对象数量
                   PreProcess preProcess = nullptr,                     // 预处理函数
                   PostProcess postProcess = nullptr,                   // 后处理函数
                   FinalProcess finalProcess = nullptr,                 // 最终处理函数
                   Args &&...args);                                     // 构造函数参数

        // 构造函数，初始化对象池
        ObjectPool(size_t initialSize = 10,                             // 初始对象数量
            size_t maxSize = std::numeric_limits<size_t>::max(), // 最大对象数量
            Args &&...args);                                     // 构造函数参数


        // 析构函数，清理资源
        ~ObjectPool();

        // 获取对象
        std::shared_ptr<T> acquire();
        // 释放对象，可指定延迟释放时间
        void release(std::shared_ptr<T> obj, std::chrono::milliseconds delay = std::chrono::milliseconds(0));
        // 获取当前可用对象数量
        size_t getAvailableCount() const;
        // 清理对象池
        void clear();

        // 设置处理函数(非线程安全)
        inline void setPreProcess(PreProcess preProcess)
        {
            m_preProcess = preProcess;
        };
        inline void setPostProcess(PostProcess postProcess)
        {
            m_postProcess = postProcess;
        };
    
        inline void setFinalProcess(FinalProcess finalProcess)
        {
            m_finalProcess = finalProcess;
        };

    private:
        // 延迟释放对象的结构
        struct DelayedObject
        {
            std::chrono::steady_clock::time_point expiry; // 到期时间
            std::shared_ptr<T> obj;                       // 对象指针
            bool operator<(const DelayedObject &other) const
            {
                return expiry > other.expiry; // 小顶堆，按到期时间排序
            }
        };

        // 延迟释放对象的优先队列
        std::priority_queue<DelayedObject> m_delayedQueue;
        // 清理线程
        std::thread m_cleanup_thread;
        // 标记是否运行
        std::atomic<bool> m_running;

        // 清理任务函数
        void cleanupTask();
        // 处理已到期的对象
        void processExpiredObjects();

        // 互斥锁，保护对象池
        mutable std::mutex m_mutex;
        mutable std::mutex m_delayMutex;
        // 对象池，存储可用对象
        std::vector<std::shared_ptr<T>> m_pool;
        // 最大对象数量
        size_t m_maxSize;
        // 预处理函数
        PreProcess m_preProcess;
        // 后处理函数
        PostProcess m_postProcess;
        // 最终处理函数
        FinalProcess m_finalProcess;
        // 当前分配的对象数量
        std::atomic<size_t> m_allocedCount{0};
        // 构造函数参数
        std::tuple<Args...> m_constructorArgs;

        // 创建对象的函数
        template <std::size_t... Is>
        std::shared_ptr<T> createObject(index_sequence<Is...>)
        {
            return std::shared_ptr<T>(new T(std::get<Is>(m_constructorArgs)...), [this](T *ptr)
                                      {
                                          if (this->m_finalProcess)
                                          {
                                              this->m_finalProcess(ptr); // 调用最终处理函数
                                          }
                                          --m_allocedCount; // 减少分配计数
                                          delete ptr;       // 删除对象
                                      });
        }

        // 创建对象的辅助函数
        std::shared_ptr<T> createObject()
        {
            return createObject(make_index_sequence<sizeof...(Args)>{}); // 使用索引序列展开参数包
        }
    };

    // 构造函数实现
    template <typename T, typename... Args>
    ObjectPool<T, Args...>::ObjectPool(size_t initialSize, size_t maxSize,
                                       PreProcess preProcess, PostProcess postProcess, FinalProcess finalProcess,
                                       Args &&...args)
        : m_maxSize(maxSize), m_preProcess(preProcess), m_postProcess(postProcess), m_finalProcess(finalProcess), m_running(true),
          m_constructorArgs(std::forward<Args>(args)...) // 初始化构造函数参数
    {
        try
        {
            for (size_t i = 0; i < initialSize; ++i)
            {
                auto obj = createObject(); // 创建初始对象
                m_pool.emplace_back(obj);  // 将对象放入对象池
                ++m_allocedCount;          // 增加分配计数
            }
            m_cleanup_thread = std::thread(&ObjectPool<T, Args...>::cleanupTask, this); // 启动清理线程
        }
        catch (const std::bad_alloc &e)
        {
            std::cerr << "Memory allocation failed in constructor: " << e.what() << std::endl; // 捕获内存分配失败异常
            throw;
        }
    }


    // 构造函数实现
    template <typename T, typename... Args>
    ObjectPool<T, Args...>::ObjectPool(size_t initialSize, size_t maxSize,
                                       Args &&...args)
        : m_maxSize(maxSize), m_running(true),
          m_constructorArgs(std::forward<Args>(args)...) // 初始化构造函数参数
    {
        try
        {
            for (size_t i = 0; i < initialSize; ++i)
            {
                auto obj = createObject(); // 创建初始对象
                m_pool.emplace_back(obj);  // 将对象放入对象池
                ++m_allocedCount;          // 增加分配计数
            }
            m_cleanup_thread = std::thread(&ObjectPool<T, Args...>::cleanupTask, this); // 启动清理线程
        }
        catch (const std::bad_alloc &e)
        {
            std::cerr << "Memory allocation failed in constructor: " << e.what() << std::endl; // 捕获内存分配失败异常
            throw;
        }
    }

    // 析构函数实现
    template <typename T, typename... Args>
    ObjectPool<T, Args...>::~ObjectPool()
    {
        m_running = false; // 停止清理线程
        if (m_cleanup_thread.joinable())
        {
            m_cleanup_thread.join(); // 等待清理线程结束
        }
        try
        {
            clear(); // 清理对象池
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception occurred during destruction: " << e.what() << std::endl; // 捕获析构过程中的异常
        }
    }

    // 获取对象
    template <typename T, typename... Args>
    std::shared_ptr<T> ObjectPool<T, Args...>::acquire()
    {
        std::shared_ptr<T> obj;
        {
            std::lock_guard<std::mutex> lock(m_mutex); // 加锁保护对象池
            if (!m_pool.empty())
            {
                obj = std::move(m_pool.back()); // 获取可用对象
                m_pool.pop_back();
            }
            else if (m_allocedCount < m_maxSize)
            {
                obj = createObject(); // 创建新对象
                ++m_allocedCount;
            }
        }

        if (obj && m_preProcess)
        {
            m_preProcess(obj); // 调用预处理函数
        }
        return obj;
    }

    // 释放对象
    template <typename T, typename... Args>
    void ObjectPool<T, Args...>::release(std::shared_ptr<T> obj, std::chrono::milliseconds delay)
    {
        if (!obj)
            return; // 如果对象为空，直接返回

        if (delay.count() == 0)
        {
            if (m_postProcess)
            {
                m_postProcess(obj); // 调用后处理函数
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex); // 加锁保护对象池
                if (m_pool.size() < m_maxSize)
                {
                    m_pool.emplace_back(std::move(obj)); // 将对象放回对象池
                }
            }
        }
        else
        {
            auto expiry = std::chrono::steady_clock::now() + delay; // 计算到期时间
            {
                std::lock_guard<std::mutex> lock(m_delayMutex);                // 加锁保护延迟队列
                m_delayedQueue.emplace(DelayedObject{expiry, std::move(obj)}); // 将对象加入延迟队列
            }
        }
    }

    // 获取当前可用对象数量
    template <typename T, typename... Args>
    size_t ObjectPool<T, Args...>::getAvailableCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex); // 加锁保护对象池
        return m_pool.size();
    }

    // 清理对象池
    template <typename T, typename... Args>
    void ObjectPool<T, Args...>::clear()
    {

        try
        {
            {
                std::lock_guard<std::mutex> lock(m_delayMutex); // 加锁保护对象池
                while (!m_delayedQueue.empty())
                {
                    auto obj = std::move(m_delayedQueue.top().obj); // 获取延迟队列中的对象
                    m_delayedQueue.pop();
                    if (m_postProcess)
                    {
                        m_postProcess(obj); // 调用后处理函数
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pool.clear();
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception occurred in clear: " << e.what() << std::endl; // 捕获清理过程中的异常
        }
    }

    // 清理任务
    template <typename T, typename... Args>
    void ObjectPool<T, Args...>::cleanupTask()
    {
        while (m_running.load()) // 检查是否运行
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 定期检查
            processExpiredObjects();                                     // 处理已到期的对象
        }
    }

    // 处理已到期的对象
    template <typename T, typename... Args>
    void ObjectPool<T, Args...>::processExpiredObjects()
    {
        std::vector<std::shared_ptr<T>> expiredObjects;
        {
            {
                std::lock_guard<std::mutex> lock(m_delayMutex); // 加锁保护延迟队列
                auto now = std::chrono::steady_clock::now();    // 获取当前时间
                while (!m_delayedQueue.empty() && m_delayedQueue.top().expiry <= now)
                {
                    expiredObjects.emplace_back(std::move(m_delayedQueue.top().obj)); // 获取已到期的对象
                    m_delayedQueue.pop();
                }
            }
        }

        for (auto &obj : expiredObjects)
        {
            if (m_postProcess)
            {
                m_postProcess(obj); // 调用后处理函数
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex); // 加锁保护对象池
                if (m_pool.size() < m_maxSize)
                {
                    m_pool.emplace_back(std::move(obj)); // 将对象放回对象池
                }
            }
        }
    }
}
#endif // OBJECTPOOL_H
#include <iostream>

class MyObject
{
public:
    MyObject(int value) : data(value)
    {
        std::cout << "MyObject created with value: " << data << std::endl;
    }
    ~MyObject()
    {
        std::cout << "MyObject destroyed with value: " << data << std::endl;
    }
    int data;
};

// 预处理函数
void preProcess(std::shared_ptr<MyObject> obj)
{
    std::cout << "Pre-processing object with value: " << obj->data << std::endl;
}

// 后处理函数
void postProcess(std::shared_ptr<MyObject> obj)
{
    std::cout << "Post-processing object with value: " << obj->data << std::endl;
}

// 最终处理函数
void finalProcess(MyObject *ptr)
{
    std::cout << "Final processing object with value: " << ptr->data << std::endl;
}

int main()
{
    // 创建初始大小2的对象池,设置pool中有效对象数量为5并进行有参构造
    cppobjectpool::ObjectPool<MyObject, int> pool(2, 5, 42);

    // 设置预处理、后处理和最终处理函数
    // preProcess： acquire 返回前执行
    // postProcess：release 返回前执行
    // finalProcess：shared_ptr 删除器中执行
    pool.setPreProcess(preProcess);
    pool.setPostProcess(postProcess);
    pool.setFinalProcess(finalProcess);

    // 获取对象
    auto obj1 = pool.acquire();
    auto obj2 = pool.acquire();

    // 立即回收到对象池
    pool.release(obj1);
    // 延迟 1s 回收到对象池
    pool.release(obj2, std::chrono::milliseconds(1000));

    // 清理对象池
    pool.clear();

    return 0;
}
