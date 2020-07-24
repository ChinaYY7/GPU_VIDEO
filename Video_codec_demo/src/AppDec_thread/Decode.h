#pragma once
#include <cuda.h>
#include <string>

#include "NvDecoder/NvDecoder.h"
#include "../Utils/FFmpegDemuxer.h"


namespace NvCodec
{
    typedef struct Configure_info
    {
        int iGpu;
        int ctxflag;
        std::string InputFileName;
        std::string OutFloderName;
        Rect cropRect;
        Dim  resizeDim;
    }Configure_info;

    class Nv_Decode
    {
        private:
            CUcontext m_cuContext;
            int m_nGpu;
            CUdevice m_cuDevice;
            char m_DeviceName[80];
            Configure_info m_Configure_info;
            FFmpegDemuxer *m_demuxer;
            NvDecoder *m_dec;
            bool m_OutPlanar;
            int m_interval;
            uint64_t m_video_id;

            static void* run(void* arg);

        public:
            Nv_Decode(int iGpu, std::string InputFileName, std::string OutFloderName);
            ~Nv_Decode();

            int init();
            void createCudaContext();
            void set_cropRect(Rect cropRect);
            void set_resizeDim(Dim  resizeDim);
            void set_OutPlanar(bool state);
            void set_interval(int interval);
            void set_video_id(uint64_t video_id);
            void set_flag(int flag);
            
            /*
            * 功能：
            *   存储模式转换，半交错模式转平面（连续）模式
            * 参数：
            *   pHostFrame：原始帧数据，执行完后，内容替换为转换后的数据
            *   nWidth：帧分辨率宽
            *   nHeight：帧分辨率高
            *   nBitDepth：帧数据位数，8/16位
            */
            void ConvertSemiplanarToPlanar(uint8_t *pHostFrame, int nWidth, int nHeight, int nBitDepth);
            pthread_t start();
    };
}