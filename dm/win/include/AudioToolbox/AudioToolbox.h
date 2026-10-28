#pragma once
#include <CoreFoundation/CoreFoundation.h>

typedef void *AudioUnit;
typedef void *AudioComponent;
typedef void *AudioConverterRef;
typedef uint32_t AudioDeviceID;
typedef uint32_t AudioUnitPropertyID;
typedef uint32_t AudioUnitScope;
typedef uint32_t AudioUnitElement;
typedef uint32_t AudioUnitRenderActionFlags;
typedef uint32_t AudioConverterPropertyID;

enum {
    kAudioObjectUnknown = 0,
    kAudioUnitType_Output = 1,
    kAudioUnitSubType_DefaultOutput = 2,
    kAudioUnitSubType_RemoteIO = 3,
    kAudioUnitManufacturer_Apple = 4,
    kAudioUnitScope_Global = 0,
    kAudioUnitScope_Input = 1,
    kAudioUnitScope_Output = 2,
    kAudioUnitProperty_StreamFormat = 8,
    kAudioUnitProperty_SetRenderCallback = 23,
    kAudioOutputUnitProperty_CurrentDevice = 2000,
    kAudioDevicePropertyBufferFrameSize = 2001,
    kAudioDevicePropertyBufferFrameSizeRange = 2002,
    kAudioFormatLinearPCM = 1,
    kAudioFormatMPEG4AAC = 2,
    kLinearPCMFormatFlagIsSignedInteger = 1,
    kLinearPCMFormatFlagIsPacked = 8,
    kAudioConverterDecompressionMagicCookie = 1
};

typedef struct {
    uint64_t mSampleTime;
} AudioTimeStamp;

typedef struct {
    UInt32 mNumberChannels;
    UInt32 mDataByteSize;
    void *mData;
} AudioBuffer;

typedef struct {
    UInt32 mNumberBuffers;
    AudioBuffer mBuffers[1];
} AudioBufferList;

typedef struct {
    Float64 mSampleRate;
    UInt32 mFormatID;
    UInt32 mFormatFlags;
    UInt32 mBytesPerPacket;
    UInt32 mFramesPerPacket;
    UInt32 mBytesPerFrame;
    UInt32 mChannelsPerFrame;
    UInt32 mBitsPerChannel;
    UInt32 mReserved;
} AudioStreamBasicDescription;

typedef struct {
    int64_t mStartOffset;
    UInt32 mVariableFramesInPacket;
    UInt32 mDataByteSize;
} AudioStreamPacketDescription;

typedef struct {
    Float64 mMinimum;
    Float64 mMaximum;
} AudioValueRange;

typedef struct {
    UInt32 componentType;
    UInt32 componentSubType;
    UInt32 componentManufacturer;
    UInt32 componentFlags;
    UInt32 componentFlagsMask;
} AudioComponentDescription;

typedef OSStatus (*AURenderCallback)(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData);

typedef struct {
    AURenderCallback inputProc;
    void *inputProcRefCon;
} AURenderCallbackStruct;

typedef void (*AudioUnitPropertyListenerProc)(void *inRefCon, AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement);
typedef OSStatus (*AudioConverterComplexInputDataProc)(AudioConverterRef inAudioConverter, UInt32 *ioNumberDataPackets, AudioBufferList *ioData, AudioStreamPacketDescription **outDataPacketDescription, void *inUserData);

AudioComponent AudioComponentFindNext(AudioComponent inComponent, const AudioComponentDescription *inDesc);
OSStatus AudioComponentInstanceNew(AudioComponent inComponent, AudioUnit *outInstance);
OSStatus AudioComponentInstanceDispose(AudioUnit inInstance);
OSStatus AudioUnitInitialize(AudioUnit inUnit);
OSStatus AudioUnitUninitialize(AudioUnit inUnit);
OSStatus AudioOutputUnitStart(AudioUnit ci);
OSStatus AudioOutputUnitStop(AudioUnit ci);
OSStatus AudioUnitGetProperty(AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, void *outData, UInt32 *ioDataSize);
OSStatus AudioUnitSetProperty(AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, const void *inData, UInt32 inDataSize);
OSStatus AudioUnitAddPropertyListener(AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc, void *inProcUserData);
OSStatus AudioUnitRemovePropertyListenerWithUserData(AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc, void *inProcUserData);
static inline OSStatus AudioConverterNew(const AudioStreamBasicDescription *inSourceFormat, const AudioStreamBasicDescription *inDestinationFormat, AudioConverterRef *outAudioConverter) {
    (void)inSourceFormat; (void)inDestinationFormat; if (outAudioConverter) *outAudioConverter = NULL; return -1;
}
static inline OSStatus AudioConverterDispose(AudioConverterRef inAudioConverter) { (void)inAudioConverter; return 0; }
static inline OSStatus AudioConverterSetProperty(AudioConverterRef inAudioConverter, AudioConverterPropertyID inPropertyID, UInt32 inPropertyDataSize, const void *inPropertyData) {
    (void)inAudioConverter; (void)inPropertyID; (void)inPropertyDataSize; (void)inPropertyData; return -1;
}
static inline OSStatus AudioConverterFillComplexBuffer(AudioConverterRef inAudioConverter, AudioConverterComplexInputDataProc inInputDataProc, void *inInputDataProcUserData, UInt32 *ioOutputDataPacketSize, AudioBufferList *outOutputData, AudioStreamPacketDescription *outPacketDescription) {
    (void)inAudioConverter; (void)inInputDataProc; (void)inInputDataProcUserData;
    if (ioOutputDataPacketSize) *ioOutputDataPacketSize = 0;
    (void)outOutputData; (void)outPacketDescription;
    return -1;
}
