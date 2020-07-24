#include <iostream>
#include <algorithm>
#include <thread>
#include <string>
#include <cuda.h>
#include <tbb/tick_count.h>
#include <glog/logging.h>
#include <signal.h>

#include "NvDecoder/NvDecoder.h"
#include "../Utils/NvCodecUtils.h"
#include "../Utils/FFmpegDemuxer.h"
#include "../Common/AppDecUtils.h"
#include "libgpujpeg/gpujpeg.h"
#include "common.h"
#include "JpegEncoder.h"
#include "Decode.h"

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

    if (signal(SIGINT, doExit) == SIG_ERR)
    {
        LOG(ERROR) << "\nIn main: set SIGINT(catch \"ctrl+c\") signal error!\n";
        return -1;
    }

    //解析命令
    use_common->ParseCommandLine(argc, argv);

    if(use_common->is_File())
    {
        LOG(INFO) << "Input is File: " << use_common->InputFileName;
        CheckInputFile(use_common->InputFileName.c_str());
    }
    else
        LOG(INFO) << "Input is rstp: " << use_common->InputFileName;
        
    use_common->CreateFolder(use_common->OutputFloder.c_str());

    pthread_t  JpegEncoder_tid;
    NvCodec::JpegEncoder::getInstance()->set_param(1920,1080, 0);
    JpegEncoder_tid = NvCodec::JpegEncoder::getInstance()->start();
    LOG(WARNING) << "start JpegEncoder thread: " << std::hex <<JpegEncoder_tid;

    pthread_t tid[10];
    for(int i = 0; i < Decode_Thread; i++)
    {
        NvCodec::Nv_Decode *decode = new NvCodec::Nv_Decode(use_common->iGpu, use_common->InputFileName, use_common->OutputFloder);
        decode->set_video_id(i);
        decode->set_interval(0);
        tid[i] = decode->start();
        LOG(WARNING) << "start decode thread: " << std::hex << tid[i];
    }
    for(int i = 0; i < Decode_Thread; i++)
    {
        pthread_join(tid[i],NULL);
        LOG(WARNING) << "Finish decode thread:" << std::hex <<tid[i];
    }

    NvCodec::JpegEncoder::getInstance()->finish();
    pthread_join(JpegEncoder_tid,NULL);
    LOG(WARNING) << "Finish JpegEncoder thread:" << std::hex << JpegEncoder_tid;
        
    return 0;
}
