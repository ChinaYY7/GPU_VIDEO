#ifndef COMMON_SYS_SINGLETON_H_
#define COMMON_SYS_SINGLETON_H_

#include <cstddef>
#include <pthread.h>
#include <iostream>

/**
 * @brief 单例模板类
 *
 * @tparam T
 */
template <typename T>
class Singleton {
    public:
        /**
         * @brief 析构函数
         */
        virtual ~Singleton() {}

        /**
         * @brief 返回唯一实例
         *
         * @return 指向实例的指针
         */
        static T* getInstance() 
        {
            pthread_once(&m_InstanceFlag, &Singleton::initInstance);
            return m_pInstance;
        }

    protected:
        // can't new
        Singleton() {}

    private:
        static pthread_once_t m_InstanceFlag;  // to protect m_pInstance
        static T* m_pInstance;  // singleton

    private:
        // can't copy
        Singleton(const Singleton &s);
        Singleton& operator=(const Singleton &s);

        // init m_pInstance
        static void initInstance() 
        {
            m_pInstance = new T();
            if(m_pInstance == NULL)
            {
                std::cout << "new error" << std::endl;
                exit(-1);
            }
        }
};

template <typename T>
pthread_once_t Singleton<T>::m_InstanceFlag = PTHREAD_ONCE_INIT;

template <typename T>
T* Singleton<T>::m_pInstance = NULL;

#endif  // COMMON_SYS_SINGLETON_H_

