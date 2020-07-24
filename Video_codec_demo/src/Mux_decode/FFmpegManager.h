#pragma once
#include <map>
#include <iostream>
#include <vector>
#include "FFmpegItem.h"
namespace FFmpeg
{
    struct Demuxframe
    {
        uint64_t id;
        int nVideoBytes;
        uint8_t *pVideo;
    };

    class FFmpegManager 
    {
        private:
            std::map<uint64_t, FFmpegItem *> m_FFmpegMap;
            uint16_t m_size;
            std::vector<Demuxframe> m_Demuxframes;
            bool m_isEnd;

            bool first_display = true;

            static void* run(void *arg);
            bool check_finish();
            void finish();
            
        public:
            FFmpegManager();
            ~FFmpegManager();
            uint16_t add(const char *szFilePath, uint64_t id);
            uint16_t remove(uint64_t id);
            cudaVideoCodec Get_NvCodecId();
            std::vector<Demuxframe> Demux_all();
            
            
            bool init();
            pthread_t start();
            uint16_t Get_size()
            {
                return m_size;
            }
    };
}