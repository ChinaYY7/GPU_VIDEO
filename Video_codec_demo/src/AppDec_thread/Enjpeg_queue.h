#pragma once
#include "JpegEncoder.h"
#include "Singleton.h"

#define ENJPEG_QUEUE_SIZE 20

namespace NvCodec
{
    class Enjpeg_queue : public Singleton<Enjpeg_queue>
    {
            friend class Singleton<Enjpeg_queue>;
        private:
            IdData m_queue[ENJPEG_QUEUE_SIZE];
            int m_write_position;
            int m_read_position;
            int m_used_queue_size;
            pthread_mutex_t m_lock;
            pthread_cond_t m_cond;

            Enjpeg_queue();
            ~Enjpeg_queue(){}
        
        public:
            int enqueue(const IdData item);
            int dequeue(IdData &item);
            int query_used_queue_size(void);
    };
}