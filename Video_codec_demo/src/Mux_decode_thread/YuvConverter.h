#pragma once
#include <string.h>

namespace GpuDecode
{
    template<typename T>
    class YuvConverter 
    {
        private:
            T *pQuad;
            int nWidth, nHeight;

        public:
            YuvConverter(int nWidth, int nHeight) : nWidth(nWidth), nHeight(nHeight) 
            {
                pQuad = new T[nWidth * nHeight / 4];
            }

            ~YuvConverter() 
            {
                delete pQuad;
            }

            void PlanarToUVInterleaved(T *pFrame, int nPitch = 0) 
            {
                if (nPitch == 0) 
                    nPitch = nWidth;
                
                T *puv = pFrame + nPitch * nHeight;
                if (nPitch == nWidth) 
                    memcpy(pQuad, puv, nWidth * nHeight / 4 * sizeof(T));
                else 
                {
                    for (int i = 0; i < nHeight / 2; i++) 
                        memcpy(pQuad + nWidth / 2 * i, puv + nPitch / 2 * i, nWidth / 2 * sizeof(T));
                }
                T *pv = puv + (nPitch / 2) * (nHeight / 2);
                for (int y = 0; y < nHeight / 2; y++) 
                {
                    for (int x = 0; x < nWidth / 2; x++) 
                    {
                        puv[y * nPitch + x * 2] = pQuad[y * nWidth / 2 + x];
                        puv[y * nPitch + x * 2 + 1] = pv[y * nPitch / 2 + x];
                    }
                }
            }

            void UVInterleavedToPlanar(T *pFrame, int nPitch = 0) 
            {
                if (nPitch == 0) 
                    nPitch = nWidth;
                
                T *puv = pFrame + nPitch * nHeight, *pu = puv, *pv = puv + nPitch * nHeight / 4;
                for (int y = 0; y < nHeight / 2; y++) 
                {
                    for (int x = 0; x < nWidth / 2; x++) 
                    {
                        pu[y * nPitch / 2 + x] = puv[y * nPitch + x * 2];
                        pQuad[y * nWidth / 2 + x] = puv[y * nPitch + x * 2 + 1];
                    }
                }
                if (nPitch == nWidth) 
                    memcpy(pv, pQuad, nWidth * nHeight / 4 * sizeof(T));
                else 
                {
                    for (int i = 0; i < nHeight / 2; i++) 
                        memcpy(pv + nPitch / 2 * i, pQuad + nWidth / 2 * i, nWidth / 2 * sizeof(T));
                }
            }
    };
}
