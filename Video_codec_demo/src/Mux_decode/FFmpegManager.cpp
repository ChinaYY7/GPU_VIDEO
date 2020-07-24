#include "FFmpegManager.h"
#include "Demux_queue.h"

namespace FFmpeg
{
    FFmpegManager::FFmpegManager():m_size(0), m_isEnd(true)
    {}

    FFmpegManager::~FFmpegManager()
    {
        std::map<uint64_t, FFmpegItem*>::iterator it = m_FFmpegMap.begin();
        while (it != m_FFmpegMap.end())
        {
            LOG(INFO) << "Remove FFmpegItem \""<< it->first << "\" finish" << " and demux I,P frames:" << it->second->Get_H264_frameCount();
            delete it->second;
            m_FFmpegMap.erase(it);	
            it++;
        }
    }

    uint16_t FFmpegManager::add(const char *szFilePath, uint64_t id)
    {
        FFmpegItem *pFFmpegItem; 
        pFFmpegItem = new FFmpegItem(szFilePath);
        pFFmpegItem->Set_id(id);
        m_FFmpegMap.insert(std::make_pair(id, pFFmpegItem));
        m_size++;

        return m_size;
    }

    uint16_t FFmpegManager::remove(uint64_t id)
    {
        std::map<uint64_t, FFmpegItem *>::iterator it = m_FFmpegMap.find(id);
        if (it != m_FFmpegMap.end())
        {
            LOG(INFO) << "Remove FFmpegItem finish: " << id;
            delete it->second;
            m_FFmpegMap.erase(it);
            m_size--;	
        }
        else
            LOG(WARNING) << "FFmpegItem id not exist: " << id;

        return m_size;
    }

    cudaVideoCodec FFmpegManager::Get_NvCodecId()
    {
        std::map<uint64_t, FFmpegItem *>::iterator it = m_FFmpegMap.begin();
        if (it != m_FFmpegMap.end())
        {
            //LOG(INFO) << "FFmpegManager::is open: " << it->second->is_open();
            while(!(it->second->is_open()));
            return it->second->FFmpeg2NvCodecId();
            //it++;
        }
    }

    bool FFmpegManager::check_finish()
    {
        std::map<uint64_t, FFmpegItem*>::iterator it = m_FFmpegMap.begin();
        while (it != m_FFmpegMap.end())
        {
            if(it->second->is_open())
                break;
            it++;
        }

        if(it == m_FFmpegMap.end())
            return true;
        else
            return false;
    }

    std::vector<Demuxframe> FFmpegManager::Demux_all()
    {
        Demuxframe temp;
        m_Demuxframes.clear();
        std::map<uint64_t, FFmpegItem*>::iterator it = m_FFmpegMap.begin();
        while (it != m_FFmpegMap.end())
        {
            if(it->second->is_open())
            {
                it->second->Demux_H264_IPFrame(&temp.pVideo, &temp.nVideoBytes);
                temp.id = it->second->Get_id();
                m_Demuxframes.push_back(temp);
            }
            else
            {
                if(it->second->is_first_finish())
                {
                    LOG(WARNING) << "\"" << it->second->Get_filename() << "\": finished or closed and total demux frames:" << it->second->Get_m_frameCount();
                    it->second->Clear_first_finish();
                    //temp.id = it->second->Get_id();
                    //temp.nVideoBytes = 0;
                    //m_Demuxframes.push_back(temp);
                }
                
            }
            
            it++;
        }

        return m_Demuxframes;
    }

    pthread_t FFmpegManager::start()
	{
		pthread_t tid;
		pthread_create(&tid, NULL, FFmpegManager::run, this);
		return tid;
	}

    bool FFmpegManager::init()
    {
        std::map<uint64_t, FFmpegItem*>::iterator it = m_FFmpegMap.begin();
        while (it != m_FFmpegMap.end())
        {
            it->second->Open();
            it->second->ffprobe();
            it++;
        }
    }

    void *FFmpegManager::run(void *arg)
	{
		FFmpegManager *this_ = reinterpret_cast<FFmpegManager*>(arg);
        std::vector<Demuxframe> Demuxframes;
        this_->m_isEnd = false;
        
        while (1)
        {
            if(this_->check_finish() || this_->m_isEnd)
            {
                this_->m_isEnd = true;
                Demuxframe Demuxframe_empty;
                Demuxframe_empty.nVideoBytes = -1;
                Demuxframe_empty.pVideo = NULL;
                //int write_position = Demux::Demux_queue::getInstance()->enqueue(Demuxframe_empty);
                uint64_t cache_size = Demux::Demux_queue::getInstance()->enqueue_non_blocking(Demuxframe_empty);
                int write_position = Demux::Demux_queue::getInstance()->get_write_position();
                if(this_->first_display)
                {
                    LOG(INFO) << "The last demux write_position:" << (write_position - 1) % QUEUE_SIZE << " and queue size:" << Demux::Demux_queue::getInstance()->query_used_queue_size()
                    << " and nVideoBytes:" << Demuxframe_empty.nVideoBytes << "\n and cache_size: " << cache_size;
                    this_->first_display = false;
                }
                if(Demux::Demux_queue::getInstance()->get_cache_size() == 0 && Demux::Demux_queue::getInstance()->is_clear_cache())
                    break;
            }

            Demuxframes = this_->Demux_all();
            //LOG(INFO) << "Demuxframes size:" << Demuxframes.size();
            if(Demuxframes.size() > 0)
            {
                for (std::vector<Demuxframe>::iterator it = Demuxframes.begin() ; it != Demuxframes.end(); ++it)
                {
                    //int write_position = Demux::Demux_queue::getInstance()->enqueue(*it);
                    uint64_t cache_size = Demux::Demux_queue::getInstance()->enqueue_non_blocking(*it);
                    //LOG(INFO) << "Demux write_position:" << write_position;
                }
            }
        }
	}

    void FFmpegManager::finish()
	{
		m_isEnd = true;
	}
}