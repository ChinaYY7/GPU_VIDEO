#pragma once
#include <glog/logging.h>
#include <tbb/tick_count.h>
#include <iostream>

#include "../Utils/Logger.h"
#include "NvDecoder/NvDecoder.h"
#include "common.h"
#include "Singleton.h"

#define START_TIMER auto start = std::chrono::high_resolution_clock::now();
#define STOP_TIMER(print_message) LOG(INFO) << print_message << \
    std::chrono::duration_cast<std::chrono::milliseconds>( \
    std::chrono::high_resolution_clock::now() - start).count() \
    << " ms ";

namespace NvCodec
{
    class common : public Singleton<common>
    {
        friend class Singleton<common>;
        private:
            bool File_or_Rtsp;
            long int filename_count;
            std::string Log_Dir;
            std::string InputFileName;
            std::vector<std::string> InputFileNames;
            int iGpu;
            Rect cropRect;
            Dim  resizeDim;
            std::string OutputFloder;
            bool finish;
            bool first_i;

            void  ShowHelpAndExit(const char *szBadOption = NULL);

        public:
            common();
            ~common(){}
            void ParseCommandLine(int argc, char *argv[]);
            void Set_glog_output(const char *log);
            int uint8_t_append(uint8_t *src_data, int src_size, uint8_t *append_data, int append_size);
            void Write_a_file(std::string file_name, const char *data, int data_size);
            std::string Generate_file_name(uint64_t video_id, const char *folder_path=NULL);
            void CreateFolder(const char *folder);

            bool is_File(void)
            {
                return (File_or_Rtsp == 0);
            }
    
            bool is_Rtsp(void)
            {
                return (File_or_Rtsp == 1);
            }

            void set_finish(bool sta)
            {
                finish = sta;
            }

            bool is_finish()
            {
                return finish;
            }

            inline std::chrono::high_resolution_clock::time_point Start_time()
            {
                return std::chrono::high_resolution_clock::now();
            }
            
            //单位:ms
            inline int64_t Duration(std::chrono::high_resolution_clock::time_point start_time)
            {
                return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            }

            int get_iGpu()
            {
                return iGpu;
            }

            std::vector<std::string> get_input_file_name()
            {
                return InputFileNames;
            }

            std::string get_OutputFloder()
            {
                return OutputFloder;
            }
    };
}
