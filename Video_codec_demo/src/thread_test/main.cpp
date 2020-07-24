#include <iostream>
#include <algorithm>
#include <thread>
#include <string>
#include <glog/logging.h>
#include <fstream> 

std::string Log_Dir;

void Set_glog_output(const char *log)
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
        Log_Dir.append("/run.log");
        std::ofstream outFile;
        outFile.open(Log_Dir.c_str(), std::ofstream::out | std::ofstream::app);
        outFile.close();
            FLAGS_log_dir = Log_Dir.c_str();
#ifdef LOG_TO_STDERR
            //设置日志消息除了日志文件之外还去标准输出
        FLAGS_alsologtostderr =true;
        LOG(WARNING) << "Set Log_Dir to " << Log_Dir << "also Print to screen";
#else
        FLAGS_alsologtostderr =false;
        LOG(WARNING) << "Set Log_Dir to " << Log_Dir << "Not Print to screen";
#endif
    }
}

int main(int argc, char **argv) 
{
    
    Set_glog_output(argv[0]);

    
        
    return 0;
}
