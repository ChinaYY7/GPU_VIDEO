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

            static void* run(void *arg);

        public:
            DecodeManager();
            DecodeManager(int iGpu);
            ~DecodeManager();
            uint16_t add(cudaVideoCodec eCodec, uint64_t id);
            uint16_t remove(uint64_t id);
    
            std::vector<Decodeframe> Decode_all(std::vector<FFmpeg::Demuxframe> Demuxframes);
        
            bool init();
            pthread_t start();
            void finish();
            uint16_t Get_size()
            {
                return m_size;
            }
    };
}