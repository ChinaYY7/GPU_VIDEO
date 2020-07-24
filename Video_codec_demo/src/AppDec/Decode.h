#pragma once
#include <cuda.h>
void DecodeMediaFile(CUcontext cuContext, const char *szInFilePath, const char *szOutFilePath);