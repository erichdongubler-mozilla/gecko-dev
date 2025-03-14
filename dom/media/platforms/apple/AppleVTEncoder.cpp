/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AppleVTEncoder.h"

#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFByteOrder.h>
#include <CoreFoundation/CFDictionary.h>
#include <MacTypes.h>

#include "AnnexB.h"
#include "AppleUtils.h"
#include "H264.h"
#include "ImageContainer.h"

#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ImageUtils.h"

namespace mozilla {
extern LazyLogModule sPEMLog;
#define LOGE(fmt, ...)                       \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Error, \
          ("[AppleVTEncoder] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGW(fmt, ...)                         \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Warning, \
          ("[AppleVTEncoder] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGD(fmt, ...)                       \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Debug, \
          ("[AppleVTEncoder] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGV(fmt, ...)                         \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Verbose, \
          ("[AppleVTEncoder] %s: " fmt, __func__, ##__VA_ARGS__))

static CFDictionaryRef BuildEncoderSpec(const bool aHardwareNotAllowed,
                                        const bool aLowLatencyRateControl) {
  if (__builtin_available(macos 11.3, *)) {
    if (aLowLatencyRateControl) {
      // If doing low-latency rate control, the hardware encoder is required.
      const void* keys[] = {
          kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder,
          kVTVideoEncoderSpecification_EnableLowLatencyRateControl};
      const void* values[] = {kCFBooleanTrue, kCFBooleanTrue};

      static_assert(std::size(keys) == std::size(values),
                    "Non matching keys/values array size");
      return CFDictionaryCreate(kCFAllocatorDefault, keys, values,
                                std::size(keys), &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
    }
  }
  const void* keys[] = {
      kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder};
  const void* values[] = {aHardwareNotAllowed ? kCFBooleanFalse
                                              : kCFBooleanTrue};

  static_assert(std::size(keys) == std::size(values),
                "Non matching keys/values array size");
  return CFDictionaryCreate(kCFAllocatorDefault, keys, values, std::size(keys),
                            &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
}

static void FrameCallback(void* aEncoder, void* aFrameRefCon, OSStatus aStatus,
                          VTEncodeInfoFlags aInfoFlags,
                          CMSampleBufferRef aSampleBuffer) {
  (static_cast<AppleVTEncoder*>(aEncoder))
      ->OutputFrame(aStatus, aInfoFlags, aSampleBuffer);
}

static bool SetAverageBitrate(VTCompressionSessionRef& aSession,
                              uint32_t aBitsPerSec) {
  int64_t bps(aBitsPerSec);
  AutoCFRelease<CFNumberRef> bitrate(
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &bps));
  return VTSessionSetProperty(aSession,
                              kVTCompressionPropertyKey_AverageBitRate,
                              bitrate) == noErr;
}

static bool SetConstantBitrate(VTCompressionSessionRef& aSession,
                               uint32_t aBitsPerSec) {
  int32_t bps(aBitsPerSec);
  AutoCFRelease<CFNumberRef> bitrate(
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &bps));

  if (__builtin_available(macos 13.0, *)) {
    int rv = VTSessionSetProperty(aSession,
                                  kVTCompressionPropertyKey_ConstantBitRate,
                                  bitrate) == noErr;
    if (rv == kVTPropertyNotSupportedErr) {
      LOGE("Constant bitrate not supported.");
    }
  }
  return false;
}

static bool SetBitrateAndMode(VTCompressionSessionRef& aSession,
                              BitrateMode aBitrateMode, uint32_t aBitsPerSec) {
  if (aBitrateMode == BitrateMode::Variable) {
    return SetAverageBitrate(aSession, aBitsPerSec);
  }
  return SetConstantBitrate(aSession, aBitsPerSec);
}

static bool SetFrameRate(VTCompressionSessionRef& aSession, int64_t aFPS) {
  AutoCFRelease<CFNumberRef> framerate(
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &aFPS));
  return VTSessionSetProperty(aSession,
                              kVTCompressionPropertyKey_ExpectedFrameRate,
                              framerate) == noErr;
}

static bool SetRealtime(VTCompressionSessionRef& aSession, bool aEnabled) {
  // B-frames has been disabled in Init(), so no need to set it here.

  CFBooleanRef enabled = aEnabled ? kCFBooleanTrue : kCFBooleanFalse;
  OSStatus status = VTSessionSetProperty(
      aSession, kVTCompressionPropertyKey_RealTime, enabled);
  LOGD("%s real time, status: %d", aEnabled ? "Enable" : "Disable", status);
  if (status != noErr) {
    return false;
  }

  if (__builtin_available(macos 11.0, *)) {
    status = VTSessionSetProperty(
        aSession, kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality,
        enabled);
    LOGD("%s PrioritizeEncodingSpeedOverQuality, status: %d",
         aEnabled ? "Enable" : "Disable", status);
    if (status != noErr && status != kVTPropertyNotSupportedErr) {
      return false;
    }
  }

  int32_t maxFrameDelayCount = aEnabled ? 0 : kVTUnlimitedFrameDelayCount;
  AutoCFRelease<CFNumberRef> cf(CFNumberCreate(
      kCFAllocatorDefault, kCFNumberSInt32Type, &maxFrameDelayCount));
  status = VTSessionSetProperty(
      aSession, kVTCompressionPropertyKey_MaxFrameDelayCount, cf);
  LOGD("Set max frame delay count to %d, status: %d", maxFrameDelayCount,
       status);
  if (status != noErr && status != kVTPropertyNotSupportedErr) {
    return false;
  }

  return true;
}

static bool SetProfileLevel(VTCompressionSessionRef& aSession,
                            H264_PROFILE aValue) {
  CFStringRef profileLevel = nullptr;
  switch (aValue) {
    case H264_PROFILE::H264_PROFILE_BASE:
      profileLevel = kVTProfileLevel_H264_Baseline_AutoLevel;
      break;
    case H264_PROFILE::H264_PROFILE_MAIN:
      profileLevel = kVTProfileLevel_H264_Main_AutoLevel;
      break;
    case H264_PROFILE::H264_PROFILE_HIGH:
      profileLevel = kVTProfileLevel_H264_High_AutoLevel;
      break;
    default:
      LOGE("Profile %d not handled", static_cast<int>(aValue));
  }

  return profileLevel ? VTSessionSetProperty(
                            aSession, kVTCompressionPropertyKey_ProfileLevel,
                            profileLevel) == noErr
                      : false;
}

RefPtr<MediaDataEncoder::InitPromise> AppleVTEncoder::Init() {
  MOZ_ASSERT(!mInited, "Cannot initialize encoder again without shutting down");

  if (mConfig.mSize.width == 0 || mConfig.mSize.height == 0) {
    LOGE("width or height 0 in encoder init");
    return InitPromise::CreateAndReject(NS_ERROR_ILLEGAL_VALUE, __func__);
  }

  if (mConfig.mScalabilityMode != ScalabilityMode::None && !OSSupportsSVC()) {
    LOGE("SVC only supported on macOS 11.3 and more recent");
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                    "SVC only supported on macOS 11.3 and more recent"),
        __func__);
  }

  bool lowLatencyRateControl =
      mConfig.mUsage == Usage::Realtime ||
      mConfig.mScalabilityMode != ScalabilityMode::None;
  LOGD("low latency rate control: %s, Hardware allowed: %s",
       lowLatencyRateControl ? "yes" : "no",
       mHardwareNotAllowed ? "no" : "yes");
  AutoCFRelease<CFDictionaryRef> spec(
      BuildEncoderSpec(mHardwareNotAllowed, lowLatencyRateControl));
  AutoCFRelease<CFDictionaryRef> srcBufferAttr(
      BuildSourceImageBufferAttributes());
  if (!srcBufferAttr) {
    LOGE("Failed to create source buffer attr");
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                    "fail to create source buffer attributes"),
        __func__);
  }

  OSStatus status = VTCompressionSessionCreate(
      kCFAllocatorDefault, mConfig.mSize.width, mConfig.mSize.height,
      kCMVideoCodecType_H264, spec, srcBufferAttr, kCFAllocatorDefault,
      &FrameCallback, this /* outputCallbackRefCon */, &mSession);

  if (status != noErr) {
    LOGE("Failed to create compression session");
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    "fail to create encoder session"),
        __func__);
  }

  if (VTSessionSetProperty(mSession,
                           kVTCompressionPropertyKey_AllowFrameReordering,
                           kCFBooleanFalse) != noErr) {
    LOGE("Couldn't disable bframes");
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Couldn't disable bframes"),
        __func__);
  }

  if (mConfig.mUsage == Usage::Realtime && !SetRealtime(mSession, true)) {
    LOGE("fail to configure realtime properties");
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    "fail to configure real-time"),
        __func__);
  }

  if (mConfig.mBitrate) {
    if (mConfig.mCodec == CodecType::H264 &&
        mConfig.mBitrateMode == BitrateMode::Constant) {
      // Not supported, fall-back to VBR.
      LOGD("H264 CBR not supported in VideoToolbox, falling back to VBR");
      mConfig.mBitrateMode = BitrateMode::Variable;
    }
    bool rv =
        SetBitrateAndMode(mSession, mConfig.mBitrateMode, mConfig.mBitrate);
    if (!rv) {
      LOGE("failed to set bitrate to %d and mode to %s", mConfig.mBitrate,
           mConfig.mBitrateMode == BitrateMode::Constant ? "constant"
                                                         : "variable");
      return InitPromise::CreateAndReject(
          MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                      "fail to configurate bitrate"),
          __func__);
    }
  }

  if (mConfig.mScalabilityMode != ScalabilityMode::None) {
    if (__builtin_available(macos 11.3, *)) {
      float baseLayerFPSRatio = 1.0f;
      switch (mConfig.mScalabilityMode) {
        case ScalabilityMode::L1T2:
          baseLayerFPSRatio = 0.5;
          break;
        case ScalabilityMode::L1T3:
          // Not supported in hw on macOS, but is accepted and errors out when
          // encoding. Reject the configuration now.
          LOGE("macOS only supports L1T2 h264 SVC");
          return InitPromise::CreateAndReject(
              MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                          nsPrintfCString("macOS only support L1T2 h264 SVC")),
              __func__);
        default:
          MOZ_ASSERT_UNREACHABLE("Unhandled value");
      }
      AutoCFRelease<CFNumberRef> cf(CFNumberCreate(
          kCFAllocatorDefault, kCFNumberFloatType, &baseLayerFPSRatio));
      if (VTSessionSetProperty(
              mSession, kVTCompressionPropertyKey_BaseLayerFrameRateFraction,
              cf)) {
        LOGE("Failed to set base layer framerate fraction to %f",
             baseLayerFPSRatio);
        return InitPromise::CreateAndReject(
            MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                        nsPrintfCString("fail to configure SVC (base ratio: %f",
                                        baseLayerFPSRatio)),
            __func__);
      }
    } else {
      LOGE("MacOS version too old to enable SVC");
      return InitPromise::CreateAndReject(
          MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                      "macOS version too old to enable SVC"),
          __func__);
    }
  }

  int64_t interval =
      mConfig.mKeyframeInterval > std::numeric_limits<int64_t>::max()
          ? std::numeric_limits<int64_t>::max()
          : AssertedCast<int64_t>(mConfig.mKeyframeInterval);
  AutoCFRelease<CFNumberRef> cf(
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &interval));
  if (VTSessionSetProperty(mSession,
                           kVTCompressionPropertyKey_MaxKeyFrameInterval,
                           cf) != noErr) {
    LOGE("Failed to set max keyframe interval");
    return InitPromise::CreateAndReject(
        MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            nsPrintfCString("fail to configurate keyframe interval:%" PRId64,
                            interval)),
        __func__);
  }

  if (mConfig.mCodecSpecific) {
    const H264Specific& specific = mConfig.mCodecSpecific->as<H264Specific>();
    if (!SetProfileLevel(mSession, specific.mProfile)) {
      LOGE("Failed to set profile level");
      return InitPromise::CreateAndReject(
          MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                      nsPrintfCString("fail to configurate profile level:%d",
                                      int(specific.mProfile))),
          __func__);
    }
  }

  AutoCFRelease<CFBooleanRef> isUsingHW = nullptr;
  status = VTSessionCopyProperty(
      mSession, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,
      kCFAllocatorDefault, isUsingHW.receive());
  mIsHardwareAccelerated = status == noErr && isUsingHW == kCFBooleanTrue;
  LOGD("Using hw acceleration: %s", mIsHardwareAccelerated ? "yes" : "no");

  mError = NS_OK;
  return InitPromise::CreateAndResolve(true, __func__);
}

static Maybe<OSType> MapPixelFormat(dom::ImageBitmapFormat aFormat) {
  switch (aFormat) {
    case dom::ImageBitmapFormat::RGBA32:
      return Some(kCVPixelFormatType_32RGBA);
    case dom::ImageBitmapFormat::BGRA32:
      return Some(kCVPixelFormatType_32BGRA);
    case dom::ImageBitmapFormat::RGB24:
      return Some(kCVPixelFormatType_24RGB);
    case dom::ImageBitmapFormat::BGR24:
      return Some(kCVPixelFormatType_24BGR);
    case dom::ImageBitmapFormat::GRAY8:
      return Some(kCVPixelFormatType_OneComponent8);
    case dom::ImageBitmapFormat::YUV444P:
      return Some(kCVPixelFormatType_444YpCbCr8);
    case dom::ImageBitmapFormat::YUV420P:
      return Some(kCVPixelFormatType_420YpCbCr8PlanarFullRange);
    case dom::ImageBitmapFormat::YUV420SP_NV12:
      return Some(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange);
    default:
      return Nothing();
  }
}

CFDictionaryRef AppleVTEncoder::BuildSourceImageBufferAttributes() {
  Maybe<OSType> fmt = MapPixelFormat(mConfig.mSourcePixelFormat);
  if (fmt.isNothing()) {
    LOGE("unsupported source pixel format");
    return nullptr;
  }

  // Source image buffer attributes
  const void* keys[] = {kCVPixelBufferOpenGLCompatibilityKey,  // TODO
                        kCVPixelBufferIOSurfacePropertiesKey,  // TODO
                        kCVPixelBufferPixelFormatTypeKey};

  AutoCFRelease<CFDictionaryRef> ioSurfaceProps(CFDictionaryCreate(
      kCFAllocatorDefault, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks));
  AutoCFRelease<CFNumberRef> pixelFormat(
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &fmt));
  const void* values[] = {kCFBooleanTrue, ioSurfaceProps, pixelFormat};

  MOZ_ASSERT(std::size(keys) == std::size(values),
             "Non matching keys/values array size");

  return CFDictionaryCreate(kCFAllocatorDefault, keys, values, std::size(keys),
                            &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
}

static bool IsKeyframe(CMSampleBufferRef aSample) {
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(aSample, 0);
  if (attachments == nullptr || CFArrayGetCount(attachments) == 0) {
    return false;
  }

  return !CFDictionaryContainsKey(
      static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0)),
      kCMSampleAttachmentKey_NotSync);
}

static size_t GetNumParamSets(CMFormatDescriptionRef aDescription) {
  size_t numParamSets = 0;
  OSStatus status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
      aDescription, 0, nullptr, nullptr, &numParamSets, nullptr);
  if (status != noErr) {
    LOGE("Cannot get number of parameter sets from format description");
  }

  return numParamSets;
}

static const uint8_t kNALUStart[4] = {0, 0, 0, 1};

static size_t GetParamSet(CMFormatDescriptionRef aDescription, size_t aIndex,
                          const uint8_t** aDataPtr) {
  size_t length = 0;
  int headerSize = 0;
  if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
          aDescription, aIndex, aDataPtr, &length, nullptr, &headerSize) !=
      noErr) {
    LOGE("failed to get parameter set from format description");
    return 0;
  }
  MOZ_ASSERT(headerSize == sizeof(kNALUStart), "Only support 4 byte header");

  return length;
}

static bool WriteSPSPPS(MediaRawData* aDst,
                        CMFormatDescriptionRef aDescription) {
  // Get SPS/PPS
  const size_t numParamSets = GetNumParamSets(aDescription);
  UniquePtr<MediaRawDataWriter> writer(aDst->CreateWriter());
  for (size_t i = 0; i < numParamSets; i++) {
    const uint8_t* data = nullptr;
    size_t length = GetParamSet(aDescription, i, &data);
    if (length == 0) {
      return false;
    }
    if (!writer->Append(kNALUStart, sizeof(kNALUStart))) {
      LOGE("Cannot write NAL unit start code");
      return false;
    }
    if (!writer->Append(data, length)) {
      LOGE("Cannot write parameter set");
      return false;
    }
  }
  return true;
}

static RefPtr<MediaByteBuffer> extractAvcc(
    CMFormatDescriptionRef aDescription) {
  CFPropertyListRef list = CMFormatDescriptionGetExtension(
      aDescription,
      kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
  if (!list) {
    LOGE("fail to get atoms");
    return nullptr;
  }
  CFDataRef avcC = static_cast<CFDataRef>(
      CFDictionaryGetValue(static_cast<CFDictionaryRef>(list), CFSTR("avcC")));
  if (!avcC) {
    LOGE("fail to extract avcC");
    return nullptr;
  }
  CFIndex length = CFDataGetLength(avcC);
  const UInt8* bytes = CFDataGetBytePtr(avcC);
  if (length <= 0 || !bytes) {
    LOGE("empty avcC");
    return nullptr;
  }

  RefPtr<MediaByteBuffer> config = new MediaByteBuffer(length);
  config->AppendElements(bytes, length);
  return config;
}

bool AppleVTEncoder::WriteExtraData(MediaRawData* aDst, CMSampleBufferRef aSrc,
                                    const bool aAsAnnexB) {
  if (!IsKeyframe(aSrc)) {
    return true;
  }

  aDst->mKeyframe = true;
  CMFormatDescriptionRef desc = CMSampleBufferGetFormatDescription(aSrc);
  if (!desc) {
    LOGE("fail to get format description from sample");
    return false;
  }

  if (aAsAnnexB) {
    return WriteSPSPPS(aDst, desc);
  }

  RefPtr<MediaByteBuffer> avcc = extractAvcc(desc);
  if (!avcc) {
    return false;
  }

  if (!mAvcc || !H264::CompareExtraData(avcc, mAvcc)) {
    mAvcc = avcc;
    aDst->mExtraData = mAvcc;
  }

  return avcc != nullptr;
}

static bool WriteNALUs(MediaRawData* aDst, CMSampleBufferRef aSrc,
                       bool aAsAnnexB = false) {
  size_t srcRemaining = CMSampleBufferGetTotalSampleSize(aSrc);
  CMBlockBufferRef block = CMSampleBufferGetDataBuffer(aSrc);
  if (!block) {
    LOGE("Cannot get block buffer frome sample");
    return false;
  }
  UniquePtr<MediaRawDataWriter> writer(aDst->CreateWriter());
  size_t writtenLength = aDst->Size();
  // Ensure capacity.
  if (!writer->SetSize(writtenLength + srcRemaining)) {
    LOGE("Cannot allocate buffer");
    return false;
  }
  size_t readLength = 0;
  while (srcRemaining > 0) {
    // Extract the size of next NAL unit
    uint8_t unitSizeBytes[4];
    MOZ_ASSERT(srcRemaining > sizeof(unitSizeBytes));
    if (CMBlockBufferCopyDataBytes(block, readLength, sizeof(unitSizeBytes),
                                   reinterpret_cast<uint32_t*>(
                                       unitSizeBytes)) != kCMBlockBufferNoErr) {
      LOGE("Cannot copy unit size bytes");
      return false;
    }
    size_t unitSize =
        CFSwapInt32BigToHost(*reinterpret_cast<uint32_t*>(unitSizeBytes));

    if (aAsAnnexB) {
      // Replace unit size bytes with NALU start code.
      PodCopy(writer->Data() + writtenLength, kNALUStart, sizeof(kNALUStart));
      readLength += sizeof(unitSizeBytes);
      srcRemaining -= sizeof(unitSizeBytes);
      writtenLength += sizeof(kNALUStart);
    } else {
      // Copy unit size bytes + data.
      unitSize += sizeof(unitSizeBytes);
    }
    MOZ_ASSERT(writtenLength + unitSize <= aDst->Size());
    // Copy NAL unit data
    if (CMBlockBufferCopyDataBytes(block, readLength, unitSize,
                                   writer->Data() + writtenLength) !=
        kCMBlockBufferNoErr) {
      LOGE("Cannot copy unit data");
      return false;
    }
    readLength += unitSize;
    srcRemaining -= unitSize;
    writtenLength += unitSize;
  }
  MOZ_ASSERT(writtenLength == aDst->Size());
  return true;
}

void AppleVTEncoder::OutputFrame(OSStatus aStatus, VTEncodeInfoFlags aFlags,
                                 CMSampleBufferRef aBuffer) {
  LOGV("status: %d, flags: %d, buffer %p", aStatus, aFlags, aBuffer);

  if (aStatus != noErr) {
    ProcessOutput(nullptr, EncodeResult::EncodeError);
    return;
  }

  if (aFlags & kVTEncodeInfo_FrameDropped) {
    ProcessOutput(nullptr, EncodeResult::FrameDropped);
    return;
  }

  if (!aBuffer) {
    ProcessOutput(nullptr, EncodeResult::EmptyBuffer);
    return;
  }

  RefPtr<MediaRawData> output(new MediaRawData());

  if (__builtin_available(macos 11.3, *)) {
    if (mConfig.mScalabilityMode != ScalabilityMode::None) {
      CFDictionaryRef dict = (CFDictionaryRef)(CFArrayGetValueAtIndex(
          CMSampleBufferGetSampleAttachmentsArray(aBuffer, true), 0));
      CFBooleanRef isBaseLayerRef = (CFBooleanRef)CFDictionaryGetValue(
          dict, (const void*)kCMSampleAttachmentKey_IsDependedOnByOthers);
      Boolean isBaseLayer = CFBooleanGetValue(isBaseLayerRef);
      output->mTemporalLayerId.emplace(isBaseLayer ? 0 : 1);
    }
  }

  bool forceAvcc = false;
  if (mConfig.mCodecSpecific->is<H264Specific>()) {
    forceAvcc = mConfig.mCodecSpecific->as<H264Specific>().mFormat ==
                H264BitStreamFormat::AVC;
  }
  bool asAnnexB = !forceAvcc;
  bool succeeded = WriteExtraData(output, aBuffer, asAnnexB) &&
                   WriteNALUs(output, aBuffer, asAnnexB);

  output->mTime = media::TimeUnit::FromSeconds(
      CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(aBuffer)));
  output->mDuration = media::TimeUnit::FromSeconds(
      CMTimeGetSeconds(CMSampleBufferGetOutputDuration(aBuffer)));
  LOGV("Make a %s output[time: %s, duration: %s]: %s",
       asAnnexB ? "AnnexB" : "AVCC", output->mTime.ToString().get(),
       output->mDuration.ToString().get(), succeeded ? "succeed" : "failed");
  ProcessOutput(succeeded ? std::move(output) : nullptr, EncodeResult::Success);
}

void AppleVTEncoder::ProcessOutput(RefPtr<MediaRawData>&& aOutput,
                                   EncodeResult aResult) {
  if (!mTaskQueue->IsCurrentThreadIn()) {
    LOGV("Dispatch ProcessOutput to task queue");
    nsresult rv = mTaskQueue->Dispatch(
        NewRunnableMethod<RefPtr<MediaRawData>, EncodeResult>(
            "AppleVTEncoder::ProcessOutput", this,
            &AppleVTEncoder::ProcessOutput, std::move(aOutput), aResult));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    Unused << rv;
    return;
  }

  if (aResult != EncodeResult::Success) {
    switch (aResult) {
      case EncodeResult::EncodeError:
        mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Failed to encode");
        break;
      case EncodeResult::EmptyBuffer:
        mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Buffer is empty");
        break;
      case EncodeResult::FrameDropped:
        if (mConfig.mUsage == Usage::Realtime) {
          // Dropping a frame in real-time usage is okay.
          LOGW("Frame is dropped");
        } else {
          // Some usages like transcoding should not drop a frame.
          LOGE("Frame is dropped");
          mError =
              MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Frame is dropped");
        }
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unknown EncodeResult");
        break;
    }
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  LOGV("Got %zu bytes of output", !aOutput.get() ? 0 : aOutput->Size());

  if (!aOutput) {
    mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "No converted output");
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  mEncodedData.AppendElement(std::move(aOutput));
  MaybeResolveOrRejectEncodePromise();
}

RefPtr<MediaDataEncoder::EncodePromise> AppleVTEncoder::Encode(
    const MediaData* aSample) {
  MOZ_ASSERT(aSample != nullptr);

  RefPtr<const VideoData> sample(aSample->As<const VideoData>());

  RefPtr<AppleVTEncoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self, this, sample] {
    MOZ_ASSERT(mEncodePromise.IsEmpty(),
               "Encode should not be called again before getting results");
    RefPtr<EncodePromise> p = mEncodePromise.Ensure(__func__);
    ProcessEncode(sample);
    return p;
  });
}

RefPtr<MediaDataEncoder::ReconfigurationPromise> AppleVTEncoder::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &AppleVTEncoder::ProcessReconfigure,
                     aConfigurationChanges);
}

void AppleVTEncoder::ProcessEncode(const RefPtr<const VideoData>& aSample) {
  LOGV("::ProcessEncode");
  AssertOnTaskQueue();
  MOZ_ASSERT(mSession);

  if (NS_FAILED(mError)) {
    LOGE("Pending error: %s", mError.Description().get());
    MaybeResolveOrRejectEncodePromise();
  }

  AutoCVBufferRelease<CVImageBufferRef> buffer(
      CreateCVPixelBuffer(aSample->mImage));
  if (!buffer) {
    LOGE("Failed to allocate buffer");
    mError = MediaResult(NS_ERROR_OUT_OF_MEMORY, "failed to allocate buffer");
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  CFDictionaryRef frameProps = nullptr;
  if (aSample->mKeyframe) {
    CFTypeRef keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
    CFTypeRef values[] = {kCFBooleanTrue};
    MOZ_ASSERT(std::size(keys) == std::size(values));
    frameProps = CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, std::size(keys),
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  };

  VTEncodeInfoFlags info;
  OSStatus status = VTCompressionSessionEncodeFrame(
      mSession, buffer,
      CMTimeMake(aSample->mTime.ToMicroseconds(), USECS_PER_S),
      CMTimeMake(aSample->mDuration.ToMicroseconds(), USECS_PER_S), frameProps,
      nullptr /* sourceFrameRefcon */, &info);
  if (status != noErr) {
    LOGE("VTCompressionSessionEncodeFrame error: %d", status);
    mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         "VTCompressionSessionEncodeFrame error");
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  if (mConfig.mUsage != Usage::Realtime) {
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  // The latency between encoding a sample and receiving the encoded output is
  // critical in real-time usage. To minimize the latency, the output result
  // should be returned immediately once they are ready, instead of being
  // returned in the next or later Encode() iterations.
  LOGV("Encoding in progress");

  // Workaround for real-time encoding in OS versions < 11.
  ForceOutputIfNeeded();
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
AppleVTEncoder::ProcessReconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  bool ok = false;
  for (const auto& confChange : aConfigurationChanges->mChanges) {
    // A reconfiguration on the fly succeeds if all changes can be applied
    // successfuly. In case of failure, the encoder will be drained and
    // recreated.
    ok &= confChange.match(
        // Not supported yet
        [&](const DimensionsChange& aChange) -> bool { return false; },
        [&](const DisplayDimensionsChange& aChange) -> bool { return false; },
        [&](const BitrateModeChange& aChange) -> bool {
          mConfig.mBitrateMode = aChange.get();
          return SetBitrateAndMode(mSession, mConfig.mBitrateMode,
                                   mConfig.mBitrate);
        },
        [&](const BitrateChange& aChange) -> bool {
          mConfig.mBitrate = aChange.get().refOr(0);
          // 0 is the default in AppleVTEncoder: the encoder chooses the bitrate
          // based on the content.
          return SetBitrateAndMode(mSession, mConfig.mBitrateMode,
                                   mConfig.mBitrate);
        },
        [&](const FramerateChange& aChange) -> bool {
          // 0 means default, in VideoToolbox, and is valid, perform some light
          // sanitation on other values.
          double fps = aChange.get().refOr(0);
          if (std::isnan(fps) || fps < 0 ||
              int64_t(fps) > std::numeric_limits<int32_t>::max()) {
            LOGE("Invalid fps of %lf", fps);
            return false;
          }
          return SetFrameRate(mSession, AssertedCast<int64_t>(fps));
        },
        [&](const UsageChange& aChange) -> bool {
          mConfig.mUsage = aChange.get();
          return SetRealtime(mSession, aChange.get() == Usage::Realtime);
        },
        [&](const ContentHintChange& aChange) -> bool { return false; },
        [&](const SampleRateChange& aChange) -> bool { return false; },
        [&](const NumberOfChannelsChange& aChange) -> bool { return false; });
  };
  using P = MediaDataEncoder::ReconfigurationPromise;
  if (ok) {
    return P::CreateAndResolve(true, __func__);
  }
  return P::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
}

static size_t NumberOfPlanes(dom::ImageBitmapFormat aPixelFormat) {
  switch (aPixelFormat) {
    case dom::ImageBitmapFormat::RGBA32:
    case dom::ImageBitmapFormat::BGRA32:
    case dom::ImageBitmapFormat::RGB24:
    case dom::ImageBitmapFormat::BGR24:
    case dom::ImageBitmapFormat::GRAY8:
      return 1;
    case dom::ImageBitmapFormat::YUV444P:
    case dom::ImageBitmapFormat::YUV420P:
      return 3;
    case dom::ImageBitmapFormat::YUV420SP_NV12:
      return 2;
    default:
      LOGE("Unsupported input pixel format");
      return 0;
  }
}

using namespace layers;

static void ReleaseSurface(void* aReleaseRef, const void* aBaseAddress) {
  RefPtr<gfx::DataSourceSurface> released =
      dont_AddRef(static_cast<gfx::DataSourceSurface*>(aReleaseRef));
}

static void ReleaseImage(void* aImageGrip, const void* aDataPtr,
                         size_t aDataSize, size_t aNumOfPlanes,
                         const void** aPlanes) {
  (static_cast<PlanarYCbCrImage*>(aImageGrip))->Release();
}

CVPixelBufferRef AppleVTEncoder::CreateCVPixelBuffer(Image* aSource) {
  AssertOnTaskQueue();

  if (aSource->GetFormat() == ImageFormat::PLANAR_YCBCR) {
    PlanarYCbCrImage* image = aSource->AsPlanarYCbCrImage();
    if (!image || !image->GetData()) {
      return nullptr;
    }

    OSType format = MapPixelFormat(mConfig.mSourcePixelFormat).ref();
    size_t numPlanes = NumberOfPlanes(mConfig.mSourcePixelFormat);
    const PlanarYCbCrImage::Data* yuv = image->GetData();
    if (!yuv) {
      return nullptr;
    }
    auto ySize = yuv->YDataSize();
    auto cbcrSize = yuv->CbCrDataSize();
    void* addresses[3] = {};
    size_t widths[3] = {};
    size_t heights[3] = {};
    size_t strides[3] = {};
    switch (numPlanes) {
      case 3:
        addresses[2] = yuv->mCrChannel;
        widths[2] = cbcrSize.width;
        heights[2] = cbcrSize.height;
        strides[2] = yuv->mCbCrStride;
        [[fallthrough]];
      case 2:
        addresses[1] = yuv->mCbChannel;
        widths[1] = cbcrSize.width;
        heights[1] = cbcrSize.height;
        strides[1] = yuv->mCbCrStride;
        [[fallthrough]];
      case 1:
        addresses[0] = yuv->mYChannel;
        widths[0] = ySize.width;
        heights[0] = ySize.height;
        strides[0] = yuv->mYStride;
        break;
      default:
        return nullptr;
    }

    CVPixelBufferRef buffer = nullptr;
    image->AddRef();  // Grip input buffers.
    CVReturn rv = CVPixelBufferCreateWithPlanarBytes(
        kCFAllocatorDefault, yuv->mPictureRect.width, yuv->mPictureRect.height,
        format, nullptr /* dataPtr */, 0 /* dataSize */, numPlanes, addresses,
        widths, heights, strides, ReleaseImage /* releaseCallback */,
        image /* releaseRefCon */, nullptr /* pixelBufferAttributes */,
        &buffer);
    if (rv == kCVReturnSuccess) {
      return buffer;
      // |image| will be released in |ReleaseImage()|.
    }
    LOGE("CVPIxelBufferCreateWithPlanarBytes error");
    image->Release();
    return nullptr;
  }

  RefPtr<gfx::SourceSurface> surface = aSource->GetAsSourceSurface();
  if (!surface) {
    LOGE("Failed to get SourceSurface");
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (!dataSurface) {
    LOGE("Failed to get DataSurface");
    return nullptr;
  }

  gfx::DataSourceSurface::ScopedMap map(dataSurface,
                                        gfx::DataSourceSurface::READ);
  if (NS_WARN_IF(!map.IsMapped())) {
    LOGE("Failed to map DataSurface");
    return nullptr;
  }

  const dom::ImageUtils imageUtils(aSource);
  Maybe<dom::ImageBitmapFormat> format = imageUtils.GetFormat();
  if (format.isNothing()) {
    LOGE("Image conversion not implemented in AppleVTEncoder");
    return nullptr;
  }

  if (format.ref() != mConfig.mSourcePixelFormat) {
    LOGV("Encode image in %s format, even though config's source format is %s",
         dom::GetEnumString(format.ref()).get(),
         dom::GetEnumString(mConfig.mSourcePixelFormat).get());
  }

  Maybe<OSType> imgFormat = MapPixelFormat(format.ref());
  if (imgFormat.isNothing()) {
    LOGE("Failed to get kCVPixelFormatType");
    return nullptr;
  }

  CVPixelBufferRef buffer = nullptr;
  gfx::DataSourceSurface* dss = dataSurface.forget().take();
  CVReturn rv = CVPixelBufferCreateWithBytes(
      kCFAllocatorDefault, dss->GetSize().Width(), dss->GetSize().Height(),
      imgFormat.value(), map.GetData(), map.GetStride(), ReleaseSurface, dss,
      nullptr, &buffer);
  if (rv == kCVReturnSuccess) {
    return buffer;
    // |dss| will be released in |ReleaseSurface()|.
  }
  LOGE("CVPIxelBufferCreateWithBytes error: %d", rv);
  RefPtr<gfx::DataSourceSurface> released = dont_AddRef(dss);
  return nullptr;
}

RefPtr<MediaDataEncoder::EncodePromise> AppleVTEncoder::Drain() {
  return InvokeAsync(mTaskQueue, this, __func__, &AppleVTEncoder::ProcessDrain);
}

RefPtr<MediaDataEncoder::EncodePromise> AppleVTEncoder::ProcessDrain() {
  LOGV("::ProcessDrain");
  AssertOnTaskQueue();
  MOZ_ASSERT(mSession);

  OSStatus status =
      VTCompressionSessionCompleteFrames(mSession, kCMTimeIndefinite);
  if (status != noErr) {
    LOGE("VTCompressionSessionCompleteFrames error");
    return EncodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
  }

  // Resolve the pending encode promise if any.
  MaybeResolveOrRejectEncodePromise();

  // VTCompressionSessionCompleteFrames() could have queued multiple tasks with
  // the new drained frames. Dispatch a task after them to resolve the promise
  // with those frames.
  RefPtr<AppleVTEncoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    EncodedData pendingFrames(std::move(self->mEncodedData));
    LOGV("Resolve drain promise with %zu encoded outputs",
         pendingFrames.Length());
    self->mEncodedData = EncodedData();
    return EncodePromise::CreateAndResolve(std::move(pendingFrames), __func__);
  });
}

RefPtr<ShutdownPromise> AppleVTEncoder::Shutdown() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &AppleVTEncoder::ProcessShutdown);
}

RefPtr<ShutdownPromise> AppleVTEncoder::ProcessShutdown() {
  LOGD("::ProcessShutdown");
  AssertOnTaskQueue();
  if (mSession) {
    VTCompressionSessionInvalidate(mSession);
    CFRelease(mSession);
    mSession = nullptr;
    mInited = false;
  }

  mError = MediaResult(NS_ERROR_DOM_MEDIA_CANCELED, "Canceled in shutdown");
  MaybeResolveOrRejectEncodePromise();
  mError = NS_OK;

  return ShutdownPromise::CreateAndResolve(true, __func__);
}

RefPtr<GenericPromise> AppleVTEncoder::SetBitrate(uint32_t aBitsPerSec) {
  RefPtr<AppleVTEncoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self, aBitsPerSec]() {
    MOZ_ASSERT(self->mSession);
    bool rv = SetBitrateAndMode(self->mSession, self->mConfig.mBitrateMode,
                                aBitsPerSec);
    return rv ? GenericPromise::CreateAndResolve(true, __func__)
              : GenericPromise::CreateAndReject(
                    NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR, __func__);
  });
}

void AppleVTEncoder::MaybeResolveOrRejectEncodePromise() {
  AssertOnTaskQueue();

  if (mEncodePromise.IsEmpty()) {
    LOGV(
        "No pending promise to resolve(pending outputs: %zu) or reject(err: "
        "%s)",
        mEncodedData.Length(), mError.Description().get());
    return;
  }

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }

  if (NS_FAILED(mError.Code())) {
    LOGE("Rejecting encode promise with error: %s", mError.Description().get());
    mEncodePromise.Reject(mError, __func__);
    return;
  }

  LOGV("Resolving with %zu encoded outputs", mEncodedData.Length());
  mEncodePromise.Resolve(std::move(mEncodedData), __func__);
}

void AppleVTEncoder::ForceOutputIfNeeded() {
  if (__builtin_available(macos 11.0, *)) {
    return;
  }
  // Ideally, OutputFrame (called via FrameCallback) should resolve the encode
  // promise. However, sometimes output is produced only after multiple
  // inputs. To ensure continuous encoding, we force the encoder to produce a
  // potentially empty output if no result is received in 50 ms.
  RefPtr<AppleVTEncoder> self = this;
  auto r = NS_NewTimerWithCallback(
      [self](nsITimer* aTimer) {
        if (!self->mSession) {
          LOGV("Do nothing since the encoder has been shut down");
          return;
        }

        LOGV("Resolving the pending promise");
        self->MaybeResolveOrRejectEncodePromise();
      },
      TimeDuration::FromMilliseconds(50), nsITimer::TYPE_ONE_SHOT,
      "EncodingProgressChecker", mTaskQueue);
  if (r.isErr()) {
    LOGE(
        "Failed to set an encoding progress checker. Resolve the pending "
        "promise now");
    MaybeResolveOrRejectEncodePromise();
    return;
  }
  mTimer = r.unwrap();
}

#undef LOGE
#undef LOGW
#undef LOGD
#undef LOGV

}  // namespace mozilla
