#ifndef __CPPOBJECTPOOL_HPP__
#define __CPPOBJECTPOOL_HPP__

// 引入必要的标准库头文件
#include <iostream>
#include <mutex>
#include <memory>
#include <functional>
#include <vector>
#include <atomic>
#include <utility>
#include <unordered_set>

// 定义命名空间 cppobjectpool
namespace cppobjectpool
{
    // 如果编译器不支持 C++14 及以上标准
#if __cplusplus < 201402L
    // 定义索引序列结构体，用于模板元编程
    template <std::size_t... Ints>
    struct index_sequence {};

    // 递归模板，用于生成索引序列
    template <std::size_t N, std::size_t... Ints>
    struct make_index_sequence : make_index_sequence<N - 1, N - 1, Ints...> {};

    // 递归终止条件，当 N 为 0 时
    template <std::size_t... Ints>
    struct make_index_sequence<0, Ints...> : index_sequence<Ints...> {};
    // 如果编译器支持 C++14 及以上标准
#else
    // 直接使用标准库中的 index_sequence 和 make_index_sequence
#include <utility>
    using std::index_sequence;
    using std::make_index_sequence;
#endif

    // 定义对象池模板类，T 是对象的类型，Args 是对象构造函数的参数类型
    template <typename T, typename... Args>
    class ObjectPool : public std::enable_shared_from_this<ObjectPool<T, Args...>>
    {
    public:
        // 定义预处理函数类型，用于在获取对象前对对象进行处理
        using PreProcess = std::function<void(T*)>;
        // 定义后处理函数类型，用于在释放对象后对对象进行处理
        using PostProcess = std::function<void(T*)>;
        // 定义最终处理函数类型，用于在对象被销毁前对对象进行处理
        using FinalProcess = std::function<void(T*)>;
    public:
        // 自定义删除器结构体，用于在对象释放时将其放回对象池
        struct CustomDeleter
        {
            // 弱引用指向对象池，避免循环引用
            std::weak_ptr<ObjectPool<T, Args...>> pool_weak;
            // 存储最终处理函数
            FinalProcess finalProcess;
            // 重载函数调用运算符，当对象被删除时调用
            void operator()(T* ptr)
            {
                // 尝试锁定弱引用，获取对象池的共享指针
                if (auto pool = pool_weak.lock())
                {
                    // std::cout << "operator()" << std::endl;
                    // 如果成功获取对象池的共享指针，将对象放回对象池
                    pool->release(std::unique_ptr<T, CustomDeleter>(ptr, *this));
                }
                else
                {
                    if (finalProcess)
                    {
                        // 如果最终处理函数不为空，调用最终处理函数
                        finalProcess(ptr);
                    }
                    delete ptr;
                    ptr = nullptr;
                }
            }
        };

        int getRealAllockedCount()
        {
            return m_realAllocedCount;
        }

        // 静态工厂方法，用于创建对象池的共享指针
        static std::shared_ptr<ObjectPool> create(
            // 对象池的初始大小，默认为 10
            size_t initialSize = 10,
            // 对象池的最大大小，默认为 size_t 类型的最大值
            size_t maxSize = std::numeric_limits<size_t>::max(),
            // 对象构造函数的参数
            Args&&... args)
        {
            // 创建对象池的共享指针
            auto pool = std::shared_ptr<ObjectPool>(new ObjectPool(
                initialSize, maxSize, std::forward<Args>(args)...));
            return pool;
        }

        // 设置预处理函数
        void setPreProcess(PreProcess preProcess)
        {
            m_preProcess = preProcess;
        }

        // 设置后处理函数
        void setPostProcess(PostProcess postProcess)
        {
            m_postProcess = postProcess;
        }

        // 设置最终处理函数
        void setFinalProcess(FinalProcess finalProcess)
        {
            m_finalProcess = finalProcess;
            // 更新自定义删除器中的最终处理函数
            for (const auto& ptr : m_pool)
            {
                // 这里不需要额外处理，因为删除器在对象归还时才会起作用
            }
        }

        // 析构函数，用于清理对象池
        ~ObjectPool()
        {
            // 清空对象池
            clear();
        }

        // 获取对象的方法，返回一个智能指针
        std::unique_ptr<T, CustomDeleter> acquire()
        {
            std::unique_ptr<T> ptr;
            {
                // 加锁，保证线程安全
                std::lock_guard<std::mutex> lock(m_mutex);
                // 如果对象池不为空
                if (!m_pool.empty())
                {
                    // 从对象池的末尾取出一个对象
                    ptr = std::move(m_pool.back());
                    m_pool.pop_back();
                    // 从跟踪集合中移除该指针
                    m_releasedPtrs.erase(ptr.get());
                }
                // 如果对象池为空且已分配的对象数量小于最大大小
                else if (m_acquiredCount < m_maxSize)
                {
                    // 创建一个新对象
                    ptr = createObject();
                    // 增加已分配的对象数量
                    ++m_acquiredCount;
                }
            }

            // 如果成功获取到对象且预处理函数不为空
            if (ptr && m_preProcess)
            {
                // 调用预处理函数
                m_preProcess(ptr.get());
            }

            // 返回一个智能指针，使用自定义删除器
            return std::unique_ptr<T, CustomDeleter>(ptr.release(), CustomDeleter{ this->weak_from_this(), m_finalProcess });
        }

        // 获取对象池中空闲对象的数量
        size_t getAvailableCount() const
        {
            // 加锁，保证线程安全
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_pool.size();
        }

        // 清空对象池
        void clear()
        {
            {
                // 加锁，保证对对象池的操作线程安全
                std::lock_guard<std::mutex> lock(m_mutex);
                // 遍历对象池中的所有对象
                for (auto& ptr : m_pool)
                {
                    // 从跟踪集合中移除该指针
                    m_releasedPtrs.erase(ptr.get());
                    // 如果最终处理函数不为空
                    if (m_finalProcess)
                    {
                        // 调用最终处理函数
                        m_finalProcess(ptr.get());
                    }
                    // 对象会在离开作用域时自动删除
                }
                // 清空对象池
                m_pool.clear();
            }
            // 清空跟踪集合
            m_releasedPtrs.clear();
        }

        // 保护对象池的互斥锁
        mutable std::mutex m_mutex;
        // 存储空闲对象的向量，使用 std::unique_ptr
        std::vector<std::unique_ptr<T>> m_pool;
        // 对象池的最大大小
        size_t m_maxSize;
        // 预处理函数
        PreProcess m_preProcess;
        // 后处理函数
        PostProcess m_postProcess;
        // 最终处理函数
        FinalProcess m_finalProcess;
        // 已分配的对象数量(随回收变化)
        std::atomic<size_t> m_acquiredCount{ 0 };
        // 分配过的对象数量
        std::atomic<size_t> m_realAllocedCount{ 0 };
        // 存储对象构造函数参数的元组
        std::tuple<Args...> m_constructorArgs;
        // 跟踪已释放的对象指针
        std::unordered_set<T*> m_releasedPtrs;

        // 构造函数
        ObjectPool(size_t initialSize,
            size_t maxSize,
            Args&&... args)
            : m_maxSize(maxSize),
            m_constructorArgs(std::forward<Args>(args)...)
        {
            try
            {
                // 初始化对象池，创建指定数量的对象
                for (size_t i = 0; i < initialSize; ++i)
                {
                    auto obj = createObject();
                    m_pool.emplace_back(std::move(obj));
                    ++m_acquiredCount;
                }
            }
            catch (const std::bad_alloc& e)
            {
                // 处理内存分配失败的异常
                std::cerr << "Memory allocation failed: " << e.what() << std::endl;
                throw;
            }
        }

        // 创建对象的方法
        std::unique_ptr<T> createObject()
        {
            // 调用辅助函数创建对象
            auto ptr = createObjectHelper(make_index_sequence<sizeof...(Args)>{});
            ++m_realAllocedCount;
            return ptr;
        }

        // 辅助函数，用于展开构造函数参数
        template <size_t... Is>
        std::unique_ptr<T> createObjectHelper(index_sequence<Is...>)
        {
            // 使用元组中的参数创建对象
            return std::unique_ptr<T>(new T(std::get<Is>(m_constructorArgs)...));
        }

        // 释放对象的方法
        void release(std::unique_ptr<T, CustomDeleter> ptr)
        {
            // 如果对象指针为空，直接返回
            if (!ptr) return;

            // 获取原始指针
            T* rawPtr = ptr.release();

            // 加锁，保证对对象池的操作线程安全
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_releasedPtrs.count(rawPtr) > 0) return;
            m_releasedPtrs.insert(rawPtr);

            // 如果后处理函数不为空
            if (m_postProcess)
            {
                // 调用后处理函数
                m_postProcess(rawPtr);
            }

            // 如果对象池的大小小于最大大小
            if (m_pool.size() < m_maxSize)
            {
                // std::cout << "emplace_back" << std::endl;
                // 将对象放回对象池
                m_pool.emplace_back(std::unique_ptr<T>(rawPtr));
            }
            else
            {
                // 如果对象池已满，且最终处理函数不为空
                if (m_finalProcess)
                {
                    // 调用最终处理函数
                    m_finalProcess(rawPtr);
                }
                // 对象会在离开作用域时自动删除
                --m_realAllocedCount;
                // 从跟踪集合中移除该指针
                m_releasedPtrs.erase(rawPtr);
                delete rawPtr;
            }
        }
    };
}
#endif // __CPPOBJECTPOOL_HPP__
