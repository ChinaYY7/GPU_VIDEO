#include "Enjpeg_queue.h"
#include <glog/logging.h>

namespace NvCodec
{
    Enjpeg_queue::Enjpeg_queue():m_write_position(0), m_read_position(0), m_used_queue_size(0)
    {
        pthread_mutex_init(&m_lock, NULL);
        pthread_cond_init(&m_cond, NULL);
        pthread_cond_init(&m_cond_full, NULL);
    }

    int Enjpeg_queue::enqueue(const IdData item)
    {
        pthread_mutex_lock(&m_lock);

        //入队列前先判断队列是否已满(一定要先判断，否则会导致解除阻塞后丢失当前数据)
        while(m_used_queue_size == ENJPEG_QUEUE_SIZE)
        {
            LOG(WARNING) << "Demux_queue is full";
            pthread_cond_wait(&m_cond_full, &m_lock);
        }

        m_queue[m_write_position % ENJPEG_QUEUE_SIZE] = item;
        m_write_position++;
        m_used_queue_size++;

        pthread_mutex_unlock(&m_lock);
        if(m_used_queue_size == 1)
            pthread_cond_signal(&m_cond);

        return m_write_position % ENJPEG_QUEUE_SIZE;
    }

    int Enjpeg_queue::dequeue(IdData &item)
    {
        pthread_mutex_lock(&m_lock);
        
        while(m_used_queue_size == 0)
            pthread_cond_wait(&m_cond, &m_lock);
        
        item = m_queue[m_read_position % ENJPEG_QUEUE_SIZE];
        m_read_position++;
        m_used_queue_size--;

        pthread_mutex_unlock(&m_lock);

        if(m_used_queue_size == ENJPEG_QUEUE_SIZE / 2)
            pthread_cond_signal(&m_cond_full);

        return m_read_position % ENJPEG_QUEUE_SIZE;
    }

    int Enjpeg_queue::query_used_queue_size(void)
    {
        return m_used_queue_size;
    }
}