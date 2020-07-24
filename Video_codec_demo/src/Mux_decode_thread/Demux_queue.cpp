#include "Demux_queue.h"
#include <glog/logging.h>
#include <cstring>

namespace Demux
{
    Demux_queue::Demux_queue():m_write_position(0), m_read_position(0), m_used_queue_size(0), m_enqueue_sta(true), m_dequeue_sta(true), m_clear_cache(false)
    {
        pthread_mutex_init(&m_lock, NULL);
        pthread_cond_init(&m_cond_empty, NULL);
        pthread_cond_init(&m_cond_full, NULL);
        Demuxframes_cache.clear();
    }

    int Demux_queue::enqueue(const FFmpeg::Demuxframe  & item)
    {
        pthread_mutex_lock(&m_lock);
#ifdef DISPLAY_INFO
        if(m_enqueue_sta)
        {
            LOG(INFO) << "enqueue have the lock ";
            m_enqueue_sta = false;
            m_dequeue_sta = true;
        }
#endif
        //入队列前先判断队列是否已满(一定要先判断，否则会导致解除阻塞后丢失当前数据)(用while判断条件避免错误唤醒)
        while(m_used_queue_size == QUEUE_SIZE)
        {
            LOG(WARNING) << "Demux_queue is full";
            pthread_cond_wait(&m_cond_full, &m_lock);
        }

        //队列未满
        m_queue[m_write_position % QUEUE_SIZE] = item;
        m_write_position++;
        m_used_queue_size++;
#ifdef DISPLAY_INFO 
        LOG(INFO) << "                              enqueue:"  << (m_write_position - 1) % QUEUE_SIZE << " size:" << m_used_queue_size << " item.nVideoBytes: "<< item.size();
#endif        
        pthread_mutex_unlock(&m_lock);
        if(m_used_queue_size == ACTIVE_DEQUEUE_SIZE || item.size() == -1) //解决最后写队列写的数量不够激活读队列线程的时候
            pthread_cond_signal(&m_cond_empty);
        
        return (m_write_position - 1) % QUEUE_SIZE;
    }

    uint64_t Demux_queue::enqueue_non_blocking(const FFmpeg::Demuxframe & item)
    {
        uint64_t cache_size = input_cahce(item);
        cache_size = enqueue_cache();
        
        return cache_size;
    }

    //返回cache中的数据大小
    uint64_t Demux_queue::input_cahce(const FFmpeg::Demuxframe  & item)
    {
        Demuxframes_cache.push_back(item);
#ifdef DISPLAY_INFO
        LOG(INFO) << "                              input cache:"  << " cahce size:" << Demuxframes_cache.size() << " item.nVideoBytes: "<< item.size();
#endif
        return Demuxframes_cache.size();
    }

    //返回cache中还剩多少数据未写入队列
    uint64_t Demux_queue::enqueue_cache()
    {
        int lock_sta = pthread_mutex_trylock(&m_lock);

        //获得锁，将缓存数据写入队列
        if(lock_sta == 0)
        {
#ifdef DISPLAY_INFO 
            if(m_enqueue_sta)
            {
                LOG(INFO) << "enqueue have the lock ";
                m_enqueue_sta = false;
                m_dequeue_sta = true;
            }
#endif
            //入队列前先判断队列是否已满，满了就先写入缓存
            if(m_used_queue_size < QUEUE_SIZE)
            {
                int i = 0;
                for(i = 0; i < Demuxframes_cache.size(); i++)
                {
                    if(m_used_queue_size < QUEUE_SIZE)
                    {
                        if(m_write_position % QUEUE_SIZE == 0)
                        {
                            LOG(WARNING) << "From Demux queue start and now cache size:" << Demuxframes_cache.size(); 
                        }
                        m_queue[m_write_position % QUEUE_SIZE] = Demuxframes_cache[i];
                        m_write_position++;
                        m_used_queue_size++;
#ifdef DISPLAY_INFO
                        LOG(INFO) << "                              enqueue:"  << (m_write_position - 1) % QUEUE_SIZE << " size:" << m_used_queue_size << " item.nVideoBytes: "<< Demuxframes_cache[i].size();
#endif            
                    }
                    else
                        break;
                }
                Demuxframes_cache.erase(Demuxframes_cache.begin(), Demuxframes_cache.begin() + i);
            }
            else if(m_used_queue_size == QUEUE_SIZE)
            {
                //LOG(WARNING) << "Demux_queue is full";
            }

            pthread_mutex_unlock(&m_lock);
            if(m_used_queue_size == ACTIVE_DEQUEUE_SIZE || m_clear_cache) //解决最后写队列写的数量不够激活的时候
            {
                pthread_cond_signal(&m_cond_empty);
            }
        }
        return Demuxframes_cache.size();
    }

    int Demux_queue::dequeue(FFmpeg::Demuxframe &item)
    {
        pthread_mutex_lock(&m_lock);
#ifdef DISPLAY_INFO
        if(m_dequeue_sta)
        {
            LOG(INFO) << "dequeue have the lock ";
            m_dequeue_sta = false;
            m_enqueue_sta = true;
        }
#endif
        while(m_used_queue_size == 0)
        {
            LOG(WARNING) << "Demux_queue is empty";
            pthread_cond_wait(&m_cond_empty, &m_lock);
        }
        
        item = m_queue[m_read_position % QUEUE_SIZE];
        m_read_position++;
        m_used_queue_size--;

#ifdef DISPLAY_INFO
        LOG(INFO) << "                              dequeue:" << (m_read_position - 1) % QUEUE_SIZE << " size:" << m_used_queue_size << " item.nVideoBytes: " << item.size();
#endif

        pthread_mutex_unlock(&m_lock);
        if(m_used_queue_size == QUEUE_SIZE - ACTIVE_ENQUEUE_SIZE)
            pthread_cond_signal(&m_cond_full);

        return (m_read_position - 1) % QUEUE_SIZE;
    }

    uint64_t Demux_queue::dequeue_batch(std::vector<FFmpeg::Demuxframe> &Demuxframes)
    {
        FFmpeg::Demuxframe item;
        int dequeue_num = 0;
        int old_read_position = m_read_position;

        Demuxframes.clear();

        pthread_mutex_lock(&m_lock);

#ifdef DISPLAY_INFO
        if(m_dequeue_sta)
        {
            LOG(INFO) << "dequeue have the lock ";
            m_dequeue_sta = false;
            m_enqueue_sta = true;
        }
#endif

        while(m_used_queue_size == 0)
        {
            LOG(WARNING) << "Demux_queue is empty";
            pthread_cond_wait(&m_cond_empty, &m_lock);
        }
        
        
        if(m_used_queue_size < BATCH_SIZE)
            dequeue_num = m_used_queue_size;
        else
            dequeue_num = BATCH_SIZE;

        if(m_read_position < m_write_position)
        {
            for(int i = 0; i < dequeue_num; i++)
            {
                Demuxframes.push_back(m_queue[m_read_position % QUEUE_SIZE]);
                m_queue[m_read_position % QUEUE_SIZE].clear_data();
                m_read_position++;
                m_used_queue_size--;
            }
        }
#ifdef DISPLAY_INFO
        LOG(INFO) << "dequeue:" << old_read_position % QUEUE_SIZE << " -> " << m_read_position % QUEUE_SIZE << " dequeue size:" << dequeue_num
        << " res size:" << m_used_queue_size << " Demuxframes size:" << Demuxframes.size();
#endif

        pthread_mutex_unlock(&m_lock);
        if(m_used_queue_size <= QUEUE_SIZE - ACTIVE_ENQUEUE_SIZE)
            pthread_cond_signal(&m_cond_full);

        return m_used_queue_size;
    }

    int Demux_queue::query_used_queue_size(void)
    {
        return m_used_queue_size;
    }
}