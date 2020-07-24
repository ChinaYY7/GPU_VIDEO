#pragma once
#include<map>
#include <iostream>
#include <vector>
#include "DecodeItem.h"
#include "FFmpegManager.h"

namespace GpuDecode
{
    struct Decodeframe
    {
        uint64_t id;
        int nVideoBytes;
        uint8_t *pVideo;
    };

    class DecodeManager
    {
        private:
            std::map<uint64_t, DecodeItem *> m_DecodeMap;
            uint16_t m_size;
            std::vector<Decodeframe> m_Decodeframes;
            int m_iGpu;
            bool m_isEnd;

            /*
            * 功能：
            *   检查是否所有解码器对应的视频文件已经读取完毕或视频流结束
            * 返回值：
            *   true:所有解码器不需要继续工作； false:还有需要工作的解码器
            */
            bool check_finish();

            /*
            * 功能：
            *   解码器管理器线程，使管理器中所有解码器依次解码各自对应的帧，并将结果写入队列（交给JPEG编码器）
            * 参数：
            *   *arg:解码器管理器指针
            */
            static void* run(void *arg);

        public:
            DecodeManager();
            DecodeManager(int iGpu);
            ~DecodeManager();

            /*
            * 功能：
            *   添加解码器
            * 参数：
            *   eCodec：GPU解码器编号
            *   id：GPU解码器id（与解流器id对应）
            * 返回值：
            *   当前解码器管理器中解码器的数量
            */
            uint16_t add(cudaVideoCodec eCodec, uint64_t id);

            /*
            * 功能：
            *   移除解码器
            * 参数：
            *   id：解码器id
            * 返回值：
            *   当前解码器管理器中解码器的数量
            */
            uint16_t remove(uint64_t id);
    
            /*
            * 功能：
            *   使解码器管理器包含的解码器依次解码对应的已解流帧数据包
            * 参数：
            *   Demuxframes：打包的解流后帧数据
            * 返回值：
            *   已完成解码的帧数据数组
            */
            std::vector<Decodeframe> Decode_all(const std::vector<FFmpeg::Demuxframe> & Demuxframes);
        
            /*
            * 功能：
            *   初始化解码器
            * 返回值：
            *   执行状态：true：成功；false：失败
            */
            bool init();

            /*
            * 功能：
            *   启动解码器管理器线程
            * 返回值：
            *   线程id
            */
            pthread_t start();

            /*
            * 功能：
            *   关闭解码器线程
            */
            void finish();

            uint16_t Get_size()
            {
                return m_size;
            }
    };
}