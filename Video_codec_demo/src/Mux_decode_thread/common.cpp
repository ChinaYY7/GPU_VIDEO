#include <iostream>
#include <string>
#include <fstream> 
#include <dirent.h>
#include <sys/stat.h>
#include <glog/logging.h>

#include "common.h"
#include "../Common/AppDecUtils.h"
#include "NvDecoder/NvDecoder.h"
#include "../Utils/NvCodecUtils.h"
#include "../Utils/FFmpegDemuxer.h"

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();
namespace NvCodec
{
    common::common():iGpu(0),File_or_Rtsp(0),filename_count(0), finish(false), first_i(true)
    {
        Log_Dir = ".";
    }

    std::string common::Generate_file_name(uint64_t video_id, const char *folder_path)
    {
        std::string path;
        std::string test;
        if(folder_path==NULL)
            path = OutputFloder;
        else
            path.assign(folder_path);

        time_t nowTime;
        struct tm fmtTime;
        time(&nowTime);
        localtime_r(&nowTime, &fmtTime);

        int curYear = fmtTime.tm_year + 1900;
        int curMonth = fmtTime.tm_mon + 1;
        int curDay = fmtTime.tm_mday;

        CreateFolder(path.c_str());
        path = path + "/" + std::to_string(video_id);
        //LOG(WARNING) << "path: " << path;
        CreateFolder(path.c_str());

        char buf[256];
        int len = snprintf(buf, 256, "%s/%02ld_%04d_%02d_%02d_%02d%02d%02d_%03ld", path.c_str(), video_id, curYear, curMonth, curDay, fmtTime.tm_hour, fmtTime.tm_min, fmtTime.tm_sec, filename_count);
        filename_count++;
        std::string filename(buf, len);

        //LOG(WARNING) << "filename: " << filename;
        return filename;
    }

    void common::CreateFolder(const char *folder)
    {
        std::string path;
        if(folder==NULL)
            path = OutputFloder;
        else
            path.assign(folder);

        DIR *dir = NULL;
        
        if ((dir = opendir(path.c_str())) == NULL)
        {	
            LOG(INFO)<<"Create Folder: "<< path;
            umask(0);
            if(mkdir(path.c_str(), 0777) < 0)
                LOG(ERROR)<<"Create Folder failed: "<< strerror(errno);
        }
        else
            closedir(dir);
    }

    void common::Write_a_file(std::string file_name, const char *data, int data_size)
    {
        std::ofstream fpOut(file_name, std::ios::app | std::ios::binary);
        if (!fpOut)
            LOG(ERROR) << "Unable to open output file: " << file_name;
        
        fpOut.write(data, data_size);

        fpOut.close();
    }

    void common::Set_glog_output(const char *log)
    {
        google::InitGoogleLogging(log);
        google::InstallFailureSignalHandler();


        //设置记录到标准输出的颜色消息
        FLAGS_colorlogtostderr =true;

        //设置是否在磁盘已满时避免日志记录到磁盘
        FLAGS_stop_logging_if_full_disk =true;

        //未设置日志目录
        if(Log_Dir.size() == 0)
        {
    #ifdef LOG_TO_STDERR
                //设置日志消息转到标准输出而不是日志文件
            FLAGS_logtostderr =true;
            LOG(WARNING) << "Not set Log_Dir and Print to screen";
    #else
            FLAGS_logtostderr =false;
    #endif
        }
        else
        {
            Log_Dir.append("/log");
            std::ofstream outFile;
            outFile.open(Log_Dir.c_str(), std::ofstream::out | std::ofstream::app);
            outFile.close();
            FLAGS_log_dir = Log_Dir.c_str();
    #ifdef LOG_TO_STDERR
            //设置日志消息除了日志文件之外还去标准输出
            FLAGS_alsologtostderr =true;
            LOG(WARNING) << "Set Log_Dir to " << Log_Dir << " also Print to screen";
    #else
            FLAGS_alsologtostderr =false;
            LOG(WARNING) << "Set Log_Dir to " << Log_Dir << " Not Print to screen";
    #endif
        }
    }

    int common::uint8_t_append(uint8_t *src_data, int src_size, uint8_t *append_data, int append_size)
    {
        int i;
        int new_size = src_size + append_size;
        for(i = src_size; i < new_size; i++)
        {
            src_data[i]=append_data[i-src_size];
        }

        return new_size;
    }

    void common::ShowHelpAndExit(const char *szBadOption)
    {
        bool bThrowError = false;
        std::ostringstream oss;
        if (szBadOption) 
        {
            bThrowError = true;
            LOG(ERROR) << "Error parsing \"" << szBadOption << "\"";
        }
        oss << "Options:" << std::endl
            << "-i             Input file path" << std::endl
            << "-o             Output file path" << std::endl
            << "-gpu           Ordinal of GPU to use" << std::endl
            << "-File          Input is a file" << std::endl
            << "-rtsp          Input is a rtsp" << std::endl
            << "-crop l,t,r,b  Crop rectangle in left,top,right,bottom (ignored for case 0)" << std::endl
            << "-resize WxH    Resize to dimension W times H (ignored for case 0)" << std::endl
            ;
        oss << std::endl;
        if (bThrowError)
        {
            std::cout << oss.str();
            exit(0);
        }
        else
        {
            std::cout << oss.str();
            ShowDecoderCapability();
            exit(0);
        }
    }

    void common::ParseCommandLine(int argc, char *argv[])
    {
        std::ostringstream oss;
        int i;
        for (i = 1; i < argc; i++) 
        {
            if (!_stricmp(argv[i], "-h")) 
                ShowHelpAndExit();
            
            if (!_stricmp(argv[i], "-i")) 
            {
                while(1)
                {
                    if((++i == argc) || (_stricmp(argv[i], "-o") == 0) || (_stricmp(argv[i], "-gpu") == 0) || (_stricmp(argv[i], "-file") == 0) || (_stricmp(argv[i], "-rtsp") == 0) ||
                    (_stricmp(argv[i], "-crop") == 0) || (_stricmp(argv[i], "-resize") == 0) || (((_stricmp(argv[i], "-i") == 0) && (first_i != true))))
                    {
                        first_i = false;
                        if(InputFileNames.empty())
                            ShowHelpAndExit("-i");
                        else
                        {
                            i--;
                            break;
                        }
                        
                    }
                    else
                    {
                        InputFileName.assign(argv[i]);
                        InputFileNames.push_back(InputFileName);
                        //LOG(INFO) << "get file_name: " << argv[i];
                    }
                }
                
                continue;
            }

            if (!_stricmp(argv[i], "-o")) 
            {
                if (++i == argc) 
                    ShowHelpAndExit("-o");
                
                OutputFloder.assign(argv[i]);
                continue;
            }
            
            if (!_stricmp(argv[i], "-gpu")) 
            {
                if (++i == argc) 
                    ShowHelpAndExit("-gpu");
    
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
            if (!_stricmp(argv[i], "-crop")) 
            {
                if (++i == argc || 4 != sscanf(argv[i], "%d,%d,%d,%d",&cropRect.l, &cropRect.t, &cropRect.r, &cropRect.b)) 
                    ShowHelpAndExit("-crop");
        
                if ((cropRect.r - cropRect.l) % 2 == 1 || (cropRect.b - cropRect.t) % 2 == 1) 
                {
                    std::cout << "Cropping rect must have width and height of even numbers" << std::endl;
                    exit(1);
                }
                continue;
            }
            if (!_stricmp(argv[i], "-resize")) 
            {
                if (++i == argc || 2 != sscanf(argv[i], "%dx%d", &resizeDim.w, &resizeDim.h)) 
                    ShowHelpAndExit("-resize");
            
                if (resizeDim.w % 2 == 1 || resizeDim.h % 2 == 1) 
                {
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
}
