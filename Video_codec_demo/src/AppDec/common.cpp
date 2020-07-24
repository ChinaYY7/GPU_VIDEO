#include <iostream>
#include <string>
#include <fstream> 
#include <dirent.h>
#include <sys/stat.h>
#include <glog/logging.h>

void ShowHelpAndExit(const char *szBadOption = NULL)
{
    std::ostringstream oss;
    if (szBadOption) 
    {
        oss << "Error parsing \"" << szBadOption << "\"" << std::endl;
    }
    oss << "Usage: ./AppDec [Options]" << std::endl
        << "Options:" << std::endl
        << "-i             Input file path or rtsp url" << std::endl
        << "-o             Output file path" << std::endl
        << "-gpu           Ordinal of GPU to use" << std::endl
        << "-File          Input is a file" << std::endl
        << "-rtsp          Input is a rtsp" << std::endl;
    oss << std::endl;
    std::cout << oss.str();
    exit(0);
}

void ParseCommandLine(int argc, char *argv[], char *szInputFileName, char *szOutputFileName, int &iGpu , int &File_or_Rtsp)
{
    std::ostringstream oss;
    int i;
    for (i = 1; i < argc; i++) 
    {
        if (!strcasecmp(argv[i], "-h")) 
            ShowHelpAndExit();
        
        if (!strcasecmp(argv[i], "-i")) 
        {
            if (++i == argc) 
                ShowHelpAndExit("-i");
            
            sprintf(szInputFileName, "%s", argv[i]);
            continue;
        }
        if (!strcasecmp(argv[i], "-o")) 
        {
            if (++i == argc) 
                ShowHelpAndExit("-o");

            sprintf(szOutputFileName, "%s", argv[i]);
            continue;
        }
        if (!strcasecmp(argv[i], "-gpu")) 
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
        ShowHelpAndExit(argv[i]);
    }

    if(argc == 1)
        ShowHelpAndExit(argv[0]);
}

void Set_glog_output(const char *log)
{
    google::InitGoogleLogging(log);
	google::InstallFailureSignalHandler();

	std::string Log_Dir;

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
		LOG(INFO) << "Not set Log_Dir and Print to screen";
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
		LOG(INFO) << "Set Log_Dir to " << Log_Dir << "also Print to screen";
#else
		FLAGS_alsologtostderr =false;
		LOG(INFO) << "Set Log_Dir to " << Log_Dir << "Not Print to screen";
#endif
	}
}

void CheckInputFile(const char *szInFilePath) 
{
    std::ifstream fpIn(szInFilePath, std::ios::in | std::ios::binary);
    if (fpIn.fail()) 
    {
        std::ostringstream err;
        LOG(ERROR) << "Unable to open input file: " << szInFilePath;
    }
}

void CreateFolder(const char *folder)
{
    DIR *dir = NULL;
	if ((dir = opendir(folder)) == NULL)
	{	
		LOG(INFO)<<"Create Folder: "<< folder;
        umask(0);
		if(mkdir(folder, 0777) < 0)
            LOG(ERROR)<<"Create Folder failed: "<< strerror(errno);
	}
	else
		closedir(dir);
}