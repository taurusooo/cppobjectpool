# cppobjectpool

## 📌 概述
`cppobjectpool::ObjectPool` 是一个基于 C++11+ 的通用对象池模板类。它提供了一种高效的方式来管理对象的创建和回收，减少动态内存分配的开销，同时支持：
- **对象的预处理、后处理和最终处理**
- **延迟回收机制**
- **线程安全**
---

## ✨ 特性
- **高效复用对象**，统一对象管理
- **线程安全**，支持多线程环境
- **支持回调**，可以在对象获取、释放和销毁时执行自定义操作
- **可限制最大池大小**，防止资源占用过多
- **支持延迟回收**，可以设定回收时间
---

## 🚀 使用示例

### 1️⃣ 定义对象类
```cpp
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
```
### 2️⃣ 定义预处理、后处理和最终处理函数
```cpp
void preProcess(std::shared_ptr<MyObject> obj)
{
    std::cout << "Pre-processing object with value: " << obj->data << std::endl;
}

void postProcess(std::shared_ptr<MyObject> obj)
{
    std::cout << "Post-processing object with value: " << obj->data << std::endl;
}

void finalProcess(MyObject *ptr)
{
    std::cout << "Final processing object with value: " << ptr->data << std::endl;
}
```
### 3️⃣ 创建对象池并使用
```cpp
#include "cppobjectpool.hpp"

int main()
{
    // 创建对象池：初始大小2，最大5，每个对象初始化值为42
    cppobjectpool::ObjectPool<MyObject, int> pool(2, 5, 42);

    // 设置回调函数
    pool.setPreProcess(preProcess);
    pool.setPostProcess(postProcess);
    pool.setFinalProcess(finalProcess);

    // 获取对象
    auto obj1 = pool.acquire();
    auto obj2 = pool.acquire();

    // 释放对象
    pool.release(obj1); // 立即回收
    pool.release(obj2, std::chrono::milliseconds(1000)); // 延迟 1s 回收

    // 清理对象池
    pool.clear();
    return 0;
}
```
## 🛠️ API 说明
### 1️⃣ 构造函数
```cpp
ObjectPool(size_t initialSize = 10, size_t maxSize = std::numeric_limits<size_t>::max(), Args&&... args);
```
initialSize：对象池初始大小 | maxSize：对象池最大大小 | args...：对象的构造参数

### 2️⃣ 获取对象
```cpp
std::shared_ptr<T> acquire();
```
获取一个对象，如果池中有可用对象，则返回，否则创建新的对象（受 maxSize 限制）。
### 3️⃣ 释放对象
```cpp
void release(std::shared_ptr<T> obj, std::chrono::milliseconds delay = std::chrono::milliseconds(0));
```
obj：需要释放的对象
delay：延迟回收时间（默认立即回收）

### 4️⃣ 清空对象池
```cpp
void clear();
```
清空对象池中所有对象,延迟回收对象也会被立即释放。
### 5️⃣ 设置回调函数(非线程安全)
```cpp
void setPreProcess(PreProcess preProcess);
void setPostProcess(PostProcess postProcess);
void setFinalProcess(FinalProcess finalProcess);
```
setPreProcess(func)：对象获取前执行的函数
setPostProcess(func)：对象释放前执行的函数
setFinalProcess(func)：对象销毁时执行的函数
## 📌 线程安全
cppobjectpool 保证除回调函数以外的其它接口的线程安全。

## 📄 许可证
本项目采用 MIT License 开源协议，欢迎自由使用和修改。🚀
