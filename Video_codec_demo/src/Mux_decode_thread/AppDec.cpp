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

    //初始化解码器管理器
    GpuDecode::DecodeManager DecodeManager1(use_common->get_iGpu());
    std::vector<GpuDecode::Decodeframe> Decodeframes;
    for(int i = 0; i < FFmpegManager1.Get_size(); i++)
        DecodeManager1.add(FFmpegManager1.Get_NvCodecId(), i);

    DecodeManager1.init();

    //启动解流器线程, 因为GPU的初始化需要一定的时间，因此因等待GPU初始化完成后再开启解流线程，否则解流队列堆积数据太快。
    pthread_t FFmpegManager_tid = FFmpegManager1.start();
    LOG(WARNING) << "start FFmpegManager thread: " << std::hex << FFmpegManager_tid;

    //启动解码器线程
    pthread_t DecodeManager_tid = DecodeManager1.start();
    LOG(WARNING) << "start DecodeManager thread: " << std::hex << DecodeManager_tid;

    pthread_join(FFmpegManager_tid,NULL);
    LOG(WARNING) << "Finish FFmpegManager thread:" << std::hex << FFmpegManager_tid;

    pthread_join(DecodeManager_tid,NULL);
    LOG(WARNING) << "Finish DecodeManager thread:" << std::hex << DecodeManager_tid;
    
    NvCodec::JpegEncoder::getInstance()->finish();
    pthread_join(JpegEncoder_tid,NULL);
    LOG(WARNING) << "Finish JpegEncoder thread:" << std::hex << JpegEncoder_tid;
    
    return 0;
}
