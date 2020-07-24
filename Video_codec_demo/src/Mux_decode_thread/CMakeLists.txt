PROJECT(Mux_decode_thread)
MESSAGE(STATUS "<VIDEO_CODEC_DEMO-src-Mux_decode_thread>")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

FILE(GLOB_RECURSE DIR_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

#LIST (APPEND DIR_SRCS ${Nv_Decoder_path})
#LIST (REMOVE_ITEM DIR_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/FFmpegManager.cpp")
#MESSAGE(STATUS ${DIR_SRCS})

SET(exe_name Mux_decode_thread.bin)
ADD_EXECUTABLE(${exe_name} ${DIR_SRCS})
TARGET_LINK_LIBRARIES(${exe_name} nvcuvid pthread cuda cudart avcodec avformat avutil avdevice glog gpujpeg)



