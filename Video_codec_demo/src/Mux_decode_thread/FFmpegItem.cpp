#include <thread>
#include "FFmpegItem.h"

namespace FFmpeg
{
    FFmpegItem::FFmpegItem(const char *szFilePath) : m_pDataWithHeader(NULL), m_frameCount(0), m_Pframe_interval_pts(0), m_timeBase(0)
    {
        m_fmtc = NULL;  
        m_bsfc = NULL;
        m_pFrame = NULL;
        m_pCodecCtx = NULL; 
        m_pCodec = NULL;  	
        m_frameCount = 0;
        m_H264_frameCount = 0;
        m_Pframe_interval_pts = -1;
        m_timeBase = 0;
        m_state = state::Closed;
        first_finish = true;
        strcpy(m_filename, szFilePath);
    }

    FFmpegItem::~FFmpegItem()
    {

        if (!m_fmtc) 
            return;
        
        if (m_pkt.data) 
            av_packet_unref(&m_pkt);
        
        if (m_pktFiltered.data) 
            av_packet_unref(&m_pktFiltered);
        
        if (m_bsfc) 
            av_bsf_free(&m_bsfc);

        if (m_pDataWithHeader)
            av_free(m_pDataWithHeader);
        
        //segment
        avformat_close_input(&m_fmtc);
    }

    bool FFmpegItem::Open()
    {
        //LOG(INFO) << "FFmpegItem::Open::is open: " << is_open() << "  id: " << Get_id();
        //1.进行网络组件的全局初始化
        avformat_network_init();
        AVDictionary* options = NULL;
        //配置参数(如果视频流出现是文件通过live555等推送的，当一个文件结束时，则需要重新配置，打开视频流，不然会一直读帧失败)
        av_dict_set(&options, "rtsp_transport", "tcp", 0);  //默认UDP，容易出现丢包的问题
    
		//av_dict_set(&options, "stimeout", "3000000", 0);    //设置超时断开连接时间
		av_dict_set(&options, "bufsize", "‭2097151", 0);
        av_dict_set(&options, "max_delay", "500000", 0);    //设置最大时延
        av_dict_set(&options, "framerate", "10", 0);

         //2.打开输入视频文件
        if (avformat_open_input(&m_fmtc, m_filename, NULL, &options) < 0)
        {
            LOG(ERROR) << "\"" << m_filename << "\" : Open failed";
            Set_state(state::Error);
            return false;
        }
        
        LOG(INFO) << "\"" << m_filename << "\" : Open successed\n"<< "Media format: " << m_fmtc->iformat->long_name << " (" << m_fmtc->iformat->name << ")";

        //3.读取一部分视音频数据并且获得一些相关的信息
        if (avformat_find_stream_info(m_fmtc, NULL) < 0)
        {
            LOG(ERROR) << "\"" << m_filename << "\" : Read stream info failed";
            Set_state(state::Error);
            return false;
        }

        //4.每个视频文件中有多个流（视频流、音频流、字幕流等，而且可有多个），这里获取其中的视频流
        m_iVideoStream = av_find_best_stream(m_fmtc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (m_iVideoStream < 0) 
        {
            LOG(ERROR) << "\"" << m_filename << "\" : Could not find stream in input file";
            Set_state(state::Error);
            return false;
        }

        m_eVideoCodec = m_fmtc->streams[m_iVideoStream]->codecpar->codec_id;                //获得编码类型id
        m_nWidth = m_fmtc->streams[m_iVideoStream]->codecpar->width;                        //视频分辨率宽
        m_nHeight = m_fmtc->streams[m_iVideoStream]->codecpar->height;                      //视频分辨率高
        m_eChromaFormat = (AVPixelFormat)m_fmtc->streams[m_iVideoStream]->codecpar->format; //视频色彩空间
        AVRational rTimeBase = m_fmtc->streams[m_iVideoStream]->time_base;                  //视频帧时间戳
        m_timeBase = av_q2d(rTimeBase);                                                     //视频帧率

        // 5.根据输入视频的色彩空间类型设置相关参数
        switch (m_eChromaFormat)
        {
            case AV_PIX_FMT_YUV420P10LE:
                m_nBitDepth = 10;
                m_nChromaHeight = (m_nHeight + 1) >> 1;
                m_nBPP = 2;
                break;
            case AV_PIX_FMT_YUV420P12LE:
                m_nBitDepth = 12;
                m_nChromaHeight = (m_nHeight + 1) >> 1;
                m_nBPP = 2;
                break;
            case AV_PIX_FMT_YUV444P10LE:
                m_nBitDepth = 10;
                m_nChromaHeight = m_nHeight << 1;
                m_nBPP = 2;
                break;
            case AV_PIX_FMT_YUV444P12LE:
                m_nBitDepth = 12;
                m_nChromaHeight = m_nHeight << 1;
                m_nBPP = 2;
                break;
            case AV_PIX_FMT_YUV444P:
                m_nBitDepth = 8;
                m_nChromaHeight = m_nHeight << 1;
                m_nBPP = 1;
                break;
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUVJ420P:
            case AV_PIX_FMT_YUVJ422P:   // jpeg decoder output is subsampled to NV12 for 422/444 so treat it as 420
            case AV_PIX_FMT_YUVJ444P:   // jpeg decoder output is subsampled to NV12 for 422/444 so treat it as 420
                m_nBitDepth = 8;
                m_nChromaHeight = (m_nHeight + 1) >> 1;
                m_nBPP = 1;
                break;
            default:
                LOG(WARNING) << "ChromaFormat not recognized. Assuming 420";
                m_nBitDepth = 8;
                m_nChromaHeight = (m_nHeight + 1) >> 1;
                m_nBPP = 1;
        }

        m_bMp4H264 = m_eVideoCodec == AV_CODEC_ID_H264 && (
                !strcmp(m_fmtc->iformat->long_name, "QuickTime / MOV") 
                || !strcmp(m_fmtc->iformat->long_name, "FLV (Flash Video)") 
                || !strcmp(m_fmtc->iformat->long_name, "Matroska / WebM")
            );
        m_bMp4HEVC = m_eVideoCodec == AV_CODEC_ID_HEVC && (
                !strcmp(m_fmtc->iformat->long_name, "QuickTime / MOV")
                || !strcmp(m_fmtc->iformat->long_name, "FLV (Flash Video)")
                || !strcmp(m_fmtc->iformat->long_name, "Matroska / WebM")
            );

        m_bMp4MPEG4 = m_eVideoCodec == AV_CODEC_ID_MPEG4 && (
                !strcmp(m_fmtc->iformat->long_name, "QuickTime / MOV")
                || !strcmp(m_fmtc->iformat->long_name, "FLV (Flash Video)")
                || !strcmp(m_fmtc->iformat->long_name, "Matroska / WebM")
            );

        //Initialize packet fields with default values
        av_init_packet(&m_pkt);
        m_pkt.data = NULL;
        m_pkt.size = 0;
        av_init_packet(&m_pktFiltered);
        m_pktFiltered.data = NULL;
        m_pktFiltered.size = 0;

        // Initialize bitstream filter and its required resources
        if (m_bMp4H264) 
        {
            const AVBitStreamFilter *bsf = av_bsf_get_by_name("h264_mp4toannexb");
            if (!bsf) 
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_get_by_name() failed";
                Set_state(state::Error);
                return false;
            }
            if (av_bsf_alloc(bsf, &m_bsfc) < 0)
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_alloc() failed";
                Set_state(state::Error);
                return false;
            }
            avcodec_parameters_copy(m_bsfc->par_in, m_fmtc->streams[m_iVideoStream]->codecpar);
            if (av_bsf_init(m_bsfc) < 0)
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_init() failed";
                Set_state(state::Error);
                return false;
            }
        }
        if (m_bMp4HEVC) 
        {
            const AVBitStreamFilter *bsf = av_bsf_get_by_name("hevc_mp4toannexb");
            if (!bsf) 
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_get_by_name() failed";
                Set_state(state::Error);
                return false;
            }
            if (av_bsf_alloc(bsf, &m_bsfc) < 0)
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_alloc() failed";
                Set_state(state::Error);
                return false;
            }
            avcodec_parameters_copy(m_bsfc->par_in, m_fmtc->streams[m_iVideoStream]->codecpar);
            if(av_bsf_init(m_bsfc) < 0)
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_init() failed";
                Set_state(state::Error);
                return false;
            }
        }
#if DEBUG
        LOG(INFO) << "m_eVideoCodec: " << m_eVideoCodec <<" m_bMp4H264: " << m_bMp4H264 << " m_bMp4HEVC: " << m_bMp4HEVC;
#endif
        Set_state(state::Opened);
        //LOG(INFO) << "FFmpegItem::Open::is open: " << is_open() << "  id: " << Get_id();
        return true;
    }

    bool FFmpegItem::Close()
    {

    }

    //探测P帧间隔
    int FFmpegItem::ffprobe()
    {
        LOG(INFO) << "*******Probe interval pts of P frame**********"; 
        m_pCodecCtx = avcodec_alloc_context3(NULL);
        if (avcodec_parameters_to_context(m_pCodecCtx, m_fmtc->streams[m_iVideoStream]->codecpar) < 0)
        {
            LOG(ERROR) << "avcodec_parameters_to_context error";
            Set_state(state::Error);
            return -1;
        }

        m_pFrame = av_frame_alloc();

        //查找FFmpeg的解码器
        m_pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id);
        if(m_pCodec == NULL)
        {
            LOG(ERROR) << "could not find codec id : " << m_pCodecCtx->codec_id;
            Set_state(state::Error);
            return -1;
        }
            
        //初始化一个视音频编解码器的AVCodecContext
        if (avcodec_open2(m_pCodecCtx, m_pCodec, NULL) < 0)
        {
            LOG(ERROR) << "could not open codec id : " << m_pCodecCtx->codec_id;
            Set_state(state::Error);
            return -1;
        }
            
        //获取硬件支持的并发线程数
        m_pCodecCtx->thread_count = std::thread::hardware_concurrency() > 8 ? 4 : (std::thread::hardware_concurrency() < 2 ? 1 : std::thread::hardware_concurrency() / 2);
        m_pCodecCtx->thread_safe_callbacks = 1;

        int read_frame_ret, receive_frame_ret;
        int ffprobe_frame_count = 0;
        int receive_frame_count = 0;
        bool start_P_sta = false, Exit_probe = false;;
        uint64_t start_Pframe_pts = 0, end_Pframe_pts = 0;

        while(1)
        {
            if (m_pkt.data) 
                av_packet_unref(&m_pkt);
            //读取一帧数据
            read_frame_ret = av_read_frame(m_fmtc, &m_pkt);
            if(read_frame_ret >= 0 && m_pkt.stream_index != m_iVideoStream)
            {
                av_packet_unref(&m_pkt);
                continue;
            }

            ffprobe_frame_count++;

            //发送1帧编码数据给解码器
            if(avcodec_send_packet(m_pCodecCtx, &m_pkt) < 0)
            {
                LOG(WARNING) << "\"" << m_filename << "\" : avcodec_send_packet error";
                continue;
            }
            
            //获取解码器的解码结果，因为解码速度原因，不一定发送了数据给解码器就能接着读到解码器的结果，因此每次发送编码数据给解码器，要读多次，应对可能解码器已经有多帧数据解码完成，且可读
            while(1)
            {
                av_frame_unref(m_pFrame);
                receive_frame_ret = avcodec_receive_frame(m_pCodecCtx, m_pFrame);
                if(receive_frame_ret == AVERROR(EAGAIN) || receive_frame_ret == AVERROR_EOF || Exit_probe)
                    break;

                receive_frame_count++;
                switch(m_pFrame->pict_type)
                {
                    case AV_PICTURE_TYPE_I:
                        LOG(INFO) << "\"" << m_filename << "\" : I_frame: " << receive_frame_count << " and pts:" << m_pFrame->pts;
                        break;
                    case AV_PICTURE_TYPE_P:
                        if(!start_P_sta)
                        {
                            start_P_sta = true;
                            start_Pframe_pts = m_pFrame->pts;
                        }
                        else
                        {
                            end_Pframe_pts = m_pFrame->pts;
                            m_Pframe_interval_pts = end_Pframe_pts - start_Pframe_pts;
                            Exit_probe = true;
                        }
                        LOG(INFO) << "\"" << m_filename << "\" : P_frame: " << receive_frame_count << " and pts:" << m_pFrame->pts;
                        break;
                    case AV_PICTURE_TYPE_B:
                        LOG(INFO) << "\"" << m_filename << "\" : B_frame: " << receive_frame_count << " and pts:" << m_pFrame->pts;
                        break;
                    case AV_PICTURE_TYPE_S:
                        //LOG(INFO) << "\"" << m_filename << "\" : S_frame: " << receive_frame_count << " and pts:" << m_pFrame->pts;
                        break;
                    case AV_PICTURE_TYPE_SI:
                        //LOG(INFO) << "\"" << m_filename << "\" : SI_frame: " << receive_frame_count << " and pts:" << m_pFrame->pts;
                        break;
                    case AV_PICTURE_TYPE_SP:
                        //LOG(INFO) << "\"" << m_filename << "\" : SP_frame: " << receive_frame_count << " and pts:" << m_pFrame->pts;
                        break;
                    case AV_PICTURE_TYPE_BI:
                        //LOG(INFO) << "\"" << m_filename << "\" : BI_frame: " << receive_frame_count << " and pts:" << m_pFrame->pts;
                        break;
                    default:
                        LOG(INFO) << "\"" << m_filename << "\" : Unkonwn frame typpe: " << receive_frame_count << " and pts:" << m_pFrame->pts;
                }
            }

            if(read_frame_ret < 0)
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_read_frame error or finished " << " and read frames: " << ffprobe_frame_count << " and receive frame: " << receive_frame_count;
                Set_state(state::Closed_error);
                return -1;
            }

            if(Exit_probe)
            {
                if (m_pFrame) 
                    av_frame_free(&m_pFrame);
                if (m_pCodecCtx) 
                    avcodec_free_context(&m_pCodecCtx);

                //重新打开视频源
                avformat_close_input(&m_fmtc);
                AVDictionary* options = NULL;
                av_dict_set(&options, "rtsp_transport", "tcp", 0);  //默认UDP，容易出现丢包的问题
                //av_dict_set(&options, "stimeout", "3000000", 0);    //设置超时断开连接时间
                av_dict_set(&options, "bufsize", "‭2097151", 0);
                av_dict_set(&options, "max_delay", "500000", 0);    //设置最大时延
                av_dict_set(&options, "framerate", "10", 0);
                if (avformat_open_input(&m_fmtc, m_filename, NULL, &options) < 0)
                {
                    LOG(ERROR) << "\"" << m_filename << "\" : ReOpen failed";
                    Set_state(state::Error);
                    return -1;
                }

                LOG(INFO) << "\"" << m_filename << "\" : Pframe_interval_pts: " << m_Pframe_interval_pts;
                LOG(INFO) << "\"" << m_filename << "\" : ReOpen Successed";
                LOG(INFO) << "********************Probe End*****************";
                break;
            }
        }

        return 0;
    }

    bool FFmpegItem::Demux_H264_IPFrame(uint8_t **ppVideo, int *pnVideoBytes, int64_t *pts)
    {
        if (!m_fmtc) 
            return false;

        *pnVideoBytes = 0;

        int ret = 0;

        if(m_Pframe_interval_pts < 0)
        {
            LOG(WARNING) << "Not ffprobe P frame internal";
            return false;
        }

        while(1)
        {
            if (m_pkt.data) 
                av_packet_unref(&m_pkt);

            ret = av_read_frame(m_fmtc, &m_pkt);
            if(ret >= 0 && m_pkt.stream_index != m_iVideoStream)
            {
                av_packet_unref(&m_pkt);
                continue;
            }
            else if(ret < 0)
            {
                //LOG(WARNING) << "\"" << m_filename << "\":demux frame error or finished " << " \nand total demux frames: " << m_frameCount;
                Set_state(state::Error);
                return false;
            }

            m_frameCount++;

            if(m_eVideoCodec == AV_CODEC_ID_H264)
            {
                //判断是否是关键帧,可以连续解P帧，不能跳着解P帧，B帧依赖性大，容易出问题
                if(m_pkt.flags == AV_PKT_FLAG_KEY)
                {
                    //需要进行数据过滤
                    if(m_bMp4H264)
                    {
                        if (m_pktFiltered.data) 
                            av_packet_unref(&m_pktFiltered);
                        
                        if (av_bsf_send_packet(m_bsfc, &m_pkt) < 0)
                        {
                            LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_send_packet error";
                            Set_state(state::Error);
                            return false;
                        }
                        if (av_bsf_receive_packet(m_bsfc, &m_pktFiltered) < 0)
                        {
                            LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_receive_packet error";
                            Set_state(state::Error);
                            return false;
                        }
                        *ppVideo = m_pktFiltered.data;
                        *pnVideoBytes = m_pktFiltered.size;
                        if (pts)
                            *pts = (int64_t) (m_pktFiltered.pts * 1000 * m_timeBase);
                    }
                    else
                    {
                        *ppVideo = m_pkt.data;
                        *pnVideoBytes = m_pkt.size;
                        if (pts)
                            *pts = (int64_t) (m_pkt.pts * 1000 * m_timeBase);
                    }

                    m_Kframe_pts = m_pkt.pts;
                    m_Pframe_pts = m_Kframe_pts + m_Pframe_interval_pts;

                    m_H264_frameCount++;
#if DEBUG
                    //LOG(INFO) << "I_frameCount: " << m_frameCount << " and key_frame_size:" << *pnVideoBytes << " and pts:" << m_pkt.pts << " and dts: " << m_pkt.dts;
#endif
                    break;
                } 
                else
                {
                    if(m_pkt.pts == m_Pframe_pts)
                    {
                        *ppVideo = m_pkt.data;
                        *pnVideoBytes = m_pkt.size;
                        if (pts)
                            *pts = (int64_t) (m_pkt.pts * 1000 * m_timeBase);
#if DEBUG                           
                        //LOG(INFO) << "P_frameCout: " <<  m_frameCount <<" and pts:" << m_pkt.pts << " and dts: " << m_pkt.dts;
#endif
                        m_Pframe_pts += m_Pframe_interval_pts;
                        m_H264_frameCount++;
                        break;
                    }
                }
            }
            else
                LOG(WARNING) << "VideoCodec : " << m_eVideoCodec <<"is not supported in Demux_H264_IPFrame";
        }

        return true;
    }

    bool FFmpegItem::Demux(uint8_t **ppVideo, int *pnVideoBytes, int64_t *pts) 
    {
        bool key_frame = false;
        if (!m_fmtc) 
            return false;
        *pnVideoBytes = 0;

        if (m_pkt.data) 
            av_packet_unref(&m_pkt);

        int e = 0;
        while ((e = av_read_frame(m_fmtc, &m_pkt)) >= 0 && m_pkt.stream_index != m_iVideoStream) 
            av_packet_unref(&m_pkt);
        
        if (e < 0) 
        {
            LOG(ERROR) << "\"" << m_filename << "\" : av_read_frame error or finished and demux frame_count: " << m_frameCount;
            Set_state(state::Closed_error);
            return false;
        }
        
        //需要进行数据过滤
        if (m_bMp4H264 || m_bMp4HEVC) 
        {
            if (m_pktFiltered.data) 
                av_packet_unref(&m_pktFiltered);
            if (av_bsf_send_packet(m_bsfc, &m_pkt) < 0)
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_send_packet error";
                Set_state(state::Error);
                return false;
            }
            if (av_bsf_receive_packet(m_bsfc, &m_pktFiltered) < 0)
            {
                LOG(ERROR) << "\"" << m_filename << "\" : av_bsf_receive_packet error";
                Set_state(state::Error);
                return false;
            }

            *ppVideo = m_pktFiltered.data;
            *pnVideoBytes = m_pktFiltered.size;
            if (pts)
                *pts = (int64_t) (m_pktFiltered.pts * 1000 * m_timeBase);
        } 
        else 
        {
            //有额外的数据
            if (m_bMp4MPEG4 && (m_frameCount == 0)) 
            {
                int extraDataSize = m_fmtc->streams[m_iVideoStream]->codecpar->extradata_size;

                if (extraDataSize > 0) 
                {
                    // extradata contains start codes 00 00 01. Subtract its size
                    m_pDataWithHeader = (uint8_t *)av_malloc(extraDataSize + m_pkt.size - 3*sizeof(uint8_t));

                    if (!m_pDataWithHeader) 
                    {
                        LOG(ERROR) << "\"" << m_filename << "\" : av_malloc failed";
                        Set_state(state::Error);
                        return false;
                    }

                    memcpy(m_pDataWithHeader, m_fmtc->streams[m_iVideoStream]->codecpar->extradata, extraDataSize);
                    memcpy(m_pDataWithHeader+extraDataSize, m_pkt.data+3, m_pkt.size - 3*sizeof(uint8_t));

                    *ppVideo = m_pDataWithHeader;
                    *pnVideoBytes = extraDataSize + m_pkt.size - 3*sizeof(uint8_t);
                }

            } 
            else 
            {
                *ppVideo = m_pkt.data;
                *pnVideoBytes = m_pkt.size;
            }

            if (pts)
                *pts = (int64_t)(m_pkt.pts * 1000 * m_timeBase);
        }

        m_frameCount++;

        return true;
    }

    
}