#include <iostream>
#include <string>
#include <algorithm>
#include <thread>
#include <cuda.h>
#include <glog/logging.h>

#include "../Common/AppDecUtils.h"
#include "common.h"
#include "Decode.h"
//#include "../Utils/NvCodecUtils.h"


int main(int argc, char **argv) 
{
    char szInFilePath[256] = "", szOutFilePath[256] = "";
    int iGpu = 0;
    int File_or_Rtsp = 0;

    ParseCommandLine(argc, argv, szInFilePath, szOutFilePath, iGpu, File_or_Rtsp);
    Set_glog_output(argv[0]);
    if(!File_or_Rtsp)
    {
        LOG(INFO) << "Input is File: " << szInFilePath;
        CheckInputFile(szInFilePath);
    }
    else
    {
        LOG(INFO) << "Input is rstp: " << szInFilePath;
    }
    
    CreateFolder(szOutFilePath);

    //初始化GPU
    ck(cuInit(0));

    //获取GPU数量
    int nGpu = 0;
    ck(cuDeviceGetCount(&nGpu));
    if (iGpu < 0 || iGpu >= nGpu) 
    {
        LOG(ERROR) << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
        return 1;
    }

    CUcontext cuContext = NULL;

    //创建对应GPU编号的上下文
    createCudaContext(&cuContext, iGpu, 0);

    LOG(INFO) << "Decode with demuxing.";
    DecodeMediaFile(cuContext, szInFilePath, szOutFilePath);
}