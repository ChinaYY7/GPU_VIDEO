#pragma once
#include <map>
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include "FFmpegItem.h"
namespace FFmpeg
{
    class Demuxframe
    {
        private:
            uint64_t m_id;
            int m_nVideoBytes;
            std::vector<uint8_t> m_Video_date;

        public:
            Demuxframe()
            {
                m_id = 100;
                m_nVideoBytes = 1;
            }

            Demuxframe(uint64_t id, int nVideoBytes, uint8_t *pVideo)
            {
                m_id = id;
                m_nVideoBytes = nVideoBytes;
                //结束帧会传入nullptr
                if(pVideo != nullptr) 
                    m_Video_date.assign(pVideo, pVideo + nVideoBytes);
            }

            ~Demuxframe(){}

            void set_size(int size)
            {
                m_nVideoBytes = size;
            }


            void clear_data()
            {
                m_Video_date.clear();
            }

            std::vector<uint8_t> data() const
            {
                return m_Video_date;
            }

            uint64_t id() const
            {
                return m_id;
            }

            int size() const
            {
                return m_nVideoBytes;
            }
    };

    class FFmpegManager 
    {
        private:
            std::map<uint64_t, FFmpegItem *> m_FFmpegMap;
            uint16_t m_size;
            bool m_isEnd;

            bool first_display = true;

            /*
            * 功能：
            *   解流器管理器线程，使管理器中所有解流器依次解流各自的视频源，并将结果写入队列（交给GPU硬解码器）
            * 参数：
            *   *arg:解流器管理器类指针
            */
            static void* run(void *arg);

            /*
            * 功能：
            *   检查是否所有解流器对应的视频文件已经读取完毕或视频流结束
            * 返回值：
            *   true:所有解流器不需要继续工作； false:还有需要工作的解流器
            */
            bool check_finish();
            
        public:
            FFmpegManager();
            ~FFmpegManager();

            /*
            * 功能：
            *   添加解流器
            * 参数：
            *   szFilePath：解流器对应的文件或rtsp流
            *   id：解流器id
            * 返回值：
            *   当前解流器管理器中解流器的数量
            */
            uint16_t add(const char *szFilePath, uint64_t id);

            /*
            * 功能：
            *   移除解流器
            * 参数：
            *   id：解流器id
            * 返回值：
            *   当前解流器管理器中解流器的数量
            */
            uint16_t remove(uint64_t id);

            /*
            * 功能：
            *   获得当前解流器管理器中解流器解流的视频对应的GPU硬解码器编号
            * 返回值：
            *   GPU硬解码器编号
            */
            cudaVideoCodec Get_NvCodecId();


            /*
            * 功能：
            *   解流管理器内所有解流器实例进行解流一次
            * 参数：
            *   Demuxframes：打包的解流后帧数据
            */
            void Demux_all(std::vector<Demuxframe> & Demuxframes);

            /*
            * 功能：
            *   当视频文件读取完毕或视频流结束时，传入结束帧（以帧大小为-1代表结束帧）
            * 参数：
            *   item：一个解流器实例
            *   Demuxframes：打包的帧数据
            */
            void Enqueue_end(FFmpegItem & item, std::vector<Demuxframe> & Demuxframes);
            
            /*
            * 功能：
            *   初始化解流器（打开解流器，探测帧分布）
            * 返回值：
            *   执行状态：true：成功；false：失败
            */
            bool init();

            /*
            * 功能：
            *   启动解流器管理器线程
            * 返回值：
            *   线程id
            */
            pthread_t start();

            /*
            * 功能：
            *   关闭解流器线程
            */
            void finish();

            uint16_t Get_size()
            {
                return m_size;
            }
    };
}