#include <tbb/tick_count.h>
#include "DecodeItem.h"
#include "YuvConverter.h"
#include "common.h"

namespace GpuDecode
{
    DecodeItem::DecodeItem(int iGpu)
    {
        m_iGpu = iGpu;
        m_eCodec = cudaVideoCodec_NumCodecs;
        m_pMutex = NULL;
        m_hParser = NULL;
        m_hDecoder = NULL;
        m_nDecodedFrame = 0;
        m_nDecodePicCnt = 0;
        m_nFrameAlloc = 0;
        m_bUseDeviceFrame = false;
        m_bDeviceFramePitched = false;
        m_nDeviceFramePitch = 0;
        m_nWidth = 0;
        m_nLumaHeight = 0;
        m_nChromaHeight = 0;
        m_nMaxWidth = 0;
        m_nMaxHeight = 0;
        m_videoFormat = {};
        m_nBitDepthMinus8 = 0;
        m_nBPP = 1;
        m_cropRect = {};
        m_displayRect = {};
        m_resizeDim = {};
        m_nNumChromaPlanes = 0;
        m_cuvidStream = 0;
        m_video_id = 0;
        bDecodeOutSemiPlanar = false;
        total_decode_frame = 0;
    }

    DecodeItem::~DecodeItem()
    {
        //START_TIMER
        cuCtxPushCurrent(m_cuContext);
        cuCtxPopCurrent(NULL);

        if (m_hParser)
            cuvidDestroyVideoParser(m_hParser);

        if (m_hDecoder) 
        {
            if (m_pMutex) 
                m_pMutex->lock();

            cuvidDestroyDecoder(m_hDecoder);

            if (m_pMutex) 
                m_pMutex->unlock();
        }

        std::lock_guard<std::mutex> lock(m_mtxVPFrame);
        if (m_vpFrame.size() != m_nFrameAlloc)
        {
            LOG(WARNING) << "nFrameAlloc(" << m_nFrameAlloc << ") != m_vpFrame.size()(" << m_vpFrame.size() << ")";
        }
        for (uint8_t *pFrame : m_vpFrame)
        {
            if (m_bUseDeviceFrame)
            {
                if (m_pMutex) 
                    m_pMutex->lock();

                cuCtxPushCurrent(m_cuContext);
                cuMemFree((CUdeviceptr)pFrame);
                cuCtxPopCurrent(NULL);

                if (m_pMutex) 
                    m_pMutex->unlock();
            }
            else
                delete[] pFrame;
        }

        cuvidCtxLockDestroy(m_ctxLock);
        //STOP_TIMER("Gpu Decode Session Deinitialization Time: ");
    }

    bool DecodeItem::Init(int ctxflag, bool bLowLatency)
    {
        if(m_iGpu < 0)
        {
            LOG(ERROR) << "Not input Gpu number";
            return false;
        }

        //初始化GPU
        check(cuInit(0), "cuInit");

        //获取GPU数量
        check(cuDeviceGetCount(&m_nGpu), "cuDeviceGetCount");
        if (m_iGpu < 0 || m_iGpu >= m_nGpu) 
        {
            LOG(ERROR) << "GPU: " << m_iGpu <<" ordinal out of range. Should be within [" << 0 << ", " << m_nGpu - 1 << "]" << std::endl;
            return false;
        }

        std::string device = std::to_string(m_iGpu);
        setenv("CUDA_VISIBLE_DEVICES", "0,1", 1);  //使GPU设备可见

        //获取GPU编号对应的GPU句柄
        check(cuDeviceGet(&m_cuDevice, m_iGpu), "cuDeviceGet");

        //获得GPU句柄对应的GPU字符串标识
        check(cuDeviceGetName(m_DeviceName, sizeof(m_DeviceName), m_cuDevice), "cuDeviceGetName");
        LOG(WARNING) << "GPU-"<< device << "(" << m_DeviceName << ") in use and server for video: " << m_video_id;

        //创建GPU句柄对应的上下文
        check(cuCtxCreate(&m_cuContext, ctxflag, m_cuDevice), "cuCtxCreate");

        //初始化一个解析器
        check(cuvidCtxLockCreate(&m_ctxLock, m_cuContext), "cuvidCtxLockCreate");

        if(m_eCodec == cudaVideoCodec_NumCodecs)
        {
            LOG(ERROR) << "Not set ecodec";
            return false;
        }

        CUVIDPARSERPARAMS videoParserParameters = {};
        videoParserParameters.CodecType = m_eCodec;
        videoParserParameters.ulMaxNumDecodeSurfaces = 1;
        videoParserParameters.ulMaxDisplayDelay = bLowLatency ? 0 : 1;
        videoParserParameters.pUserData = this;
        videoParserParameters.pfnSequenceCallback = HandleVideoSequenceProc;
        videoParserParameters.pfnDecodePicture = HandlePictureDecodeProc;
        videoParserParameters.pfnDisplayPicture = HandlePictureDisplayProc;

        if (m_pMutex) 
            m_pMutex->lock();
        
        check(cuvidCreateVideoParser(&m_hParser, &videoParserParameters), "cuvidCreateVideoParser");

        if (m_pMutex) 
            m_pMutex->unlock();
    }

    bool DecodeItem::Decode(const uint8_t *pData, int nSize, uint8_t ***pppFrame, int *pnFrameReturned, int64_t **ppTimestamp, int64_t timestamp, uint32_t flags)
    {
        if (!m_hParser)
        {
            LOG(ERROR) << "Parser not initialized. And error code: " << CUDA_ERROR_NOT_INITIALIZED;
            exit(0);
            return false;
        }
        
        m_nDecodedFrame = 0;
        CUVIDSOURCEDATAPACKET packet = {0};
        packet.payload = pData;
        packet.payload_size = nSize;
        packet.flags = flags | CUVID_PKT_TIMESTAMP;
        packet.timestamp = timestamp;
        if (!pData || nSize == 0) 
            packet.flags |= CUVID_PKT_ENDOFSTREAM;
    
        
        //LOG(INFO) << "Start cuvidParseVideoData";
        if (m_pMutex) 
            m_pMutex->lock();

        check(cuvidParseVideoData(m_hParser, &packet), "cuvidParseVideoData");

        if (m_pMutex) 
            m_pMutex->unlock();
        //LOG(INFO) << "Finish cuvidParseVideoData";
        

        if (m_nDecodedFrame > 0)
        {
            if (pppFrame)
            {
                m_vpFrameRet.clear();
                std::lock_guard<std::mutex> lock(m_mtxVPFrame);
                m_vpFrameRet.insert(m_vpFrameRet.begin(), m_vpFrame.begin(), m_vpFrame.begin() + m_nDecodedFrame);
                *pppFrame = &m_vpFrameRet[0];
            }
            if (ppTimestamp)
            {
                *ppTimestamp = &m_vTimestamp[0];
            }
        }
        if (pnFrameReturned)
        {
            *pnFrameReturned = m_nDecodedFrame;
        }
        return true;
    }

    bool DecodeItem::DecodeLockFrame(const uint8_t *pData, int nSize, uint8_t ***pppFrame, int *pnFrameReturned, int64_t **ppTimestamp, int64_t timestamp, uint32_t flags)
    {
        bool ret = Decode(pData, nSize, pppFrame, pnFrameReturned, ppTimestamp, timestamp, flags);
        std::lock_guard<std::mutex> lock(m_mtxVPFrame);
        m_vpFrame.erase(m_vpFrame.begin(), m_vpFrame.begin() + m_nDecodedFrame);
        return true;
    }

    void DecodeItem::UnlockFrame(uint8_t **ppFrame, int nFrame)
    {
        std::lock_guard<std::mutex> lock(m_mtxVPFrame);
        m_vpFrame.insert(m_vpFrame.end(), &ppFrame[0], &ppFrame[nFrame]);
    }

    int DecodeItem::ReconfigureDecoder(CUVIDEOFORMAT *pVideoFormat)
    {
        //判断是否可以重新配置
        if (pVideoFormat->bit_depth_luma_minus8 != m_videoFormat.bit_depth_luma_minus8 || pVideoFormat->bit_depth_chroma_minus8 != m_videoFormat.bit_depth_chroma_minus8)
        {
            LOG(ERROR) << "Reconfigure Not supported for bit depth change: " << CUDA_ERROR_NOT_SUPPORTED;
            return 1;
        }
            

        if (pVideoFormat->chroma_format != m_videoFormat.chroma_format) 
        {
            LOG(ERROR) << "Reconfigure Not supported for chroma format change: " << CUDA_ERROR_NOT_SUPPORTED;
            return 1;
        }
            
        
        //分辨率改变
        bool bDecodeResChange = !(pVideoFormat->coded_width == m_videoFormat.coded_width && pVideoFormat->coded_height == m_videoFormat.coded_height);

        //显示尺寸改变
        bool bDisplayRectChange = !(pVideoFormat->display_area.bottom == m_videoFormat.display_area.bottom && pVideoFormat->display_area.top == m_videoFormat.display_area.top \
            && pVideoFormat->display_area.left == m_videoFormat.display_area.left && pVideoFormat->display_area.right == m_videoFormat.display_area.right);

        int nDecodeSurface = pVideoFormat->min_num_decode_surfaces;

        if ((pVideoFormat->coded_width > m_nMaxWidth) || (pVideoFormat->coded_height > m_nMaxHeight)) 
        {
            // 对于 VP9格式, 可以让硬件去自动处理该种情况 
            if ((m_eCodec != cudaVideoCodec_VP9) || m_bReconfigExternal)
                LOG(ERROR) << "Reconfigure Not supported when width/height > maxwidth/maxheight: " << CUDA_ERROR_NOT_SUPPORTED;

            return 1;
        }

        if (!bDecodeResChange && !m_bReconfigExtPPChange) 
        {
            // if the coded_width/coded_height hasn't changed but display resolution has changed, then need to update width/height for
            // correct output without cropping. Example : 1920x1080 vs 1920x1088
            if (bDisplayRectChange)
            {
                m_nWidth = pVideoFormat->display_area.right - pVideoFormat->display_area.left;
                m_nLumaHeight = pVideoFormat->display_area.bottom - pVideoFormat->display_area.top;
                m_nChromaHeight = int(m_nLumaHeight * GetChromaHeightFactor(pVideoFormat->chroma_format));
                m_nNumChromaPlanes = GetChromaPlaneCount(pVideoFormat->chroma_format);
            }

            // no need for reconfigureDecoder(). Just return
            return 1;
        }

        CUVIDRECONFIGUREDECODERINFO reconfigParams = { 0 };

        reconfigParams.ulWidth = m_videoFormat.coded_width = pVideoFormat->coded_width;
        reconfigParams.ulHeight = m_videoFormat.coded_height = pVideoFormat->coded_height;

        // Dont change display rect and get scaled output from decoder. This will help display app to present apps smoothly
        reconfigParams.display_area.bottom = m_displayRect.b;
        reconfigParams.display_area.top = m_displayRect.t;
        reconfigParams.display_area.left = m_displayRect.l;
        reconfigParams.display_area.right = m_displayRect.r;
        reconfigParams.ulTargetWidth = m_nSurfaceWidth;
        reconfigParams.ulTargetHeight = m_nSurfaceHeight;

        // If external reconfigure is called along with resolution change even if post processing params is not changed,
        // do full reconfigure params update
        if ((m_bReconfigExternal && bDecodeResChange) || m_bReconfigExtPPChange) 
        {
            // update display rect and target resolution if requested explicitely
            m_bReconfigExternal = false;
            m_bReconfigExtPPChange = false;
            m_videoFormat = *pVideoFormat;
            if (!(m_cropRect.r && m_cropRect.b) && !(m_resizeDim.w && m_resizeDim.h)) 
            {
                m_nWidth = pVideoFormat->display_area.right - pVideoFormat->display_area.left;
                m_nLumaHeight = pVideoFormat->display_area.bottom - pVideoFormat->display_area.top;
                reconfigParams.ulTargetWidth = pVideoFormat->coded_width;
                reconfigParams.ulTargetHeight = pVideoFormat->coded_height;
            }
            else 
            {
                if (m_resizeDim.w && m_resizeDim.h) 
                {
                    reconfigParams.display_area.left = pVideoFormat->display_area.left;
                    reconfigParams.display_area.top = pVideoFormat->display_area.top;
                    reconfigParams.display_area.right = pVideoFormat->display_area.right;
                    reconfigParams.display_area.bottom = pVideoFormat->display_area.bottom;
                    m_nWidth = m_resizeDim.w;
                    m_nLumaHeight = m_resizeDim.h;
                }

                if (m_cropRect.r && m_cropRect.b) 
                {
                    reconfigParams.display_area.left = m_cropRect.l;
                    reconfigParams.display_area.top = m_cropRect.t;
                    reconfigParams.display_area.right = m_cropRect.r;
                    reconfigParams.display_area.bottom = m_cropRect.b;
                    m_nWidth = m_cropRect.r - m_cropRect.l;
                    m_nLumaHeight = m_cropRect.b - m_cropRect.t;
                }
                reconfigParams.ulTargetWidth = m_nWidth;
                reconfigParams.ulTargetHeight = m_nLumaHeight;
            }

            m_nChromaHeight = int(m_nLumaHeight * GetChromaHeightFactor(pVideoFormat->chroma_format));
            m_nNumChromaPlanes = GetChromaPlaneCount(pVideoFormat->chroma_format);
            m_nSurfaceHeight = reconfigParams.ulTargetHeight;
            m_nSurfaceWidth = reconfigParams.ulTargetWidth;
            m_displayRect.b = reconfigParams.display_area.bottom;
            m_displayRect.t = reconfigParams.display_area.top;
            m_displayRect.l = reconfigParams.display_area.left;
            m_displayRect.r = reconfigParams.display_area.right;
        }

        reconfigParams.ulNumDecodeSurfaces = nDecodeSurface;

        START_TIMER

        check(cuCtxPushCurrent(m_cuContext), "cuCtxPushCurrent");
        check(cuvidReconfigureDecoder(m_hDecoder, &reconfigParams), "cuvidReconfigureDecoder");
        check(cuCtxPopCurrent(NULL), "cuCtxPopCurrent");

        STOP_TIMER("Session Reconfigure Time: ");

        return nDecodeSurface;
    }

    int DecodeItem::HandleVideoSequence(CUVIDEOFORMAT *pVideoFormat)
    {
        START_TIMER
        //LOG(WARNING)<<"HandleVideoSequence::Create or change decoder";

        //获取解码的视频源信息
        m_videoInfo.str("");
        m_videoInfo.clear();
        m_videoInfo << "\nVideo-" << m_video_id<< ": Input Information" << std::endl
            << "\tCodec        : " << GetVideoCodecString(pVideoFormat->codec) << std::endl
            << "\tFrame rate   : " << pVideoFormat->frame_rate.numerator << "/" << pVideoFormat->frame_rate.denominator
                << " = " << 1.0 * pVideoFormat->frame_rate.numerator / pVideoFormat->frame_rate.denominator << " fps" << std::endl
            << "\tSequence     : " << (pVideoFormat->progressive_sequence ? "Progressive" : "Interlaced") << std::endl
            << "\tCoded size   : [" << pVideoFormat->coded_width << ", " << pVideoFormat->coded_height << "]" << std::endl
            << "\tDisplay area : [" << pVideoFormat->display_area.left << ", " << pVideoFormat->display_area.top << ", "
                << pVideoFormat->display_area.right << ", " << pVideoFormat->display_area.bottom << "]" << std::endl
            << "\tChroma       : " << GetVideoChromaFormatString(pVideoFormat->chroma_format) << std::endl
            << "\tBit depth    : " << pVideoFormat->bit_depth_luma_minus8 + 8;

        m_videoInfo << std::endl;

        //判断需要解码的视频，在当前GPU中是否支持解码
        int nDecodeSurface = pVideoFormat->min_num_decode_surfaces;

        CUVIDDECODECAPS decodecaps;
        memset(&decodecaps, 0, sizeof(decodecaps));

        decodecaps.eCodecType = pVideoFormat->codec;
        decodecaps.eChromaFormat = pVideoFormat->chroma_format;
        decodecaps.nBitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8;

        check(cuCtxPushCurrent(m_cuContext), "cuCtxPushCurrent");
        check(cuvidGetDecoderCaps(&decodecaps), "cuvidGetDecoderCaps");
        check(cuCtxPopCurrent(NULL), "cuCtxPopCurrent");

        if(!decodecaps.bIsSupported)
        {
            LOG(ERROR) << "Codec not supported on this GPU. And error code: " << CUDA_ERROR_NOT_SUPPORTED;
            return nDecodeSurface;
        }

        if ((pVideoFormat->coded_width > decodecaps.nMaxWidth) || (pVideoFormat->coded_height > decodecaps.nMaxHeight))
        {

            std::ostringstream errorString;
            errorString << std::endl
                        << "Resolution          : " << pVideoFormat->coded_width << "x" << pVideoFormat->coded_height << std::endl
                        << "Max Supported (wxh) : " << decodecaps.nMaxWidth << "x" << decodecaps.nMaxHeight << std::endl
                        << "Resolution not supported on this GPU";

            const std::string cErr = errorString.str();
            LOG(ERROR) << cErr << " And error code: " << CUDA_ERROR_NOT_SUPPORTED;
            return nDecodeSurface;
        }

        if ((pVideoFormat->coded_width>>4)*(pVideoFormat->coded_height>>4) > decodecaps.nMaxMBCount)
        {

            std::ostringstream errorString;
            errorString << std::endl
                        << "MBCount             : " << (pVideoFormat->coded_width >> 4)*(pVideoFormat->coded_height >> 4) << std::endl
                        << "Max Supported mbcnt : " << decodecaps.nMaxMBCount << std::endl
                        << "MBCount not supported on this GPU";
        
            const std::string cErr = errorString.str();
            LOG(ERROR) << cErr << " And error code: " << CUDA_ERROR_NOT_SUPPORTED;
            return nDecodeSurface;
        }

        if (m_nWidth && m_nLumaHeight && m_nChromaHeight) 
        {
            // cuvidCreateDecoder() has been called before, and now there's possible config change
            return ReconfigureDecoder(pVideoFormat);
        }

        // eCodec 已经在初始化之前设置了，这里再设置一次是为了潜在的纠正
        m_eCodec = pVideoFormat->codec;
        m_eChromaFormat = pVideoFormat->chroma_format;
        m_nBitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8;
        m_nBPP = m_nBitDepthMinus8 > 0 ? 2 : 1;

        if (m_eChromaFormat == cudaVideoChromaFormat_420)
            m_eOutputFormat = pVideoFormat->bit_depth_luma_minus8 ? cudaVideoSurfaceFormat_P016 : cudaVideoSurfaceFormat_NV12;
        else if (m_eChromaFormat == cudaVideoChromaFormat_444)
            m_eOutputFormat = pVideoFormat->bit_depth_luma_minus8 ? cudaVideoSurfaceFormat_YUV444_16Bit : cudaVideoSurfaceFormat_YUV444;

        m_videoFormat = *pVideoFormat;

        //创建解码器
        CUVIDDECODECREATEINFO videoDecodeCreateInfo = { 0 };
        videoDecodeCreateInfo.CodecType = pVideoFormat->codec;
        videoDecodeCreateInfo.ChromaFormat = pVideoFormat->chroma_format;
        videoDecodeCreateInfo.OutputFormat = m_eOutputFormat;
        videoDecodeCreateInfo.bitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8;
        if (pVideoFormat->progressive_sequence)
            videoDecodeCreateInfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
        else
            videoDecodeCreateInfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Adaptive;
        videoDecodeCreateInfo.ulNumOutputSurfaces = 2;
        // With PreferCUVID, JPEG is still decoded by CUDA while video is decoded by NVDEC hardware
        videoDecodeCreateInfo.ulCreationFlags = cudaVideoCreate_PreferCUVID;
        videoDecodeCreateInfo.ulNumDecodeSurfaces = nDecodeSurface;
        videoDecodeCreateInfo.vidLock = m_ctxLock;
        videoDecodeCreateInfo.ulWidth = pVideoFormat->coded_width;
        videoDecodeCreateInfo.ulHeight = pVideoFormat->coded_height;
        if (m_nMaxWidth < (int)pVideoFormat->coded_width)
            m_nMaxWidth = pVideoFormat->coded_width;
        if (m_nMaxHeight < (int)pVideoFormat->coded_height)
            m_nMaxHeight = pVideoFormat->coded_height;
        videoDecodeCreateInfo.ulMaxWidth = m_nMaxWidth;
        videoDecodeCreateInfo.ulMaxHeight = m_nMaxHeight;

        //配置视频输出的缩放和裁剪(不需要进行缩放和裁剪的情况)
        if (!(m_cropRect.r && m_cropRect.b) && !(m_resizeDim.w && m_resizeDim.h)) 
        {
            //和输入视频的一致
            m_nWidth = pVideoFormat->display_area.right - pVideoFormat->display_area.left;
            m_nLumaHeight = pVideoFormat->display_area.bottom - pVideoFormat->display_area.top;
            videoDecodeCreateInfo.ulTargetWidth = pVideoFormat->coded_width;
            videoDecodeCreateInfo.ulTargetHeight = pVideoFormat->coded_height;
        } 
        else 
        {
            //调节分辨率，只改变分辨率，不改变显示尺寸
            if (m_resizeDim.w && m_resizeDim.h) 
            {
                //显示尺寸
                videoDecodeCreateInfo.display_area.left = pVideoFormat->display_area.left;
                videoDecodeCreateInfo.display_area.top = pVideoFormat->display_area.top;
                videoDecodeCreateInfo.display_area.right = pVideoFormat->display_area.right;
                videoDecodeCreateInfo.display_area.bottom = pVideoFormat->display_area.bottom;

                //分辨率
                m_nWidth = m_resizeDim.w;
                m_nLumaHeight = m_resizeDim.h;
            }
            //裁剪，显示尺寸的大小发生改变，同时分辨率也会变化
            if (m_cropRect.r && m_cropRect.b) 
            {
                //显示尺寸
                videoDecodeCreateInfo.display_area.left = m_cropRect.l;
                videoDecodeCreateInfo.display_area.top = m_cropRect.t;
                videoDecodeCreateInfo.display_area.right = m_cropRect.r;
                videoDecodeCreateInfo.display_area.bottom = m_cropRect.b;

                //分辨率
                m_nWidth = m_cropRect.r - m_cropRect.l;                
                m_nLumaHeight = m_cropRect.b - m_cropRect.t;
            }

            videoDecodeCreateInfo.ulTargetWidth = m_nWidth;
            videoDecodeCreateInfo.ulTargetHeight = m_nLumaHeight;
        }

        m_nChromaHeight = (int)(m_nLumaHeight * GetChromaHeightFactor(videoDecodeCreateInfo.ChromaFormat));
        m_nNumChromaPlanes = GetChromaPlaneCount(videoDecodeCreateInfo.ChromaFormat);
        m_nSurfaceHeight = videoDecodeCreateInfo.ulTargetHeight;//无符号数变成符号数
        m_nSurfaceWidth = videoDecodeCreateInfo.ulTargetWidth;
        m_displayRect.b = videoDecodeCreateInfo.display_area.bottom;
        m_displayRect.t = videoDecodeCreateInfo.display_area.top;
        m_displayRect.l = videoDecodeCreateInfo.display_area.left;
        m_displayRect.r = videoDecodeCreateInfo.display_area.right;

        m_videoInfo << "\nVideo-" << m_video_id << " Decoding Params:" << std::endl
            << "\tNum Surfaces : " << videoDecodeCreateInfo.ulNumDecodeSurfaces << std::endl
            << "\tCrop         : [" << videoDecodeCreateInfo.display_area.left << ", " << videoDecodeCreateInfo.display_area.top << ", "
            << videoDecodeCreateInfo.display_area.right << ", " << videoDecodeCreateInfo.display_area.bottom << "]" << std::endl
            << "\tResize       : " << videoDecodeCreateInfo.ulTargetWidth << "x" << videoDecodeCreateInfo.ulTargetHeight << std::endl
            << "\tDeinterlace  : " << std::vector<const char *>{"Weave", "Bob", "Adaptive"}[videoDecodeCreateInfo.DeinterlaceMode];
        m_videoInfo << std::endl;
        

        LOG(INFO) << m_videoInfo.str();

        check(cuCtxPushCurrent(m_cuContext), "cuCtxPushCurrent");
        check(cuvidCreateDecoder(&m_hDecoder, &videoDecodeCreateInfo), "cuvidCreateDecoder");
        if(m_hDecoder == NULL)
        {
            LOG(ERROR) << "cuvidCreateDecoder failed";
            exit(0);
        }
        check(cuCtxPopCurrent(NULL), "cuCtxPopCurrent");

        STOP_TIMER("HandleVideoSequence::Create decode Time: ");

        start_time = NvCodec::common::getInstance()->Start_time();
        
        return nDecodeSurface;
    }

    int DecodeItem::HandlePictureDecode(CUVIDPICPARAMS *pPicParams)
    {
        if (!m_hDecoder)
        {
            LOG(ERROR) << "Decoder not initialized : " << CUDA_ERROR_NOT_INITIALIZED;
            exit(0);
            return false;
        }
        //LOG(INFO)<< "HandlePictureDecode::CurrPicIdx: " << pPicParams->CurrPicIdx << " ready to decode";
        m_nPicNumInDecodeOrder[pPicParams->CurrPicIdx] = m_nDecodePicCnt++;
        check(cuvidDecodePicture(m_hDecoder, pPicParams), "cuvidDecodePicture");
        return 1;
    }

    int DecodeItem::HandlePictureDisplay(CUVIDPARSERDISPINFO *pDispInfo)
    {
        //LOG(INFO)<< "HandlePictureDisplay::picture_index: " << pDispInfo->picture_index << " finish decode" << " and timestamp: " << pDispInfo->timestamp;
        CUVIDPROCPARAMS videoProcessingParameters = {};
        videoProcessingParameters.progressive_frame = pDispInfo->progressive_frame;
        videoProcessingParameters.second_field = pDispInfo->repeat_first_field + 1;
        videoProcessingParameters.top_field_first = pDispInfo->top_field_first;
        videoProcessingParameters.unpaired_field = pDispInfo->repeat_first_field < 0;
        videoProcessingParameters.output_stream = m_cuvidStream;

        CUdeviceptr dpSrcFrame = 0;
        unsigned int nSrcPitch = 0;
        check(cuvidMapVideoFrame(m_hDecoder, pDispInfo->picture_index, &dpSrcFrame,&nSrcPitch, &videoProcessingParameters), "cuvidMapVideoFrame");

        //获取解码状态
        CUVIDGETDECODESTATUS DecodeStatus;
        memset(&DecodeStatus, 0, sizeof(DecodeStatus));
        CUresult result = cuvidGetDecodeStatus(m_hDecoder, pDispInfo->picture_index, &DecodeStatus);
        if (result == CUDA_SUCCESS && (DecodeStatus.decodeStatus == cuvidDecodeStatus_Error || DecodeStatus.decodeStatus == cuvidDecodeStatus_Error_Concealed))
            LOG(WARNING) << "Decode Error occurred for picture: " << m_nPicNumInDecodeOrder[pDispInfo->picture_index];
        

        uint8_t *pDecodedFrame = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mtxVPFrame);

            // 当用于保存已完成解码的帧的数据的buffer不够存下新的数据时申请新的内存
            if ((unsigned)++m_nDecodedFrame > m_vpFrame.size())
            {
                // Not enough frames in stock
                m_nFrameAlloc++;
                uint8_t *pFrame = NULL;
                if (m_bUseDeviceFrame)
                {
                    check(cuCtxPushCurrent(m_cuContext), "cuCtxPushCurrent");
                    if (m_bDeviceFramePitched)
                        check(cuMemAllocPitch((CUdeviceptr *)&pFrame, &m_nDeviceFramePitch, m_nWidth * m_nBPP, m_nLumaHeight + (m_nChromaHeight * m_nNumChromaPlanes), 16), "cuMemAllocPitch");
                    else
                        check(cuMemAlloc((CUdeviceptr *)&pFrame, GetFrameSize()), "cuMemAlloc");
                    
                    check(cuCtxPopCurrent(NULL), "cuCtxPopCurrent");
                }
                else
                    pFrame = new uint8_t[GetFrameSize()];
        
                m_vpFrame.push_back(pFrame);
            }

            pDecodedFrame = m_vpFrame[m_nDecodedFrame - 1];
        }

        check(cuCtxPushCurrent(m_cuContext), "cuCtxPushCurrent");
        CUDA_MEMCPY2D m = { 0 };
        m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        m.srcDevice = dpSrcFrame;
        m.srcPitch = nSrcPitch;
        m.dstMemoryType = m_bUseDeviceFrame ? CU_MEMORYTYPE_DEVICE : CU_MEMORYTYPE_HOST;
        m.dstDevice = (CUdeviceptr)(m.dstHost = pDecodedFrame);
        m.dstPitch = m_nDeviceFramePitch ? m_nDeviceFramePitch : m_nWidth * m_nBPP;
        m.WidthInBytes = m_nWidth * m_nBPP;
        m.Height = m_nLumaHeight;
        check(cuMemcpy2DAsync(&m, m_cuvidStream), "cuMemcpy2DAsync");

        m.srcDevice = (CUdeviceptr)((uint8_t *)dpSrcFrame + m.srcPitch * m_nSurfaceHeight);
        m.dstDevice = (CUdeviceptr)(m.dstHost = pDecodedFrame + m.dstPitch * m_nLumaHeight);
        m.Height = m_nChromaHeight;
        check(cuMemcpy2DAsync(&m, m_cuvidStream), "cuMemcpy2DAsync");

        if (m_nNumChromaPlanes == 2)
        {
            m.srcDevice = (CUdeviceptr)((uint8_t *)dpSrcFrame + m.srcPitch * m_nSurfaceHeight * 2);
            m.dstDevice = (CUdeviceptr)(m.dstHost = pDecodedFrame + m.dstPitch * m_nLumaHeight * 2);
            m.Height = m_nChromaHeight;
            check(cuMemcpy2DAsync(&m, m_cuvidStream), "cuMemcpy2DAsync");
        }
        check(cuStreamSynchronize(m_cuvidStream), "cuStreamSynchronize");
        check(cuCtxPopCurrent(NULL), "cuCtxPopCurrent");

        if ((int)m_vTimestamp.size() < m_nDecodedFrame) 
        {
            m_vTimestamp.resize(m_vpFrame.size());
        }
        m_vTimestamp[m_nDecodedFrame - 1] = pDispInfo->timestamp;

        check(cuvidUnmapVideoFrame(m_hDecoder, dpSrcFrame), "cuvidUnmapVideoFrame");
        return 1;
    }

    void DecodeItem::ConvertSemiplanarToPlanar(uint8_t *pHostFrame, int nWidth, int nHeight, int nBitDepth)
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
}