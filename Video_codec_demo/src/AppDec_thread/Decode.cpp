#include <iostream>
#include <algorithm>
#include <thread>
#include <cuda.h>
#include <glog/logging.h>

#include "NvDecoder/NvDecoder.h"
#include "../Utils/NvCodecUtils.h"
#include "../Utils/FFmpegDemuxer.h"
#include "../Common/AppDecUtils.h"
#include "common.h"
#include "Decode.h"
#include "JpegEncoder.h"

namespace NvCodec
{
    Nv_Decode::Nv_Decode(int iGpu, std::string InputFileName, std::string OutFloderName):
    m_cuContext(NULL), m_nGpu(0), m_cuDevice(0), m_OutPlanar(1), m_interval(0), m_video_id(0)
    {
        memset(m_DeviceName, 0, sizeof(m_DeviceName));
        m_Configure_info.iGpu = iGpu;
        m_Configure_info.ctxflag = 0;
        m_Configure_info.InputFileName = InputFileName;
        m_Configure_info.OutFloderName = OutFloderName;
        m_Configure_info.cropRect = {};
        m_Configure_info.resizeDim = {};
    }

    Nv_Decode::~Nv_Decode()
    {
        delete m_demuxer;
        delete m_dec;
    }

    void Nv_Decode::set_cropRect(Rect cropRect)
    {
        m_Configure_info.cropRect = cropRect;
    }

    void Nv_Decode::set_resizeDim(Dim resizeDim)
    {
        m_Configure_info.resizeDim = resizeDim;
    }

    void Nv_Decode::set_OutPlanar(bool state)
    {
        m_OutPlanar = state;
    }

    void Nv_Decode::set_interval(int interval)
    {
        m_interval = interval;
    }

    void Nv_Decode::set_video_id(uint64_t video_id)
    {
        m_video_id = video_id;
    }

    void Nv_Decode::set_flag(int flag)
    {
        m_Configure_info.ctxflag = flag;
    }

    void Nv_Decode::createCudaContext()
    {
        
        //获取GPU编号对应的GPU句柄
        ck(cuDeviceGet(&m_cuDevice, m_Configure_info.iGpu));

        //获得GPU句柄对应的GPU字符串标识
        ck(cuDeviceGetName(m_DeviceName, sizeof(m_DeviceName), m_cuDevice));
        LOG(INFO) << "GPU in use: " << m_DeviceName;

        //创建GPU句柄对应的上下文
        ck(cuCtxCreate(&m_cuContext, m_Configure_info.ctxflag, m_cuDevice));
    }

    int Nv_Decode::init()
    {
        //初始化GPU
        ck(cuInit(0));

        //获取GPU数量
        ck(cuDeviceGetCount(&m_nGpu));
        if (m_Configure_info.iGpu < 0 || m_Configure_info.iGpu >= m_nGpu) 
        {
            LOG(ERROR) << "GPU ordinal out of range. Should be within [" << 0 << ", " << m_nGpu - 1 << "]" << std::endl;
            return 1;
        }

        //创建对应GPU编号的上下文
        createCudaContext();
        m_demuxer = new FFmpegDemuxer(m_Configure_info.InputFileName.c_str());
        m_dec = new NvDecoder(m_cuContext, false, FFmpeg2NvCodecId(m_demuxer->GetVideoCodec()), NULL, false, false, &m_Configure_info.cropRect, &m_Configure_info.resizeDim);
    }

    pthread_t Nv_Decode::start()
    {
        pthread_t tid;
        pthread_create(&tid, NULL, Nv_Decode::run, this);
        return tid;
    }

    void Nv_Decode::ConvertSemiplanarToPlanar(uint8_t *pHostFrame, int nWidth, int nHeight, int nBitDepth) 
    {
        if (nBitDepth == 8) 
        {
            // nv12->iyuv
            YuvConverter<uint8_t> converter8(nWidth, nHeight);
            converter8.UVInterleavedToPlanar(pHostFrame);
        } 
        else 
        {
            // p016->yuv420p16
            YuvConverter<uint16_t> converter16(nWidth, nHeight);
            converter16.UVInterleavedToPlanar((uint16_t *)pHostFrame);
        }
    }

    void* Nv_Decode::run(void* arg)
    {
        Nv_Decode *this_ = (Nv_Decode *)arg;
        int nVideoBytes = 0, nFrameReturned = 0, nFrame = 0;
        uint8_t *pVideo = NULL, **ppFrame;
        int64_t *ppTimestamp, frame_timestamp = 0;
        bool bDecodeOutSemiPlanar;
        unsigned long interval_count = 0;
        std::string frame_date_name;
        
        this_->init();

        this_->m_demuxer->ffprobe();

        auto start_time = NvCodec::common::getInstance()->Start_time();
        while(1)
        {
            //this_->m_demuxer->Demux(&pVideo, &nVideoBytes);
            this_->m_demuxer->Demux_H264_keyFrame(&pVideo, &nVideoBytes);
            LOG(INFO) << "demux a frame" << " and nVideoBytes: " << nVideoBytes << " and timestamp: " << frame_timestamp;
            //frame_date_name = common::getInstance()->Generate_file_name(0, "frame_data");
            //frame_date_name = frame_date_name + ".data";
            //common::getInstance()->Write_a_file(frame_date_name,reinterpret_cast<char*>(pVideo),nVideoBytes);
            this_->m_dec->Decode(pVideo, nVideoBytes, &ppFrame, &nFrameReturned, 0, &ppTimestamp, frame_timestamp);
            frame_timestamp++;
            LOG(INFO) << "Decode nFrameReturned: " << nFrameReturned;
            
            if (!nFrame && nFrameReturned)
            {
                //LOG(INFO) << this_->m_dec->GetVideoInfo();
                bDecodeOutSemiPlanar = (this_->m_dec->GetOutputFormat() == cudaVideoSurfaceFormat_NV12) || (this_->m_dec->GetOutputFormat() == cudaVideoSurfaceFormat_P016);
            }
            
            for (int i = 0; i < nFrameReturned; i++) 
            {
                if (this_->m_OutPlanar && bDecodeOutSemiPlanar) 
                    this_->ConvertSemiplanarToPlanar(ppFrame[i], this_->m_dec->GetWidth(), this_->m_dec->GetHeight(), this_->m_dec->GetBitDepth());
                
                if(this_->m_interval != 0)
                {
                    interval_count++;
                    if(!(interval_count % this_->m_interval))
                    {
                        //std::string capFile = NvCodec::common::getInstance()->Generate_file_name(this_->m_video_id);
                        //capFile += ".yuv";
                        //LOG(INFO) << "saveAsFile : " << capFile;
                        //NvCodec::common::getInstance()->Write_a_file(capFile,reinterpret_cast<char*>(ppFrame[i]) ,this_->m_dec->GetFrameSize());
                        if(NvCodec::JpegEncoder::getInstance()->enqueue(this_->m_video_id, ppFrame[i], this_->m_dec->GetFrameSize()) < 0)
                        {
                            LOG(ERROR) << "Can't finish Jpeg Encode";
                            break;
                        }
                            
                    }
                }
                else
                {
                    if(NvCodec::JpegEncoder::getInstance()->enqueue(this_->m_video_id, ppFrame[i], this_->m_dec->GetFrameSize()) < 0)
                    {
                        LOG(ERROR) << "Can't finish Jpeg Encode";
                        nVideoBytes = 0;
                        break;
                    }
                }
            }
            nFrame += nFrameReturned;

            if(NvCodec::common::getInstance()->is_Rtsp())
            {
                if(NvCodec::common::getInstance()->finish)
                    break;
            }
            else
            {
                if(!nVideoBytes || NvCodec::common::getInstance()->finish)
                    break;
            }
        }
        std::vector <std::string> aszDecodeOutFormat = { "NV12", "P016", "YUV444", "YUV444P16" };
        if (this_->m_OutPlanar) 
            aszDecodeOutFormat[0] = "iyuv";   aszDecodeOutFormat[1] = "yuv420p16";
    
        LOG(INFO)<< "\nvideo: " << this_->m_video_id <<" Total frame decoded: " << nFrame
                << "\nSaved in file \"" << this_->m_Configure_info.OutFloderName << "\" in "
                << aszDecodeOutFormat[this_->m_dec->GetOutputFormat()]
                << " format";
        
        int64_t duration_time = NvCodec::common::getInstance()->Duration(start_time);
        LOG(WARNING) << "Decode Finish Time: " << float(duration_time / 1000) << " s\nDecoode frame per second = " << nFrame / (duration_time / 1000) << " frames/s";
    }
}
