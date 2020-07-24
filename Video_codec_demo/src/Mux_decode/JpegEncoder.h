#pragma once
#include <pthread.h>
#include <stdint.h>
#include "libgpujpeg/gpujpeg.h"
#include "Singleton.h"

#define FRAME_SIZE 3200000

namespace NvCodec
{

	typedef struct IdData
	{
		uint64_t videoId;
		uint8_t *pData;
		uint64_t data_size;
		uint8_t data[FRAME_SIZE];
		bool empty;
	}IdData;

	class JpegEncoder : public Singleton<JpegEncoder>
	{
			friend class Singleton<JpegEncoder>;
		public:
			JpegEncoder();
			~JpegEncoder();

			void set_param(int width, int height, int device)
			{
				width_ = width;
				height_ = height;
				jpegDevice_ = device;
			}
			pthread_t start();
			int init();
			int reCreateEncoder(int width, int height);
			int enqueue(const uint64_t &videoId, uint8_t *pData, uint64_t data_size);
			int enqueue_empty(void);
			int encode(IdData data);
			void finish();

			int width() { return width_; }
			int height() { return height_; }

		private:
			static void* run(void *arg);

			int 		width_;
			int 		height_;
			int 		handleRead_;
			int 		handleWrite_;
			int 		jpegDevice_;
			bool 		isEnd_;
			uint8_t     *pdata_;
			struct gpujpeg_parameters 		param_;
			struct gpujpeg_image_parameters paramImage_;
			struct gpujpeg_encoder 			*pEncoder_;
	};

}
