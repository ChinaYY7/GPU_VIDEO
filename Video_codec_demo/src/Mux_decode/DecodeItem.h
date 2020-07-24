#pragma once
#include <cuda.h>
#include <glog/logging.h>
#include <mutex>
#include <stdint.h>
#include <vector>
#include <string>
#include <assert.h>
#include <string>
#include <iostream>
#include <sstream>
#include <string.h>
#include <chrono>
#include <thread>
#include <algorithm>

#include "nvcuvid.h"
#include "common.h"

namespace GpuDecode
{
    static const int IF = 0;
    static const int WAR = 1;
    static const int ER = 2;

    struct Rect 
    {
        int l, t, r, b;
    };

    struct Dim 
    {
        int w, h;
    };

    class DecodeItem
    {
        private:
            //GPU相关变量
            CUcontext m_cuContext; 
            int m_nGpu, m_iGpu;
            CUdevice m_cuDevice;
            char m_DeviceName[80];
                
            //GPU解码器相关
            CUvideoctxlock m_ctxLock;
            cudaVideoCodec m_eCodec;                                //GPU编码器id
            std::mutex *m_pMutex;                                   //互斥信号量指针，若是需要加锁的场景，需提供一个信号量
            CUvideoparser m_hParser;                                //解析器句柄
            CUvideodecoder m_hDecoder;                              //解码器句柄
            int m_nDecodedFrame;                                    //调用一次decode中解码完成的帧数  
            int m_nDecodePicCnt, m_nPicNumInDecodeOrder[32];        //一个缓存保存一段时间内进行解码的帧编号
            int m_nFrameAlloc;                                      //申请的用于保存解码完成的帧数据的缓存计数
            bool m_bUseDeviceFrame;                                 //使用GPU显存来保存解码完成后的数据的标志
            bool m_bDeviceFramePitched;                             //在GPU中以pitch为单位的标志
            size_t m_nDeviceFramePitch;                             //pitch的大小
            
            std::vector<uint8_t *> m_vpFrame;                       //保存解码完成的帧数据的缓存地址                    
            std::vector<uint8_t *> m_vpFrameRet;                    //上一个变量的一个缓存
            std::vector<int64_t> m_vTimestamp;                      //保存解码完成的帧数据的时间戳
            std::mutex m_mtxVPFrame;                                //操作m_vpFrame的互斥量
            std::ostringstream m_videoInfo;                         //保存视频源信息的输出字符串
            
            unsigned int m_nWidth, m_nLumaHeight, m_nChromaHeight;  //输出的维度信息
            unsigned int m_nMaxWidth, m_nMaxHeight;                 //当前最大分辨率
            CUVIDEOFORMAT m_videoFormat;                            //保存当前解码视频的格式信息，用于当视频源格式发生变化时进行判断。
            cudaVideoChromaFormat m_eChromaFormat;                  //视频源的色彩格式
            cudaVideoSurfaceFormat m_eOutputFormat;                 //转码后的输出色彩格式
            int m_nBitDepthMinus8;                                  //色彩格式的位数（0：8位，1：16位）
            int m_nBPP;                                             //暂时不清楚，计算帧大小有用
            Rect m_cropRect;                                        //视频源输出显示尺寸，不设置则和原视频一致
            Rect m_displayRect;                                     //保存实际视频源输出尺寸
            Dim m_resizeDim;                                        //视频源输出分辨率，不设置则和原视频一致
            unsigned int m_nNumChromaPlanes;                        //暂时不清楚，计算帧大小有用
            CUstream m_cuvidStream;                                 //暂时不清楚
                
            // height of the mapped surface 
            int m_nSurfaceHeight = 0;                               //暂时不清楚，和GPU显存分配有关
            int m_nSurfaceWidth = 0;

            bool m_bReconfigExternal = false;                       //暂时不清楚，和重新配置解码器有关
            bool m_bReconfigExtPPChange = false;

            //业务相关
            uint64_t m_video_id;
            bool bDecodeOutSemiPlanar;
            uint64_t total_decode_frame;
            std::chrono::high_resolution_clock::time_point start_time;
            int64_t duration_time;
            float decode_speed;

            bool check(int e, const char *func_name, int Log_level = ER)
            {
                if (e < 0) 
                {
                    switch (Log_level)
                    {
                        case IF:
                            LOG(INFO) << "\"" << func_name << "\": someting wrong and error number is" << e;
                            break;
                        case WAR:
                            LOG(WARNING) << "\"" << func_name << "\": someting wrong and error number is" << e;
                            break;
                        case ER:
                            LOG(ERROR) << "\"" << func_name << "\": someting wrong and error number is" << e;
                            break;
                        default:
                            LOG(WARNING) << "check: not support Log_level";
                            break;
                    }
                    return false;
                }
                return true;
            }

            bool check(CUresult errorCode, const char *func_name, int Log_level = ER)
            {
                const char *szErrName = NULL; 
                if( errorCode != CUDA_SUCCESS)                                                                             
                {                                                                                                         
                    switch (Log_level)
                    {
                        cuGetErrorName(errorCode, &szErrName); 
                        case IF:
                            LOG(INFO) << "\"" << func_name << "\": " << szErrName;
                            break;
                        case WAR:
                            LOG(WARNING) << "\"" << func_name << "\": " << szErrName;
                            break;
                        case ER:
                            LOG(ERROR) << "\"" << func_name << "\": " << szErrName << " errorCode" << errorCode;
                            break;
                        default:
                            LOG(WARNING) << "check: not support Log_level";
                            break;
                    }
                    return false;
                }      
            }

            const char * GetVideoCodecString(cudaVideoCodec eCodec) 
            {
                static struct 
                {
                    cudaVideoCodec eCodec;
                    const char *name;
                } aCodecName [] = {
                    { cudaVideoCodec_MPEG1,     "MPEG-1"       },
                    { cudaVideoCodec_MPEG2,     "MPEG-2"       },
                    { cudaVideoCodec_MPEG4,     "MPEG-4 (ASP)" },
                    { cudaVideoCodec_VC1,       "VC-1/WMV"     },
                    { cudaVideoCodec_H264,      "AVC/H.264"    },
                    { cudaVideoCodec_JPEG,      "M-JPEG"       },
                    { cudaVideoCodec_H264_SVC,  "H.264/SVC"    },
                    { cudaVideoCodec_H264_MVC,  "H.264/MVC"    },
                    { cudaVideoCodec_HEVC,      "H.265/HEVC"   },
                    { cudaVideoCodec_VP8,       "VP8"          },
                    { cudaVideoCodec_VP9,       "VP9"          },
                    { cudaVideoCodec_NumCodecs, "Invalid"      },
                    { cudaVideoCodec_YUV420,    "YUV  4:2:0"   },
                    { cudaVideoCodec_YV12,      "YV12 4:2:0"   },
                    { cudaVideoCodec_NV12,      "NV12 4:2:0"   },
                    { cudaVideoCodec_YUYV,      "YUYV 4:2:2"   },
                    { cudaVideoCodec_UYVY,      "UYVY 4:2:2"   },
                };

                //压缩后的格式
                if (eCodec >= 0 && eCodec <= cudaVideoCodec_NumCodecs)
                    return aCodecName[eCodec].name;

                //未压缩格式
                for (int i = cudaVideoCodec_NumCodecs + 1; i < sizeof(aCodecName) / sizeof(aCodecName[0]); i++) 
                {
                    if (eCodec == aCodecName[i].eCodec) 
                        return aCodecName[eCodec].name;
                }
                return "Unknown";
            }

            const char * GetVideoChromaFormatString(cudaVideoChromaFormat eChromaFormat) 
            {
                static struct 
                {
                    cudaVideoChromaFormat eChromaFormat;
                    const char *name;
                } aChromaFormatName[] = {
                    { cudaVideoChromaFormat_Monochrome, "YUV 400 (Monochrome)" },
                    { cudaVideoChromaFormat_420,        "YUV 420"              },
                    { cudaVideoChromaFormat_422,        "YUV 422"              },
                    { cudaVideoChromaFormat_444,        "YUV 444"              },
                };

                if (eChromaFormat >= 0 && eChromaFormat < sizeof(aChromaFormatName) / sizeof(aChromaFormatName[0])) 
                    return aChromaFormatName[eChromaFormat].name;

                return "Unknown";
            }

            float GetChromaHeightFactor(cudaVideoChromaFormat eChromaFormat)
            {
                float factor = 0.5;
                switch (eChromaFormat)
                {
                    case cudaVideoChromaFormat_Monochrome:
                        factor = 0.0;
                        break;
                    case cudaVideoChromaFormat_420:
                        factor = 0.5;
                        break;
                    case cudaVideoChromaFormat_422:
                        factor = 1.0;
                        break;
                    case cudaVideoChromaFormat_444:
                        factor = 1.0;
                        break;
                }

                return factor;
            }

            int GetChromaPlaneCount(cudaVideoChromaFormat eChromaFormat)
            {
                int numPlane = 1;
                switch (eChromaFormat)
                {
                    case cudaVideoChromaFormat_Monochrome:
                        numPlane = 0;
                        break;
                    case cudaVideoChromaFormat_420:
                        numPlane = 1;
                        break;
                    case cudaVideoChromaFormat_444:
                        numPlane = 2;
                        break;
                }

                return numPlane;
            }

            //在解码帧或帧格式改变之前的回调函数
            static int CUDAAPI HandleVideoSequenceProc(void *pUserData, CUVIDEOFORMAT *pVideoFormat) { return ((DecodeItem *)pUserData)->HandleVideoSequence(pVideoFormat); }

            //当图片准备解码时的回调函数(解码顺序)
            static int CUDAAPI HandlePictureDecodeProc(void *pUserData, CUVIDPICPARAMS *pPicParams) { return ((DecodeItem *)pUserData)->HandlePictureDecode(pPicParams); }

            //当图片准备好显示时的回调函数(显示顺序)
            static int CUDAAPI HandlePictureDisplayProc(void *pUserData, CUVIDPARSERDISPINFO *pDispInfo) { return ((DecodeItem *)pUserData)->HandlePictureDisplay(pDispInfo); }

            //当发生视频源格式发生变化时，不用重新创建解码器，只需重新配置
            int ReconfigureDecoder(CUVIDEOFORMAT *pVideoFormat);

            int HandleVideoSequence(CUVIDEOFORMAT *pVideoFormat);
            int HandlePictureDecode(CUVIDPICPARAMS *pPicParams);
            int HandlePictureDisplay(CUVIDPARSERDISPINFO *pDispInfo);

        public:
            DecodeItem(int iGpu);
            DecodeItem() : m_iGpu(-1)
            {
                LOG(WARNING) << "Please input Gpu number";
                return;
            }
            ~DecodeItem();

            void Set_ecodec(cudaVideoCodec eCodec)
            {
                m_eCodec = eCodec;
            }

            void Set_video_id(uint64_t video_id)
            {
                m_video_id = video_id;
            }

            void Set_bDecodeOutSemiPlanar(bool state)
            {
                bDecodeOutSemiPlanar = state;
            }

            void Set_output_cropRect(const Rect *pCropRect)
            {
                m_cropRect = *pCropRect;
            }

            void Set_output_resizeDim(const Dim *pResizeDim)
            {
                m_resizeDim = *pResizeDim;
            }

            void increase_total_decode_frame(int num)
            {
                total_decode_frame+=num;
            }

            uint64_t Get_total_decode_frame()
            {
                return total_decode_frame;
            }

            std::string GetVideoInfo() const 
            { 
                return m_videoInfo.str(); 
            }

            cudaVideoSurfaceFormat GetOutputFormat() 
            { 
                return m_eOutputFormat; 
            }

            int GetWidth() 
            { 
                assert(m_nWidth); 
                return m_nWidth; 
            }

            int GetHeight() 
            { 
                assert(m_nLumaHeight); 
                return m_nLumaHeight; 
            }

            int GetChromaHeight() 
            { 
                assert(m_nChromaHeight); 
                return m_nChromaHeight; 
            }

            int GetBitDepth() 
            { 
                assert(m_nWidth); 
                return m_nBitDepthMinus8 + 8; 
            }

            uint64_t Get_video_id()
            {
                return m_video_id;
            }

            bool Get_bDecodeOutSemiPlanar()
            {
                return bDecodeOutSemiPlanar;
            }

            float Get_decode_speed()
            {
                duration_time = NvCodec::common::getInstance()->Duration(start_time);
                decode_speed = total_decode_frame / ((float)duration_time / 1000);
            }

            int64_t Get_duration_time()
            {
                duration_time = NvCodec::common::getInstance()->Duration(start_time);
                return duration_time;
            }

            /*
            * 功能：
            *   获得未压缩的一帧数据大小
            * 返回值：
            *   帧数据大小
            */
            int GetFrameSize() 
            { 
                assert(m_nWidth); 
                return m_nWidth * (m_nLumaHeight + m_nChromaHeight * m_nNumChromaPlanes) * m_nBPP; 
            }

            bool Init(int ctxflag = 0, bool bLowLatency = false);

            /*
            * 功能：
            *   解码帧数据并返回已完成解码的帧数据，在下一次调用解码前，解码帧数据buffer中的数据必须被使用，否则会被覆盖
            * 参数：
            *   pData：未解码帧数据
            *   nSize：未解码帧数据大小
            *   pppFrame：当前已完成解码的帧被保存的buffer地址
            *   pnFrameReturned：当前获得的已解码帧数量
            *   ppTimestamp：当前已完成解码的帧的时间戳被保存的buffer地址
            *   timestamp：未解码帧的时间戳
            *   flags：解码选项
            * 返回值：
            *   执行结果
            */
            bool Decode(const uint8_t *pData, int nSize, uint8_t ***pppFrame, int *pnFrameReturned, int64_t **ppTimestamp = NULL, int64_t timestamp = 0, uint32_t flags = 0);

            /*
            * 功能：
            *   解码帧数据并返回已完成解码的帧数据，其数据进行了缓存保护，该部分缓存不能写入，直到使用UnlockFrame，才能继续写入
            * 参数：
            *   pData：未解码帧数据
            *   nSize：未解码帧数据大小
            *   pppFrame：当前已完成解码的帧被保存的buffer地址
            *   pnFrameReturned：当前获得的已解码帧数量
            *   ppTimestamp：当前已完成解码的帧的时间戳被保存的buffer地址
            *   timestamp：未解码帧的时间戳
            *   flags：解码选项
            * 返回值：
            *   执行结果
            */
            bool DecodeLockFrame(const uint8_t *pData, int nSize, uint8_t ***pppFrame, int *pnFrameReturned, int64_t **ppTimestamp = NULL, int64_t timestamp = 0, uint32_t flags = 0);

            /*
            * 功能：
            *   解除帧数据缓存的保护，使得可以该部分缓存可以再被写入
            * 参数：
            *   ppFrame：需解除保护的帧数据缓存的首地址
            *   nFrame：帧数据缓存大小
            */
            void UnlockFrame(uint8_t **ppFrame, int nFrame);

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
    };
}