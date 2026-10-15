#ifndef MOCK_SNDCORE2_VOICE_H
#define MOCK_SNDCORE2_VOICE_H
#include <stdbool.h>
#include <stdint.h>
typedef int AXResult;
typedef struct AXVoice AXVoice;
typedef struct {
    uint16_t dataType,loopingEnabled;
    uint32_t loopOffset,endOffset,currentOffset;
    const void *data;
} AXVoiceOffsets;
typedef struct { uint16_t volume;int16_t delta; } AXVoiceVeData;
typedef struct { uint16_t volume;int16_t delta; } AXVoiceDeviceBusMixData;
typedef struct { AXVoiceDeviceBusMixData bus[4]; } AXVoiceDeviceMixData;
struct AXVoice { unsigned slot;bool live,running;AXVoiceOffsets offsets; };
enum {
    AX_VOICE_STATE_STOPPED=0,AX_VOICE_STATE_PLAYING=1,
    AX_VOICE_FORMAT_LPCM16=10,AX_VOICE_LOOP_DISABLED=0,
    AX_VOICE_SRC_TYPE_NONE=0,AX_VOICE_RATIO_RESULT_SUCCESS=0,
    AX_DEVICE_TYPE_TV=0,AX_DEVICE_TYPE_DRC=1,AX_RESULT_SUCCESS=0
};
AXVoice *AXAcquireVoice(uint32_t,void (*)(void *),void *);
void AXFreeVoice(AXVoice *);
void AXSetVoiceState(AXVoice *,uint32_t);
bool AXCheckVoiceOffsets(AXVoiceOffsets *);
void AXSetVoiceOffsets(AXVoice *,AXVoiceOffsets *);
void AXSetVoiceSrcType(AXVoice *,uint32_t);
int AXSetVoiceSrcRatio(AXVoice *,float);
void AXSetVoiceVe(AXVoice *,AXVoiceVeData *);
AXResult AXSetVoiceDeviceMix(AXVoice *,uint32_t,uint32_t,AXVoiceDeviceMixData *);
void AXSetVoiceCurrentOffset(AXVoice *,uint32_t);
void AXGetVoiceOffsets(AXVoice *,AXVoiceOffsets *);
bool AXIsVoiceRunning(AXVoice *);
#endif
