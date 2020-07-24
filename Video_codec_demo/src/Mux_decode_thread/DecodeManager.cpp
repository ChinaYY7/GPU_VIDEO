#include "DecodeManager.h"
#include "Demux_queue.h"
#include "JpegEncoder.h"
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
        bool sta;
        std::map<uint64_t, DecodeItem*>::iterator it = m_DecodeMap.begin();
        while (it != m_DecodeMap.end())
        {
            sta = it->second->Init();
            it++;
        }
        return sta;
    }

    std::vector<Decodeframe> DecodeManager::Decode_all(const std::vector<FFmpeg::Demuxframe> & Demuxframes)
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
            std::map<uint64_t, DecodeItem *>::iterator it = m_DecodeMap.find(Demuxframes[i].id());
            if(it == m_DecodeMap.end())
                continue;

            pDecodeItem = it->second;
            
            //对应视频流结束
            if(Demuxframes[i].size() == -1)
            {
                pDecodeItem->Set_decode_end();
                LOG(INFO) << "Decode " << it->second->Get_video_id() << " spend time: " <<it->second->Get_duration_time() << " ms, and decode speed: " << (int)it->second->Get_decode_speed() << " fps";
                continue;
            }

            pDecodeItem->Decode(Demuxframes[i].data().data(), Demuxframes[i].size(), &ppFrame, &nFrameReturned, &ppTimestamp, Demuxframes[i].id());
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
                //需要抽离出来
                if(NvCodec::JpegEncoder::getInstance()->enqueue(m_Decodeframes[i].id, m_Decodeframes[i].pVideo, m_Decodeframes[i].nVideoBytes) < 0)
                {
                    LOG(ERROR) << "Can't finish Jpeg Encode";
                    break;
                }
            }
            
        }

        return m_Decodeframes;
    }

    bool DecodeManager::check_finish()
    {
        std::map<uint64_t, DecodeItem*>::iterator it = m_DecodeMap.begin();
        while (it != m_DecodeMap.end())
        {
            if(!it->second->is_decode_end())
                break;
            it++;
        }

        if(it == m_DecodeMap.end())
            return true;
        else
            return false;
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
            if(this_->check_finish() || this_->m_isEnd)
            {
                this_->m_isEnd = true;
                break;
            }

            int read_position = Demux::Demux_queue::getInstance()->dequeue_batch(Demuxframes);
            Decodeframes = this_->Decode_all(Demuxframes);
            //LOG(INFO) << "Demuxframes size:" << Demuxframes.size();
            /*
            for(int i = 0; i < Decodeframes.size(); i++)
            {
                if(NvCodec::JpegEncoder::getInstance()->enqueue(Decodeframes[i].id, Decodeframes[i].pVideo, Decodeframes[i].nVideoBytes) < 0)
                {
                    LOG(ERROR) << "Can't finish Jpeg Encode";
                    break;
                }
            }
            */
        
			if (this_->m_isEnd) 	
				break;
        }
    }

    void DecodeManager::finish()
	{
		m_isEnd = true;
	}
}