PROJECT(AppDec)
MESSAGE(STATUS "<VIDEO_CODEC_DEMO-src-AppDec_thread>")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

FILE(GLOB_RECURSE DIR_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

LIST (APPEND DIR_SRCS ${Nv_Decoder_path})

#MESSAGE(STATUS ${DIR_SRCS})

SET(exe_name AppDec_thread.bin)
ADD_EXECUTABLE(${exe_name} ${DIR_SRCS})
TARGET_LINK_LIBRARIES(${exe_name} nvcuvid pthread cuda cudart avcodec avformat avutil avdevice glog gpujpeg)


