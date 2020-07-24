//多GPU测试，传入两个视频文件即可
#include <iostream>
#include <algorithm>
#include <thread>
#include <string>
#include <cuda.h>
#include <tbb/tick_count.h>
#include <glog/logging.h>
#include <signal.h>

#include "../Utils/NvCodecUtils.h"
#include "libgpujpeg/gpujpeg.h"
#include "common.h"
#include "JpegEncoder.h"
#include "DecodeItem.h"
#include "FFmpegItem.h"
#include "FFmpegManager.h"
#include "Demux_queue.h"
#include "DecodeManager.h"

#define Decode_Thread 1

void doExit(int signo)
{
    if (signo == SIGINT)       
    {
        LOG(WARNING) << "Ctrl+c EXIT !";
        NvCodec::common::getInstance()->set_finish(true);
        //exit(0);
    }
}

int main(int argc, char **argv) 
{
    NvCodec::common *use_common = NvCodec::common::getInstance();
    use_common->Set_glog_output(argv[0]);

    /*
    if (signal(SIGINT, doExit) == SIG_ERR)
    {
        LOG(ERROR) << "\nIn main: set SIGINT(catch \"ctrl+c\") signal error!\n";
        return -1;
    }
    */

    //解析命令
    use_common->ParseCommandLine(argc, argv);

    std::vector<std::string> InputFileNames = use_common->get_input_file_name();
    if(use_common->is_File())
        LOG(INFO) << "Input is File: ";
    else
        LOG(INFO) << "Input is rstp: ";
    
    for(int i = 0; i < InputFileNames.size(); i++)
            LOG(INFO) << "  " << InputFileNames[i];
        

    //配置并启动JPEG编码器线程
    pthread_t  JpegEncoder_tid;
    NvCodec::JpegEncoder::getInstance()->set_param(1920,1080, use_common->get_iGpu());
    JpegEncoder_tid = NvCodec::JpegEncoder::getInstance()->start();
    LOG(WARNING) << "start JpegEncoder thread: " << std::hex <<JpegEncoder_tid;

    int nFrameReturned = 0, nFrame = 0;
    uint8_t **ppFrame;
    int64_t *ppTimestamp, frame_timestamp = 0;
    bool bDecodeOutSemiPlanar;

    //初始化解流器管理器
    FFmpeg::Demuxframe Demuxframe_item;
    int read_position;
    std::vector<FFmpeg::Demuxframe> Demuxframes;
    FFmpeg::FFmpegManager FFmpegManager1;
    for(int i = 0; i < InputFileNames.size(); i++)
        FFmpegManager1.add(InputFileNames[i].c_str(), i);
    
    FFmpegManager1.init();

    std::vector<GpuDecode::Decodeframe> Decodeframes;

    GpuDecode::DecodeManager DecodeManager1(0);
    std::vector<GpuDecode::Decodeframe> Decodeframes1;
    DecodeManager1.add(FFmpegManager1.Get_NvCodecId(), 0);
    DecodeManager1.init();

    GpuDecode::DecodeManager DecodeManager2(1);
    std::vector<GpuDecode::Decodeframe> Decodeframes2;
    DecodeManager2.add(FFmpegManager1.Get_NvCodecId(), 1);
    DecodeManager2.init();

    while(1)
    {

        Demuxframes = FFmpegManager1.Demux_all();
        
        Decodeframes1 = DecodeManager1.Decode_all(Demuxframes);
        Decodeframes2 = DecodeManager2.Decode_all(Demuxframes);


        for(int i = 0; i < Decodeframes2.size(); i++)
        {
            Decodeframes1.push_back(Decodeframes2[i]);
            Decodeframes.swap(Decodeframes1);
        }
        
        for(int i = 0; i < Decodeframes.size(); i++)
        {
            if(Decodeframes.size() > 0)
            {
                if(NvCodec::JpegEncoder::getInstance()->enqueue(Decodeframes[i].id, Decodeframes[i].pVideo, Decodeframes[i].nVideoBytes) < 0)
                {
                    LOG(ERROR) << "Can't finish Jpeg Encode";
                    Demuxframe_item.nVideoBytes = 0;
                    break;
                }
            }
        }
        if(Demuxframes.size() == 0 || NvCodec::common::getInstance()->is_finish())
            break;
    }
    
    NvCodec::JpegEncoder::getInstance()->finish();
    pthread_join(JpegEncoder_tid,NULL);
    LOG(WARNING) << "Finish JpegEncoder thread:" << std::hex << JpegEncoder_tid;
    
    return 0;
}
