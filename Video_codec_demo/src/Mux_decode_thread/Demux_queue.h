#pragma once
#include "FFmpegManager.h"
#include "Singleton.h"

//#define DISPLAY_INFO
#define QUEUE_SIZE 1000
#define ACTIVE_DEQUEUE_SIZE 10  //当队列为空时，写队列线程使队列增加多少后才激活读队列的线程（小心设置，否则可能出现死锁，当写对列线程已经不能写够激活读队列线程的数量的时候，待解决）
#define ACTIVE_ENQUEUE_SIZE 10  //当队列满了时，读对列线程使队列减少多少后才激活写队列的线程（一定要保证读完队列，否则可能造成死锁）
#define BATCH_SIZE 500

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
            ~Demux_queue()
            {
                //LOG(INFO) << "Demux_queue delete";
            }

            /*
            * 功能：
            *   非阻塞模式，写入缓存队列
            * 参数：
            *   item：入队元素
            * 返回值：
            *   当前缓存队列大小
            */
            uint64_t input_cahce(const FFmpeg::Demuxframe & item);
        
        public:

            /*
            * 功能：
            *   阻塞模式，入队列（单元素）
            * 参数：
            *   item：入队元素
            * 返回值：
            *   当前入队位置
            */
            int enqueue(const FFmpeg::Demuxframe  & item);

            /*
            * 功能：
            *   阻塞模式，出队列（单元素）
            * 参数：
            *   item：出队元素
            * 返回值：
            *   当前出队位置
            */
            int dequeue(FFmpeg::Demuxframe & item);

            /*
            * 功能：
            *   非阻塞模式，入队列（单元素）
            * 参数：
            *   item：入队元素
            * 返回值：
            *   当前缓存队列大小
            */
            uint64_t enqueue_non_blocking(const FFmpeg::Demuxframe & item);

            /*
            * 功能：
            *   非阻塞模式，将缓存队列入队列（单元素）
            * 返回值：
            *   当前缓存队列大小
            */
            uint64_t enqueue_cache();

            /*
            * 功能：
            *   阻塞模式，批量出队列（多元素）
            * 参数：
            *   Demuxframes：元素数组
            * 返回值：
            *   当前队列剩余大小
            */
            uint64_t dequeue_batch(std::vector<FFmpeg::Demuxframe> & Demuxframes);

            /*
            * 功能：
            *   查询队列使用量
            * 返回值：
            *   队列使用量
            */
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

            void clear_cache()
            {
                m_clear_cache = true;
            }

            int is_clear_cache()
            {
                return m_clear_cache;
            }
    };
}