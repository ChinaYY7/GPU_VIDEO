#include <glog/logging.h>
#include <cuda.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <assert.h>
#include <iostream>

#include "common.h"
#include "JpegEncoder.h"
#include "Enjpeg_queue.h"

namespace NvCodec
{
	JpegEncoder::JpegEncoder():isEnd_(false),pEncoder_(NULL)
	{
		int fd[2];
		if (pipe(fd) < 0)
		{
			LOG(ERROR) << "JpegEncoder::JpegEncoder(): pipe error";
			assert(0);
		}
		handleRead_ = fd[0];
		handleWrite_ = fd[1];

		pdata_ = new uint8_t[3200000];
	}

	JpegEncoder::~JpegEncoder()
	{
		close(handleRead_);
		delete pdata_;
	}

	pthread_t JpegEncoder::start()
	{
		pthread_t tid;
		pthread_create(&tid, NULL, JpegEncoder::run, this);
		return tid;
	}

	int JpegEncoder::init()
	{
		//编/解码器参数初始化
		gpujpeg_set_default_parameters(&param_);
		param_.quality = 60; 
		param_.restart_interval = 16;
		param_.interleaved = 0;
		
		//输入图像参数初始化
		gpujpeg_image_set_default_parameters(&paramImage_);
		paramImage_.width = width_;
		paramImage_.height = height_;
		paramImage_.comp_count = 3;
		paramImage_.color_space = GPUJPEG_YCBCR_JPEG;
		
		paramImage_.pixel_format = GPUJPEG_420_U8_P0P1P2;
	
		gpujpeg_parameters_chroma_subsampling_420(&param_);

		std::string device = std::to_string(jpegDevice_);
		//初始化GPU
		setenv("CUDA_VISIBLE_DEVICES", "0,1", 1);  //使GPU设备可见
		int ret;
		if (ret = gpujpeg_init_device(jpegDevice_, 0))
		{   
			LOG(ERROR) << "gpujpeg_init_device Fail!  and  jpegDevice: " << jpegDevice_ << " errorcode: " << ret;
			return -1;
		}

		pEncoder_  = gpujpeg_encoder_create(NULL);
		assert(pEncoder_ != NULL);
		LOG(WARNING) << "Jpeg_GPU init on GPU-" << device << " success ";
	}		

	int JpegEncoder::reCreateEncoder(int width, int height)
	{
		LOG(INFO) << "JpegEncoder::reCreateEncoder !";
		if (paramImage_.width != width ||
			paramImage_.height != height ||
			pEncoder_ == NULL)
		{
			gpujpeg_encoder_destroy(pEncoder_);
			width_ = width;
			height_ = height;
			paramImage_.width = width_;
			paramImage_.height = height_;
			//pEncoder_ = gpujpeg_encoder_create(&param_, &paramImage_);
			//add by yy
			pEncoder_  = gpujpeg_encoder_create(NULL);
			if (pEncoder_ == NULL)
				return -1;
		}
		return 0;
	}

	void *JpegEncoder::run(void *arg)
	{
		JpegEncoder *this_ = reinterpret_cast<JpegEncoder*>(arg);
		struct timeval startTime, endTime;
		int read_position;
		int queue_size;
		this_->init();
		while (1)
		{
			IdData idData;

			read_position = Enjpeg_queue::getInstance()->dequeue(idData);

			queue_size = Enjpeg_queue::getInstance()->query_used_queue_size();
			//LOG(INFO) << "read_position: " << read_position;
			//read(this_->handleRead_, &idData, sizeof(IdData));
			
			if(!idData.empty)
			{
				//gettimeofday(&startTime, 0);	
				this_->encode(idData);
				//gettimeofday(&endTime, 0);
			}

			//处理完队列中的数据再退出
			if (this_->isEnd_ && (Enjpeg_queue::getInstance()->query_used_queue_size() == 0)) 	
				break;
		}
	}

	int JpegEncoder::enqueue(const uint64_t &videoId, uint8_t *pData, uint64_t data_size)
	{
		IdData idData; 

		memcpy(idData.data, pData, data_size);//单独存储每帧为编码的数据，若存储地址，会被覆盖
		idData.videoId = videoId;
		idData.pData = pdata_;
		idData.data_size = data_size;
		idData.empty = false;

		int write_position = Enjpeg_queue::getInstance()->enqueue(idData);
		
		//LOG(INFO) << "write_position: " << write_position << " and uesd_queue_size: " << Enjpeg_queue::getInstance()->query_used_queue_size();
		if(write_position < 0)
			return -1;
		else 
			return 0;
			
		//write(handleWrite_, &idData, sizeof(idData));
	}

	int JpegEncoder::enqueue_empty()
	{
		IdData idData; 
		idData.empty = true;
		int write_position = Enjpeg_queue::getInstance()->enqueue(idData);

		if(write_position < 0)
			return -1;
		else 
			return 0;
	}

	int JpegEncoder::encode(IdData data)
	{	
		struct gpujpeg_encoder_input encoderInput;
		uint8_t *pImage = NULL;
		int imageSize = 0;
		encoderInput.image = data.data;
		encoderInput.type = GPUJPEG_ENCODER_INPUT_IMAGE;

		if (gpujpeg_encoder_encode(pEncoder_, &param_, &paramImage_, &encoderInput, &pImage, &imageSize) != 0)
		{
			LOG(ERROR) << "In JpegEncoder::encode() gpujpeg_encoder_encode error";
			//delete[] pData;
			return -1;
		}

		std::string capFile = NvCodec::common::getInstance()->Generate_file_name(data.videoId);
		capFile += ".jpg";
		//LOG(INFO) << "saveAsFile : " << capFile;
		NvCodec::common::getInstance()->Write_a_file(capFile,reinterpret_cast<char*>(pImage) ,imageSize);

		//std::string capFile_yuv = capFile += ".yuv";
		//NvCodec::common::getInstance()->Write_a_file(capFile_yuv,reinterpret_cast<char*>(data.data) ,data.data_size);
		
		return 0;
	}

	void JpegEncoder::finish()
	{
		isEnd_ = true;
		enqueue_empty();
		close(handleWrite_);
	}
}
