#ifndef MOCK_SNDCORE2_CORE_H
#define MOCK_SNDCORE2_CORE_H
#include <stdint.h>
int AXIsInit(void);
uint32_t AXGetInputSamplesPerSec(void);
uint32_t AXGetInputSamplesPerFrame(void);
int AXRegisterAppFrameCallback(void (*callback)(void));
int AXDeregisterAppFrameCallback(void (*callback)(void));
#endif
