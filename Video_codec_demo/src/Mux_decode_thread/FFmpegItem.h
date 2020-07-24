#pragma once
extern "C"  //该部分头文件必须包含extern "C"，否则会报undefined reference
{
    #include <libavformat/avformat.h>
    #include <libavformat/avio.h>
    #include <libavcodec/avcodec.h>
}
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <glog/logging.h>

#include "cuviddec.h"

namespace FFmpeg
{
    class FFmpegItem
    {
        private:
            enum class state {Closed, Opened, Error, Closed_error};
            AVFormatContext *m_fmtc;   // 封装格式上下文的结构体，也是统领全局的结构体，保存了视频文件封装格式的相关信息
            AVPacket m_pkt, m_pktFiltered; /*!< AVPacket stores compressed data typically exported by demuxers and then passed as input to decoders *///存储1帧压缩编码数据
            AVBSFContext *m_bsfc;
            AVFrame *m_pFrame;
            AVCodecContext  *m_pCodecCtx;  //编码器上下文结构体，保存了视频（音频）编解码相关信息
            AVCodec  *m_pCodec;  	//解码器，每种视频（音频）编解码器（例如H.264解码器）对应一个该结构体

            int m_iVideoStream;
            bool m_bMp4H264, m_bMp4HEVC, m_bMp4MPEG4;
            AVCodecID m_eVideoCodec;
            AVPixelFormat m_eChromaFormat;
            int m_nWidth, m_nHeight, m_nBitDepth, m_nBPP, m_nChromaHeight;
            double m_timeBase;
            uint64_t m_id;
            state m_state;
            bool first_finish;

            uint8_t *m_pDataWithHeader;

            unsigned int m_frameCount;
            unsigned int m_H264_frameCount;
            char m_filename[512];
            

            int64_t m_Pframe_interval_pts;
            uint64_t m_Kframe_pts, m_Pframe_pts;

        public:
            /*
            * 功能：
            *   构造函数，获得视频流地址或视频文件名称
            * 参数：
            *   szFilePath：文件名字符串
            */
            FFmpegItem(const char *szFilePath);
            ~FFmpegItem();

            /*
            * 功能：
            *   打开视频源
            * 返回值：
            *   执行结果，ture：执行成功；false：执行失败
            */
            bool Open();
            bool Close();

            /*
            * 功能：
            *   探测H264视频的P帧间隔
            * 返回值：
            *   执行结果，0：执行成功；-1：执行失败
            */
            int ffprobe();

            /*
            * 功能：
            *   H264视频只抽取I帧和P帧
            * 参数：
            *   ppVideo：一帧未解码数据的缓存地址
            *   pnVideoBytes：该未解码帧数据大小
            *   pts：帧时间戳（待纠正）
            * 返回值：
            *   执行结果，ture：执行成功；false：执行失败
            */
            bool Demux_H264_IPFrame(uint8_t **ppVideo, int *pnVideoBytes, int64_t *pts = NULL);

            //从视频源中抽取所有帧
            bool Demux(uint8_t **ppVideo, int *pnVideoBytes, int64_t *pts = NULL);

            AVCodecID GetVideoCodec() 
            {
                return m_eVideoCodec;
            }

            AVPixelFormat GetChromaFormat() 
            {
                return m_eChromaFormat;
            }
            int GetWidth() 
            {
                return m_nWidth;
            }
            int GetHeight() 
            {
                return m_nHeight;
            }
            int GetBitDepth() 
            {
                return m_nBitDepth;
            }
            int GetFrameSize() 
            {
                return m_nWidth * (m_nHeight + m_nChromaHeight) * m_nBPP;
            }

            uint64_t Get_id()
            {
                return m_id;
            }

            char * Get_filename()
            {
                return m_filename;
            }

            uint64_t GetPframe_interval_pts()
            {
                return m_Pframe_interval_pts;
            }

            uint64_t Get_H264_frameCount()
            {
                return m_H264_frameCount;
            }

            uint64_t Get_m_frameCount()
            {
                return m_frameCount;
            }

            bool is_open()
            {
                return m_state == state::Opened;
            }

            bool is_error()
            {
                return m_state == state::Error;
            }

            bool is_close()
            {
                return m_state == state::Closed;
            }

            bool is_close_error()
            {
                return m_state == state::Closed_error;
            }

            //返回是否是打开视频流后第一次视频流被关闭的状态
            bool is_first_finish()
            {
                return first_finish;
            }

            void Set_state(state st)
            {
                m_state = st;
                if(m_state == state::Opened)
                    first_finish = true;
            }

            void Set_id(uint64_t id)
            {
                m_id = id;
            }

            void Clear_first_finish()
            {
                first_finish = false;
            }




            cudaVideoCodec FFmpeg2NvCodecId() 
            {
                switch (m_eVideoCodec) 
                {
                    case AV_CODEC_ID_MPEG1VIDEO : return cudaVideoCodec_MPEG1;
                    case AV_CODEC_ID_MPEG2VIDEO : return cudaVideoCodec_MPEG2;
                    case AV_CODEC_ID_MPEG4      : return cudaVideoCodec_MPEG4;
                    case AV_CODEC_ID_VC1        : return cudaVideoCodec_VC1;
                    case AV_CODEC_ID_H264       : return cudaVideoCodec_H264;
                    case AV_CODEC_ID_HEVC       : return cudaVideoCodec_HEVC;
                    case AV_CODEC_ID_VP8        : return cudaVideoCodec_VP8;
                    case AV_CODEC_ID_VP9        : return cudaVideoCodec_VP9;
                    case AV_CODEC_ID_MJPEG      : return cudaVideoCodec_JPEG;
                    default                     : return cudaVideoCodec_NumCodecs;
                }
            }
    };
    
}