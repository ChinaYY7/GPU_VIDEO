#pragma once
#include <glog/logging.h>

inline bool check(int e, int iLine, const char *szFile) 
{
    if (e < 0) 
    {
        LOG(ERROR) << "General error " << e << " at line " << iLine << " in file " << szFile;
        return false;
    }
    return true;
}

#define ck(call) check(call, __LINE__, __FILE__)

#define START_TIMER auto start = std::chrono::high_resolution_clock::now();
#define STOP_TIMER(print_message) std::cout << print_message << \
    std::chrono::duration_cast<std::chrono::milliseconds>( \
    std::chrono::high_resolution_clock::now() - start).count() \
    << " ms " << std::endl;


void ShowHelpAndExit(const char *szBadOption = NULL);
void ParseCommandLine(int argc, char *argv[], char *szInputFileName, char *szOutputFileName, int &iGpu , int &File_or_Rtsp);
void Set_glog_output(const char *log);
void CheckInputFile(const char *szInFilePath);
void CreateFolder(const char *folder);