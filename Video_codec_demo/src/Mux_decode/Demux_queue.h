#pragma once
#include "FFmpegManager.h"
#include "Singleton.h"

//#define DISPLAY_INFO
#define QUEUE_SIZE 1000
#define ACTIVE_DEQUEUE_SIZE 10  //当队列为空时，写队列线程使队列增加多少后才激活读队列的线程（小心设置，否则可能出现死锁，当写对列线程已经不能写够激活读队列线程的数量的时候，待解决）
#define ACTIVE_ENQUEUE_SIZE 10  //当队列满了时，读对列线程使队列减少多少后才激活写队列的线程（一定要保证读完队列，否则可能造成死锁）
#define BATCH_SIZE 100

namespace Demux
{
    class Demux_queue : public Singleton<Demux_queue>
    {
            friend class Singleton<Demux_queue>;
        private:
            FFmpeg::Demuxframe m_queue[QUEUE_SIZE];
            std::vector<FFmpeg::Demuxframe> Demuxframes_cache;
            int m_write_position;
            int m_read_position;
            int m_used_queue_size;
            pthread_mutex_t m_lock;
            pthread_cond_t m_cond_empty;
            pthread_cond_t m_cond_full;

            bool m_enqueue_sta;
            bool m_dequeue_sta;
            bool m_clear_cache;

            Demux_queue();
            ~Demux_queue(){}

            uint64_t input_cahce(const FFmpeg::Demuxframe item);
            uint64_t enqueue_cache();
        
        public:
            int enqueue(const FFmpeg::Demuxframe item);
            int dequeue(FFmpeg::Demuxframe &item);
            uint64_t enqueue_non_blocking(const FFmpeg::Demuxframe item);
            uint64_t dequeue_batch(std::vector<FFmpeg::Demuxframe> &Demuxframes);
            int query_used_queue_size(void);

            int get_write_position()
            {
                return m_write_position;
            }

            int get_read_position()
            {
                return m_read_position;
            }

            int get_cache_size()
            {
                return Demuxframes_cache.size();
            }

            int is_clear_cache()
            {
                return m_clear_cache;
            }
    };
}