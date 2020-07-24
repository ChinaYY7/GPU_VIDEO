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

    bool FFmpegManager::init()
    {
        bool sta;
        std::map<uint64_t, FFmpegItem*>::iterator it = m_FFmpegMap.begin();
        while (it != m_FFmpegMap.end())
        {
            sta = it->second->Open();
            if(it->second->ffprobe() < 0)
                sta && false;
            it++;
        }
        return sta;
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

    pthread_t FFmpegManager::start()
	{
		pthread_t tid;
		pthread_create(&tid, NULL, FFmpegManager::run, this);
		return tid;
	}

    void *FFmpegManager::run(void *arg)
	{
		FFmpegManager *this_ = reinterpret_cast<FFmpegManager*>(arg);
        std::vector<Demuxframe> Demuxframes;
        this_->m_isEnd = false;
        
        while (1)
        {   
            //所有解流器完成工作，即，没有需要解流的数据了
            if(this_->check_finish() || this_->m_isEnd)
            {
                this_->m_isEnd = true;
                //通知队列，只需要清空缓存队列
                Demux::Demux_queue::getInstance()->clear_cache();

                //如果缓存队列已空
                if(Demux::Demux_queue::getInstance()->get_cache_size() == 0)
                    break;
                else
                    Demux::Demux_queue::getInstance()->enqueue_cache();
            }
            else
            {
                this_->Demux_all(Demuxframes);
                if(Demuxframes.size() > 0)
                {
                    for (std::vector<Demuxframe>::iterator it = Demuxframes.begin() ; it != Demuxframes.end(); ++it)
                    {
                        //int write_position = Demux::Demux_queue::getInstance()->enqueue(*it);
                        uint64_t cache_size = Demux::Demux_queue::getInstance()->enqueue_non_blocking(*it);
                        if(it->size() == -1)
                        {
                            int write_position = Demux::Demux_queue::getInstance()->get_write_position();
                            //非阻塞情况下，写入的位置目前不能确定，这里显示的写入位置不一定准确
                            LOG(INFO) << "Demux-"  << it->id() << " have write the last frame and write_position:" << (write_position - 1) % QUEUE_SIZE << " and queue size:" << Demux::Demux_queue::getInstance()->query_used_queue_size()
                            << " and nVideoBytes:" << it->size() << "\n and cache_size: " << cache_size;
                        }
                        //LOG(INFO) << "Demux write_position:" << write_position;
                    }
                }
            }
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

    //类最好要用引用，特别是类中包含指针时，否则以参数传递，会将指针复制到临时变量，从而在退出函数时，析构掉原本对象中指针指向的内容
    void FFmpegManager::Enqueue_end(FFmpegItem & item, std::vector<Demuxframe> & Demuxframes)
    {
        Demuxframe Demuxframe_temp(item.Get_id(), -1, nullptr);
        Demuxframes.push_back(Demuxframe_temp); 
    }

    void FFmpegManager::finish()
	{
		m_isEnd = true;
	}

    void FFmpegManager::Demux_all(std::vector<Demuxframe> & Demuxframes)
    {
        uint8_t *pVideo = nullptr;
        int nVideoBytes = 0;

        std::map<uint64_t, FFmpegItem*>::iterator it = m_FFmpegMap.begin();
        Demuxframes.clear();
        while (it != m_FFmpegMap.end())
        {
            if(it->second->is_open())
            {
                it->second->Demux_H264_IPFrame(&pVideo, &nVideoBytes);
                if(it->second->is_open())
                {
                    Demuxframe Demuxframe_temp(it->second->Get_id(), nVideoBytes, pVideo);
                    Demuxframes.push_back(Demuxframe_temp);
                }
                else
                {
                    if(it->second->is_first_finish())
                    {
                        LOG(WARNING) << "\"" << it->second->Get_filename() << "\": finished or closed and total demux frames:" << it->second->Get_m_frameCount();
                        Enqueue_end(*(it->second), Demuxframes);
                        it->second->Clear_first_finish();
                    }
                }
            }
            
            it++;
        }
    }
}