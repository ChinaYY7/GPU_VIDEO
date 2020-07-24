#include "DecodeManager.h"
#include "Demux_queue.h"
#include <iomanip>
namespace GpuDecode
{
    DecodeManager::DecodeManager(): m_iGpu(0), m_size(0), m_isEnd(true)
    {}

    DecodeManager::DecodeManager(int iGpu): m_size(0), m_isEnd(true)
    {
        m_iGpu = iGpu;
    }

    DecodeManager::~DecodeManager()
    {
        std::map<uint64_t, DecodeItem*>::iterator it = m_DecodeMap.begin();
        while (it != m_DecodeMap.end())
        {
            LOG(INFO) << "Remove DecodeItem \"" << it->first<< "\" finish" << " and decode total frame: " << it->second->Get_total_decode_frame();
            delete it->second;
            m_DecodeMap.erase(it);	
            it++;
        }
    }

    uint16_t DecodeManager::add(cudaVideoCodec eCodec, uint64_t id)
    {
        DecodeItem *pDecodeItem; 
        pDecodeItem = new DecodeItem(m_iGpu);
        pDecodeItem->Set_ecodec(eCodec);
        pDecodeItem->Set_video_id(id);
        m_DecodeMap.insert(std::make_pair(id, pDecodeItem));
        m_size++;

        return m_size;
    }

    uint16_t DecodeManager::remove(uint64_t id)
    {
        std::map<uint64_t, DecodeItem *>::iterator it = m_DecodeMap.find(id);
        if (it != m_DecodeMap.end())
        {
            LOG(INFO) << "Remove DecodeItem finish: " << id;
            delete it->second;
            m_DecodeMap.erase(it);
            m_size--;	
        }
        else
            LOG(WARNING) << "DecodeItem id not exist: " << id;

        return m_size;
    }

    bool DecodeManager::init()
    {
        std::map<uint64_t, DecodeItem*>::iterator it = m_DecodeMap.begin();
        while (it != m_DecodeMap.end())
        {
            it->second->Init();
            it++;
        }
    }

    std::vector<Decodeframe> DecodeManager::Decode_all(std::vector<FFmpeg::Demuxframe> Demuxframes)
    {
        int nFrameReturned = 0;
        uint8_t **ppFrame;
        int64_t *ppTimestamp;
        bool bDecodeOutSemiPlanar;
        Decodeframe Decodeframe_temp;
        DecodeItem* pDecodeItem;
        m_Decodeframes.clear();

        for(int i = 0; i < Demuxframes.size(); i++)
        {
            std::map<uint64_t, DecodeItem *>::iterator it = m_DecodeMap.find(Demuxframes[i].id);
            if(it == m_DecodeMap.end())
                continue;
            //对应视频流结束
            if(Demuxframes[i].nVideoBytes == 0)
            {
                LOG(INFO) << "Decode " << it->second->Get_video_id() << " spend time: " <<it->second->Get_duration_time() << " ms, and decode speed: " << (int)it->second->Get_decode_speed() << " fps";
                continue;
            }
            if(it != m_DecodeMap.end())
            {
                pDecodeItem = it->second;
                pDecodeItem->Decode(Demuxframes[i].pVideo, Demuxframes[i].nVideoBytes, &ppFrame, &nFrameReturned, &ppTimestamp, Demuxframes[i].id);
                if (!pDecodeItem->Get_total_decode_frame() && nFrameReturned)
                {
                    bDecodeOutSemiPlanar = (pDecodeItem->GetOutputFormat() == cudaVideoSurfaceFormat_NV12) || (pDecodeItem->GetOutputFormat() == cudaVideoSurfaceFormat_P016);
                    pDecodeItem->Set_bDecodeOutSemiPlanar(bDecodeOutSemiPlanar);
                }
                
                pDecodeItem->increase_total_decode_frame(nFrameReturned);

                for (int i = 0; i < nFrameReturned; i++) 
                {
                    if (pDecodeItem->Get_bDecodeOutSemiPlanar()) 
                        pDecodeItem->ConvertSemiplanarToPlanar(ppFrame[i], pDecodeItem->GetWidth(),pDecodeItem->GetHeight(), pDecodeItem->GetBitDepth());
                    else
                        LOG(WARNING) << "Not Support to ConvertSemiplanarToPlanar";

                    Decodeframe_temp.id = ppTimestamp[i];
                    Decodeframe_temp.pVideo = ppFrame[i];
                    Decodeframe_temp.nVideoBytes = pDecodeItem->GetFrameSize();
                    m_Decodeframes.push_back(Decodeframe_temp);
                }
            }
        }

        return m_Decodeframes;
    }

    pthread_t DecodeManager::start()
    {
        pthread_t tid;
		pthread_create(&tid, NULL, DecodeManager::run, this);
		return tid;
    }

    void *DecodeManager::run(void *arg)
    {
        DecodeManager *this_ = reinterpret_cast<DecodeManager*>(arg);
        std::vector<Decodeframe> Decodeframes;
        FFmpeg::Demuxframe Demuxframe_tmp;
        std::vector<FFmpeg::Demuxframe> Demuxframes;

        this_->m_isEnd = false;
        
        while (1)
        {
            //int read_position = Demux::Demux_queue::getInstance()->dequeue(Demuxframe_tmp);
            int read_position = Demux::Demux_queue::getInstance()->dequeue_batch(Demuxframes);
    
            for(int i = 0; i < Demuxframes.size(); i++)
            {
                if(Demuxframes[i].nVideoBytes == -1)
                {
                    LOG(WARNING) << "Decode read last demux";
                    this_->m_isEnd = true;
                    Demuxframes[i].nVideoBytes = 0;
                    break;
                }
            }

            Decodeframes = this_->Decode_all(Demuxframes);
            //LOG(INFO) << "Demuxframes size:" << Demuxframes.size();
            if(Decodeframes.size() > 0)
            {
                for (std::vector<Decodeframe>::iterator it = Decodeframes.begin() ; it != Decodeframes.end(); ++it)
                {
                    
                }
            }

            //处理完队列中的数据再退出
			if (this_->m_isEnd) 	
				break;
        }
    }

    void DecodeManager::finish()
	{
		m_isEnd = true;
	}
}