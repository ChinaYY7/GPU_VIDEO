MESSAGE(STATUS "<VIDEO_CODEC_DEMO-src>")
SET(LOG_TO_STDERR ON)
IF(LOG_TO_STDERR STREQUAL ON)
    MESSAGE (STATUS "\tLOG_TO_STDERR: ON")
    ADD_DEFINITIONS (-DLOG_TO_STDERR)
ELSE()
    MESSAGE (STATUS "\tLOG_TO_STDERR: OFF")
ENDIF()
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/NvCodec)
FILE(GLOB_RECURSE Nv_Decoder_path ${CMAKE_CURRENT_SOURCE_DIR}/NvCodec/NvDecoder/*.cpp)
FILE(GLOB_RECURSE Nv_Encoder_path ${CMAKE_CURRENT_SOURCE_DIR}/NvCodec/NvEncoder/*.cpp)
ADD_SUBDIRECTORY (${CMAKE_CURRENT_SOURCE_DIR}/AppDec)
ADD_SUBDIRECTORY (${CMAKE_CURRENT_SOURCE_DIR}/AppDec_thread)
ADD_SUBDIRECTORY (${CMAKE_CURRENT_SOURCE_DIR}/Mux_decode)
ADD_SUBDIRECTORY (${CMAKE_CURRENT_SOURCE_DIR}/Mux_decode_thread)

