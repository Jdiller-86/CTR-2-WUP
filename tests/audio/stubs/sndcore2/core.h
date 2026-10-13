/* Original host stub for the AX core calls used by the lifecycle helper.
   It checks policy and resource ordering, not Wii U firmware behavior. */
#ifndef CTR_TEST_AX_CORE_H
#define CTR_TEST_AX_CORE_H
#include <stdint.h>
typedef struct { uint32_t renderer, reserved, pipeline; } AXInitParams;
enum { AX_INIT_RENDERER_48KHZ=1, AX_INIT_PIPELINE_SINGLE=0 };
int AXIsInit(void);
void AXInitWithParams(AXInitParams *params);
void AXQuit(void);
#endif
