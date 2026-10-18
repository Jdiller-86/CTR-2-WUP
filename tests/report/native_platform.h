/* Test-only SDK declarations for the actual installed frontend translation unit.
 * Native cross-builds check real WUT declarations; these doubles drive host flow. */
#ifndef CTR_NATIVE_TEST_PLATFORM_H
#define CTR_NATIVE_TEST_PLATFORM_H
#include <stdbool.h>
#include <stdint.h>
#define TRUE 1
typedef uint64_t OSTime;
typedef enum { PROCUI_STATUS_IN_FOREGROUND,PROCUI_STATUS_IN_BACKGROUND,
    PROCUI_STATUS_RELEASE_FOREGROUND,PROCUI_STATUS_EXITING } ProcUIStatus;
typedef int VPADReadError;
typedef struct { uint32_t trigger; } VPADStatus;
enum { VPAD_CHAN_0,VPAD_READ_SUCCESS=0,VPAD_READ_NO_SAMPLES=-1,VPAD_BUTTON_MINUS=0x1000,
       PROCUI_CALLBACK_HOME_BUTTON_DENIED=3 };
void OSReport(const char *,...);
uint64_t OSGetTime(void);
uint64_t OSGetTitleID(void);
uint32_t __OSGetProcessSDKVersion(void);
uint64_t OSTicksToMilliseconds(uint64_t);
uint64_t OSMillisecondsToTicks(uint64_t);
void OSSleepTicks(uint64_t);
void OSSavesDone_ReadyToRelease(void);
void WHBProcInit(void);
void WHBProcShutdown(void);
void SYSLaunchMenu(void);
ProcUIStatus ProcUIProcessMessages(int);
void ProcUIDrawDoneRelease(void);
void ProcUIShutdown(void);
void ProcUIClearCallbacks(void);
void ProcUISetSaveCallback(uint32_t (*callback)(void *),void *);
void ProcUIRegisterCallback(int,uint32_t (*callback)(void *),void *,int);
int VPADRead(int,VPADStatus *,int,VPADReadError *);
#endif
