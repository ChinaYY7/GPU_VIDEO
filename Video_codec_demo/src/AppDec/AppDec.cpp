/*
* Copyright 2017-2018 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

//---------------------------------------------------------------------------
//! \file AppDec.cpp
//! \brief Source file for AppDec sample
//!
//! This sample application illustrates the demuxing and decoding of a media file followed by resize and crop of the output frames.
//! The application supports both planar (YUV420P and YUV420P16) and non-planar (NV12 and P016) output formats.
//---------------------------------------------------------------------------

#include <iostream>
#include <algorithm>
#include <thread>
#include <string>
#include <cuda.h>
#include <tbb/tick_count.h>
#include "NvDecoder/NvDecoder.h"
#include "../Utils/NvCodecUtils.h"
#include "../Utils/FFmpegDemuxer.h"
#include "../Common/AppDecUtils.h"
#include "libgpujpeg/gpujpeg.h"
#include <dirent.h>

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

#define START_TIMER auto start = std::chrono::high_resolution_clock::now();
#define STOP_TIMER(print_message) std::cout << print_message << \
    std::chrono::duration_cast<std::chrono::milliseconds>( \
    std::chrono::high_resolution_clock::now() - start).count() \
    << " ms " << std::endl;

inline std::chrono::high_resolution_clock::time_point Start_time()
{
    return std::chrono::high_resolution_clock::now();
}

inline int64_t Duration(std::chrono::high_resolution_clock::time_point start_time)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
}

void ConvertSemiplanarToPlanar(uint8_t *pHostFrame, int nWidth, int nHeight, int nBitDepth) {
    if (nBitDepth == 8) {
        // nv12->iyuv
        YuvConverter<uint8_t> converter8(nWidth, nHeight);
        converter8.UVInterleavedToPlanar(pHostFrame);
    } else {
        // p016->yuv420p16
        YuvConverter<uint16_t> converter16(nWidth, nHeight);
        converter16.UVInterleavedToPlanar((uint16_t *)pHostFrame);
    }
}

static long int file_count =0;
std::string Generate_file_name(const char *folder_path)
{
    time_t nowTime;
    struct tm fmtTime;
	time(&nowTime);
	localtime_r(&nowTime, &fmtTime);

    int curYear = fmtTime.tm_year + 1900;
    int curMonth = fmtTime.tm_mon + 1;
    int curDay = fmtTime.tm_mday;


    char buf[256];
    int len = snprintf(buf, 256, "%s/%04d_%02d_%02d_%02d%02d%02d_%03ld", folder_path, curYear, curMonth, curDay, fmtTime.tm_hour, fmtTime.tm_min, fmtTime.tm_sec, file_count);
    file_count++;
	std::string filename(buf, len);
	return filename;
}

void Write_a_file(std::string file_name, const char *data, int data_size)
{
    std::ofstream fpOut(file_name, std::ios::app | std::ios::binary);
    if (!fpOut)
    {
        std::ostringstream err;
        err << "Unable to open output file: " << file_name << std::endl;
        throw std::invalid_argument(err.str());
    }

    fpOut.write(data, data_size);

    fpOut.close();
}

int savejpeg(uint8_t *pData, int data_size, const unsigned int &width, const unsigned int &height, const char *folder_path)
{
    struct gpujpeg_parameters param;
	gpujpeg_set_default_parameters(&param);
	param.quality = 60; 
	param.restart_interval = 16;
	param.interleaved = 0;

	//输入图像参数初始化
	struct gpujpeg_image_parameters param_image;
	gpujpeg_image_set_default_parameters(&param_image);
	param_image.width = width;
	param_image.height = height;
	param_image.comp_count = 3;
	param_image.color_space = GPUJPEG_YCBCR_JPEG;
	param_image.pixel_format = GPUJPEG_420_U8_P0P1P2;

    gpujpeg_parameters_chroma_subsampling_420(&param);

    //初始化GPU
    setenv("CUDA_VISIBLE_DEVICES", "0", 1);
    int ret;
	if (ret = gpujpeg_init_device(0, 0))
	{   
		NV_LOG(ERROR) << "gpujpeg_init_device Fail! " << " errorcode: " << ret;
		return -1;
	}

    struct gpujpeg_encoder* encoder;

    encoder  = gpujpeg_encoder_create(NULL);

    if (encoder == NULL) 
	{
		NV_LOG(ERROR) << "gpujpeg_encoder_create Fail!";
		return -1;
	}

    struct gpujpeg_encoder_input encoder_input;
	encoder_input.image = pData;
	encoder_input.type = GPUJPEG_ENCODER_INPUT_IMAGE;

	uint8_t* image_compressed = NULL;
	int image_compressed_size = 0;

    if (gpujpeg_encoder_encode(encoder, &param, &param_image, &encoder_input, &image_compressed, &image_compressed_size) != 0)
	{
		NV_LOG(ERROR) << "In PostProcess::saveJpeg(): gpujpeg_encoder_encode Error!";
		return -1;
	}
	std::string capFile = Generate_file_name(folder_path);
	//std::string capFile_yuv = capFile + ".yuv";
    std::string capFile_jpg = capFile + ".jpg";

	NV_LOG(INFO) << "saveAsFile : " << capFile_jpg;
	//NV_LOG(INFO) << "saveAsFile_yuv : " << capFile_yuv;
	Write_a_file(capFile_jpg,reinterpret_cast<char*>(image_compressed) ,image_compressed_size);
    //Write_a_file(capFile_yuv, reinterpret_cast<char*>(pData), data_size);
	
	gpujpeg_encoder_destroy(encoder);

}

int uint8_t_append(uint8_t *src_data, int src_size, uint8_t *append_data, int append_size)
{
    int i;
    int new_size = src_size + append_size;
    for(i = src_size; i < new_size; i++)
    {
        src_data[i]=append_data[i-src_size];
    }

    return new_size;
}

/**
*   @brief  Function to decode media file and write raw frames into an output file.
*   @param  cuContext     - Handle to CUDA context
*   @param  szInFilePath  - Path to file to be decoded
*   @param  szOutFilePath - Path to output file into which raw frames are stored
*   @param  bOutPlanar    - Flag to indicate whether output needs to be converted to planar format
*   @param  cropRect      - Cropping rectangle coordinates
*   @param  resizeDim     - Resizing dimensions for output
*/
void DecodeMediaFile(CUcontext cuContext, const char *szInFilePath, const char *szOutFilePath, bool bOutPlanar,
    const Rect &cropRect, const Dim &resizeDim)
{
    FFmpegDemuxer demuxer(szInFilePath);
    NvDecoder dec(cuContext, false, FFmpeg2NvCodecId(demuxer.GetVideoCodec()), NULL, false, false, &cropRect, &resizeDim);

    int nVideoBytes = 0, nFrameReturned = 0, nFrame = 0;
    uint8_t *pVideo = NULL, **ppFrame;
    int64_t *ppTimestamp;

    int  interval=100;
    unsigned long interval_count = 0;

    bool bDecodeOutSemiPlanar = false;
    std::string file_name;
    //START_TIMER;
    auto start_time = Start_time();
    do 
    {
        demuxer.Demux(&pVideo, &nVideoBytes);
        dec.Decode(pVideo, nVideoBytes, &ppFrame, &nFrameReturned);
        if (!nFrame && nFrameReturned)
            NV_LOG(INFO) << dec.GetVideoInfo();
        
        bDecodeOutSemiPlanar = (dec.GetOutputFormat() == cudaVideoSurfaceFormat_NV12) || (dec.GetOutputFormat() == cudaVideoSurfaceFormat_P016);

        for (int i = 0; i < nFrameReturned; i++) 
        {
            if (bOutPlanar && bDecodeOutSemiPlanar) 
            {
                ConvertSemiplanarToPlanar(ppFrame[i], dec.GetWidth(), dec.GetHeight(), dec.GetBitDepth());
            }
            interval_count++;
            if(!(interval_count % interval))
            {
                savejpeg(ppFrame[i],dec.GetFrameSize(),dec.GetWidth(), dec.GetHeight(),szOutFilePath);
                /*
                file_name = Generate_file_name(szOutFilePath);
                file_name = file_name + ".yuv";
                NV_LOG(INFO) << "file_name: " << file_name;
                Write_a_file(file_name,reinterpret_cast<char*>(ppFrame[i]),dec.GetFrameSize());
                */
            }
        }
        nFrame += nFrameReturned;
        
        //std::cout << "nFrameReturned: " << nFrameReturned << std::endl;

    } while (nVideoBytes);

    std::vector <std::string> aszDecodeOutFormat = { "NV12", "P016", "YUV444", "YUV444P16" };
    if (bOutPlanar) {
        aszDecodeOutFormat[0] = "iyuv";   aszDecodeOutFormat[1] = "yuv420p16";
    }
    std::cout << "Total frame decoded: " << nFrame << std::endl
            << "Saved in file " << szOutFilePath << " in "
            << aszDecodeOutFormat[dec.GetOutputFormat()]
            << " format" << std::endl;
    
    int64_t duration_time = Duration(start_time);
    NV_LOG(INFO) << "Decode Finish Time: " << float(duration_time / 1000) << " s\nDecoode frame per second = " << nFrame / (duration_time / 1000) << " frames/s";
    //STOP_TIMER("Decode Finish Time: ");
}

void ShowHelpAndExit(const char *szBadOption = NULL)
{
    bool bThrowError = false;
    std::ostringstream oss;
    if (szBadOption) 
    {
        bThrowError = true;
        oss << "Error parsing \"" << szBadOption << "\"" << std::endl;
    }
    oss << "Options:" << std::endl
        << "-i             Input file path" << std::endl
        << "-o             Output file path" << std::endl
        << "-outplanar     Convert output to planar format" << std::endl
        << "-gpu           Ordinal of GPU to use" << std::endl
        << "-File          Input is a file" << std::endl
        << "-rtsp          Input is a rtsp" << std::endl
        << "-crop l,t,r,b  Crop rectangle in left,top,right,bottom (ignored for case 0)" << std::endl
        << "-resize WxH    Resize to dimension W times H (ignored for case 0)" << std::endl
        ;
    oss << std::endl;
    if (bThrowError)
    {
        throw std::invalid_argument(oss.str());
    }
    else
    {
        std::cout << oss.str();
        ShowDecoderCapability();
        exit(0);
    }
}

void ParseCommandLine(int argc, char *argv[], char *szInputFileName, char *szOutputFileName,
    bool &bOutPlanar, int &iGpu, int &File_or_Rtsp, Rect &cropRect, Dim &resizeDim)
{
    std::ostringstream oss;
    int i;
    for (i = 1; i < argc; i++) {
        if (!_stricmp(argv[i], "-h")) {
            ShowHelpAndExit();
        }
        if (!_stricmp(argv[i], "-i")) {
            if (++i == argc) {
                ShowHelpAndExit("-i");
            }
            sprintf(szInputFileName, "%s", argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-o")) {
            if (++i == argc) {
                ShowHelpAndExit("-o");
            }
            sprintf(szOutputFileName, "%s", argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-outplanar")) {
            bOutPlanar = true;
            continue;
        }
        if (!_stricmp(argv[i], "-gpu")) {
            if (++i == argc) {
                ShowHelpAndExit("-gpu");
            }
            iGpu = atoi(argv[i]);
            continue;
        }
        if (!strcasecmp(argv[i], "-file")) 
        {
            File_or_Rtsp = 0;
            continue;
        }
        if (!strcasecmp(argv[i], "-rtsp")) 
        {
            File_or_Rtsp = 1;
            continue;
        }
        if (!_stricmp(argv[i], "-crop")) {
            if (++i == argc || 4 != sscanf(
                    argv[i], "%d,%d,%d,%d",
                    &cropRect.l, &cropRect.t, &cropRect.r, &cropRect.b)) {
                ShowHelpAndExit("-crop");
            }
            if ((cropRect.r - cropRect.l) % 2 == 1 || (cropRect.b - cropRect.t) % 2 == 1) {
                std::cout << "Cropping rect must have width and height of even numbers" << std::endl;
                exit(1);
            }
            continue;
        }
        if (!_stricmp(argv[i], "-resize")) {
            if (++i == argc || 2 != sscanf(argv[i], "%dx%d", &resizeDim.w, &resizeDim.h)) {
                ShowHelpAndExit("-resize");
            }
            if (resizeDim.w % 2 == 1 || resizeDim.h % 2 == 1) {
                std::cout << "Resizing rect must have width and height of even numbers" << std::endl;
                exit(1);
            }
            continue;
        }
        ShowHelpAndExit(argv[i]);
    }

    if(argc == 1)
        ShowHelpAndExit(argv[0]);
}

void CreateFolder(const char *folder)
{
    DIR *dir = NULL;
	if ((dir = opendir(folder)) == NULL)
	{	
		NV_LOG(INFO)<<"Create Folder: "<< folder;
        umask(0);
		if(mkdir(folder, 0777) < 0)
            NV_LOG(ERROR)<<"Create Folder failed: "<< strerror(errno);
	}
	else
		closedir(dir);
}

int main(int argc, char **argv) 
{
    char szInFilePath[256] = "", szOutFilePath[256] = "";
    bool bOutPlanar = false;
    int iGpu = 0;
    int File_or_Rtsp = 0;
    Rect cropRect = {};
    Dim resizeDim = {};
    try
    {
        //解析命令
        ParseCommandLine(argc, argv, szInFilePath, szOutFilePath, bOutPlanar, iGpu, File_or_Rtsp, cropRect, resizeDim);
        if(!File_or_Rtsp)
        {
            NV_LOG(INFO) << "Input is File: " << szInFilePath;
            CheckInputFile(szInFilePath);
        }
        else
        {
            NV_LOG(INFO) << "Input is rstp: " << szInFilePath;
        }

        if (!*szOutFilePath) {
            sprintf(szOutFilePath, bOutPlanar ? "out.planar" : "out.native");
        }

        CreateFolder(szOutFilePath);

        //初始化GPU
        ck(cuInit(0));

        //获取GPU数量
        int nGpu = 0;
        ck(cuDeviceGetCount(&nGpu));
        if (iGpu < 0 || iGpu >= nGpu) {
            std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
            return 1;
        }

        CUcontext cuContext = NULL;

        //创建对应GPU编号的上下文
        createCudaContext(&cuContext, iGpu, 0);

        std::cout << "Decode with demuxing." << std::endl;
        DecodeMediaFile(cuContext, szInFilePath, szOutFilePath, bOutPlanar, cropRect, resizeDim);
        
    }
    catch (const std::exception& ex)
    {
        std::cout << ex.what();
        exit(1);
    }

    return 0;
}
