/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "KeySystemConfig.h"

#include "EMEUtils.h"
#include "GMPUtils.h"
#include "KeySystemNames.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/StaticPrefs_media.h"
#include "nsPrintfCString.h"

#ifdef XP_WIN
#  include "PDMFactory.h"
#  include "WMFDecoderModule.h"
#endif
#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidDecoderModule.h"
#  include "mozilla/java/MediaDrmProxyWrappers.h"
#  include "nsMimeTypes.h"
#endif

#ifdef MOZ_WMF_CDM
#  include "mediafoundation/WMFCDMImpl.h"
#endif

namespace mozilla {

/* static */
bool KeySystemConfig::Supports(const nsAString& aKeySystem) {
#ifdef MOZ_WIDGET_ANDROID
  // No GMP on Android, check if we can use MediaDrm for this keysystem.
  if (mozilla::java::MediaDrmProxy::IsSchemeSupported(
          NS_ConvertUTF16toUTF8(aKeySystem))) {
    return true;
  }
#else
#  ifdef MOZ_WMF_CDM
  // Test only, pretend we have already installed CDMs.
  if (StaticPrefs::media_eme_wmf_use_mock_cdm_for_external_cdms()) {
    return true;
  }
#  endif
  // Check if Widevine L3 or Clearkey has been downloaded via GMP downloader.
  if (IsWidevineKeySystem(aKeySystem) || IsClearkeyKeySystem(aKeySystem)) {
    return HaveGMPFor(nsCString(CHROMIUM_CDM_API),
                      {NS_ConvertUTF16toUTF8(aKeySystem)});
  }
#endif

#if MOZ_WMF_CDM
  // Check if Widevine L1 has been downloaded via GMP downloader.
  if (IsWidevineExperimentKeySystemAndSupported(aKeySystem)) {
    return HaveGMPFor(nsCString(kWidevineExperimentAPIName),
                      {nsCString(kWidevineExperimentKeySystemName)});
  }

  // PlayReady and WMF-based ClearKey are always installed, we don't need to
  // download them.
  if (IsPlayReadyKeySystemAndSupported(aKeySystem) ||
      IsWMFClearKeySystemAndSupported(aKeySystem)) {
    return true;
  }
#endif

  return false;
}

/* static */ void KeySystemConfig::CreateClearKeyKeySystemConfigs(
    const KeySystemConfigRequest& aRequest,
    nsTArray<KeySystemConfig>& aOutConfigs) {
  KeySystemConfig* config = aOutConfigs.AppendElement();
  config->mKeySystem = aRequest.mKeySystem;
  config->mInitDataTypes.AppendElement(u"cenc"_ns);
  config->mInitDataTypes.AppendElement(u"keyids"_ns);
  config->mInitDataTypes.AppendElement(u"webm"_ns);
  config->mPersistentState = Requirement::Optional;
  config->mDistinctiveIdentifier = Requirement::NotAllowed;
  config->mSessionTypes.AppendElement(SessionType::Temporary);
  if (StaticPrefs::media_clearkey_persistent_license_enabled()) {
    config->mSessionTypes.AppendElement(SessionType::PersistentLicense);
  }
#if defined(XP_WIN)
  // Clearkey CDM uses WMF's H.264 decoder on Windows.
  auto pdmFactory = MakeRefPtr<PDMFactory>();
  if (!pdmFactory->SupportsMimeType("video/avc"_ns).isEmpty()) {
    config->mMP4.SetCanDecryptAndDecode(EME_CODEC_H264);
  } else {
    config->mMP4.SetCanDecrypt(EME_CODEC_H264);
  }
#else
  config->mMP4.SetCanDecrypt(EME_CODEC_H264);
#endif
  config->mMP4.SetCanDecrypt(EME_CODEC_AAC);
  config->mMP4.SetCanDecrypt(EME_CODEC_FLAC);
  config->mMP4.SetCanDecrypt(EME_CODEC_OPUS);
  config->mMP4.SetCanDecrypt(EME_CODEC_VP9);
#ifdef MOZ_AV1
  config->mMP4.SetCanDecrypt(EME_CODEC_AV1);
#endif
  config->mWebM.SetCanDecrypt(EME_CODEC_VORBIS);
  config->mWebM.SetCanDecrypt(EME_CODEC_OPUS);
  config->mWebM.SetCanDecrypt(EME_CODEC_VP8);
  config->mWebM.SetCanDecrypt(EME_CODEC_VP9);
#ifdef MOZ_AV1
  config->mWebM.SetCanDecrypt(EME_CODEC_AV1);
#endif

  if (StaticPrefs::media_clearkey_test_key_systems_enabled()) {
    // Add testing key systems. These offer the same capabilities as the
    // base clearkey system, so just clone clearkey and change the name.
    KeySystemConfig clearkeyWithProtectionQuery{*config};
    clearkeyWithProtectionQuery.mKeySystem.AssignLiteral(
        kClearKeyWithProtectionQueryKeySystemName);
    aOutConfigs.AppendElement(std::move(clearkeyWithProtectionQuery));
  }
}

/* static */ void KeySystemConfig::CreateWivineL3KeySystemConfigs(
    const KeySystemConfigRequest& aRequest,
    nsTArray<KeySystemConfig>& aOutConfigs) {
  KeySystemConfig* config = aOutConfigs.AppendElement();
  config->mKeySystem = aRequest.mKeySystem;
  config->mInitDataTypes.AppendElement(u"cenc"_ns);
  config->mInitDataTypes.AppendElement(u"keyids"_ns);
  config->mInitDataTypes.AppendElement(u"webm"_ns);
  config->mPersistentState = Requirement::Optional;
  config->mDistinctiveIdentifier = Requirement::NotAllowed;
  config->mSessionTypes.AppendElement(SessionType::Temporary);
#ifdef MOZ_WIDGET_ANDROID
  config->mSessionTypes.AppendElement(SessionType::PersistentLicense);
#endif
  config->mAudioRobustness.AppendElement(u"SW_SECURE_CRYPTO"_ns);
  config->mVideoRobustness.AppendElement(u"SW_SECURE_CRYPTO"_ns);
  config->mVideoRobustness.AppendElement(u"SW_SECURE_DECODE"_ns);

#if defined(MOZ_WIDGET_ANDROID)
  // MediaDrm.isCryptoSchemeSupported only allows passing
  // "video/mp4" or "video/webm" for mimetype string.
  // See
  // https://developer.android.com/reference/android/media/MediaDrm.html#isCryptoSchemeSupported(java.util.UUID,
  // java.lang.String) for more detail.
  typedef struct {
    const nsCString& mMimeType;
    const nsCString& mEMECodecType;
    const char16_t* mCodecType;
    KeySystemConfig::ContainerSupport* mSupportType;
  } DataForValidation;

  DataForValidation validationList[] = {
      {nsCString(VIDEO_MP4), EME_CODEC_H264, java::MediaDrmProxy::AVC,
       &config->mMP4},
      {nsCString(VIDEO_MP4), EME_CODEC_VP9, java::MediaDrmProxy::AVC,
       &config->mMP4},
#  ifdef MOZ_AV1
      {nsCString(VIDEO_MP4), EME_CODEC_AV1, java::MediaDrmProxy::AV1,
       &config->mMP4},
#  endif
      {nsCString(AUDIO_MP4), EME_CODEC_AAC, java::MediaDrmProxy::AAC,
       &config->mMP4},
      {nsCString(AUDIO_MP4), EME_CODEC_FLAC, java::MediaDrmProxy::FLAC,
       &config->mMP4},
      {nsCString(AUDIO_MP4), EME_CODEC_OPUS, java::MediaDrmProxy::OPUS,
       &config->mMP4},
      {nsCString(VIDEO_WEBM), EME_CODEC_VP8, java::MediaDrmProxy::VP8,
       &config->mWebM},
      {nsCString(VIDEO_WEBM), EME_CODEC_VP9, java::MediaDrmProxy::VP9,
       &config->mWebM},
#  ifdef MOZ_AV1
      {nsCString(VIDEO_WEBM), EME_CODEC_AV1, java::MediaDrmProxy::AV1,
       &config->mWebM},
#  endif
      {nsCString(AUDIO_WEBM), EME_CODEC_VORBIS, java::MediaDrmProxy::VORBIS,
       &config->mWebM},
      {nsCString(AUDIO_WEBM), EME_CODEC_OPUS, java::MediaDrmProxy::OPUS,
       &config->mWebM},
  };

  for (const auto& data : validationList) {
    if (java::MediaDrmProxy::IsCryptoSchemeSupported(kWidevineKeySystemName,
                                                     data.mMimeType)) {
      if (!AndroidDecoderModule::SupportsMimeType(data.mMimeType).isEmpty()) {
        data.mSupportType->SetCanDecryptAndDecode(data.mEMECodecType);
      } else {
        data.mSupportType->SetCanDecrypt(data.mEMECodecType);
      }
    }
  }
#else
#  if defined(XP_WIN)
  // Widevine CDM doesn't include an AAC decoder. So if WMF can't
  // decode AAC, and a codec wasn't specified, be conservative
  // and reject the MediaKeys request, since we assume Widevine
  // will be used with AAC.
  auto pdmFactory = MakeRefPtr<PDMFactory>();
  if (!pdmFactory->SupportsMimeType("audio/mp4a-latm"_ns).isEmpty()) {
    config->mMP4.SetCanDecrypt(EME_CODEC_AAC);
  }
#  else
  config->mMP4.SetCanDecrypt(EME_CODEC_AAC);
#  endif
  config->mMP4.SetCanDecrypt(EME_CODEC_FLAC);
  config->mMP4.SetCanDecrypt(EME_CODEC_OPUS);
  config->mMP4.SetCanDecryptAndDecode(EME_CODEC_H264);
  config->mMP4.SetCanDecryptAndDecode(EME_CODEC_VP9);
#  ifdef MOZ_AV1
  config->mMP4.SetCanDecryptAndDecode(EME_CODEC_AV1);
#  endif
  config->mWebM.SetCanDecrypt(EME_CODEC_VORBIS);
  config->mWebM.SetCanDecrypt(EME_CODEC_OPUS);
  config->mWebM.SetCanDecryptAndDecode(EME_CODEC_VP8);
  config->mWebM.SetCanDecryptAndDecode(EME_CODEC_VP9);
#  ifdef MOZ_AV1
  config->mWebM.SetCanDecryptAndDecode(EME_CODEC_AV1);
#  endif
#endif
}

/* static */
RefPtr<KeySystemConfig::SupportedConfigsPromise>
KeySystemConfig::CreateKeySystemConfigs(
    const nsTArray<KeySystemConfigRequest>& aRequests) {
  // Create available configs for all supported key systems in the request, but
  // some of them might not be created immediately.

  nsTArray<KeySystemConfig> outConfigs;
  nsTArray<KeySystemConfigRequest> asyncRequests;

  for (const auto& request : aRequests) {
    const nsAString& keySystem = request.mKeySystem;
    if (!Supports(keySystem)) {
      continue;
    }

    if (IsClearkeyKeySystem(keySystem)) {
      CreateClearKeyKeySystemConfigs(request, outConfigs);
    } else if (IsWidevineKeySystem(keySystem)) {
      CreateWivineL3KeySystemConfigs(request, outConfigs);
    }
#ifdef MOZ_WMF_CDM
    else if (IsPlayReadyKeySystemAndSupported(keySystem) ||
             IsWidevineExperimentKeySystemAndSupported(keySystem)) {
      asyncRequests.AppendElement(request);
    }
#endif
  }

#ifdef MOZ_WMF_CDM
  if (!asyncRequests.IsEmpty()) {
    RefPtr<SupportedConfigsPromise::Private> promise =
        new SupportedConfigsPromise::Private(__func__);
    RefPtr<WMFCDMCapabilites> cdm = new WMFCDMCapabilites();
    cdm->GetCapabilities(asyncRequests)
        ->Then(GetMainThreadSerialEventTarget(), __func__,
               [syncConfigs = std::move(outConfigs),
                promise](SupportedConfigsPromise::ResolveOrRejectValue&&
                             aResult) mutable {
                 // Return the capabilities we already know
                 if (aResult.IsReject()) {
                   promise->Resolve(std::move(syncConfigs), __func__);
                   return;
                 }
                 // Merge sync results with async results
                 auto& asyncConfigs = aResult.ResolveValue();
                 asyncConfigs.AppendElements(std::move(syncConfigs));
                 promise->Resolve(std::move(asyncConfigs), __func__);
               });
    return promise;
  }
#endif
  return SupportedConfigsPromise::CreateAndResolve(std::move(outConfigs),
                                                   __func__);
}

/* static */
void KeySystemConfig::GetGMPKeySystemConfigs(dom::Promise* aPromise) {
  MOZ_ASSERT(aPromise);

  // Generate config requests
  const nsTArray<nsString> keySystemNames{
      NS_ConvertUTF8toUTF16(kClearKeyKeySystemName),
      NS_ConvertUTF8toUTF16(kWidevineKeySystemName),
  };
  nsTArray<KeySystemConfigRequest> requests;
  for (const auto& keySystem : keySystemNames) {
#ifdef MOZ_WMF_CDM
    if (IsWMFClearKeySystemAndSupported(keySystem)) {
      // Using wmf clearkey, not gmp clearkey.
      continue;
    }
#endif
    requests.AppendElement(KeySystemConfigRequest{
        keySystem, DecryptionInfo::Software, false /* IsPrivateBrowsing */});
  }

  // Get supported configs
  KeySystemConfig::CreateKeySystemConfigs(requests)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [promise = RefPtr<dom::Promise>{aPromise}](
          const SupportedConfigsPromise::ResolveOrRejectValue& aResult) {
        if (aResult.IsResolve()) {
          // Generate CDMInformation from configs
          FallibleTArray<dom::CDMInformation> cdmInfo;
          for (const auto& config : aResult.ResolveValue()) {
            auto* info = cdmInfo.AppendElement(fallible);
            if (!info) {
              promise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
              return;
            }
            info->mKeySystemName = config.mKeySystem;
            info->mCapabilities = config.GetDebugInfo();
            info->mClearlead = DoesKeySystemSupportClearLead(config.mKeySystem);
            // TODO : ask real CDM for HDCP
            info->mIsHDCP22Compatible = false;
            // GMP-based CDM doesn't support hardware decryption.
            info->mIsHardwareDecryption = false;
          }
          promise->MaybeResolve(cdmInfo);
        } else {
          promise->MaybeReject(NS_ERROR_DOM_MEDIA_CDM_ERR);
        }
      });
}

nsString KeySystemConfig::GetDebugInfo() const {
  nsString debugInfo;
  debugInfo.AppendLiteral(" key-system=");
  debugInfo.Append(mKeySystem);
  debugInfo.AppendLiteral(" init-data-type=[");
  for (size_t idx = 0; idx < mInitDataTypes.Length(); idx++) {
    debugInfo.Append(mInitDataTypes[idx]);
    if (idx + 1 < mInitDataTypes.Length()) {
      debugInfo.AppendLiteral(",");
    }
  }
  debugInfo.AppendLiteral("]");
  debugInfo.AppendPrintf(" persistent=%s", EnumValueToString(mPersistentState));
  debugInfo.AppendPrintf(" distinctive=%s",
                         EnumValueToString(mDistinctiveIdentifier));
  debugInfo.AppendLiteral(" sessionType=[");
  for (size_t idx = 0; idx < mSessionTypes.Length(); idx++) {
    debugInfo.AppendASCII(EnumValueToString(mSessionTypes[idx]));
    if (idx + 1 < mSessionTypes.Length()) {
      debugInfo.AppendLiteral(",");
    }
  }
  debugInfo.AppendLiteral("]");
  debugInfo.AppendLiteral(" video-robustness=");
  for (size_t idx = 0; idx < mVideoRobustness.Length(); idx++) {
    debugInfo.Append(mVideoRobustness[idx]);
    if (idx + 1 < mVideoRobustness.Length()) {
      debugInfo.AppendLiteral(",");
    }
  }
  debugInfo.AppendLiteral(" audio-robustness=");
  for (size_t idx = 0; idx < mAudioRobustness.Length(); idx++) {
    debugInfo.Append(mAudioRobustness[idx]);
    if (idx + 1 < mAudioRobustness.Length()) {
      debugInfo.AppendLiteral(",");
    }
  }
  debugInfo.AppendLiteral(" MP4={");
  debugInfo.Append(NS_ConvertUTF8toUTF16(mMP4.GetDebugInfo()));
  debugInfo.AppendLiteral("}");
  debugInfo.AppendLiteral(" WEBM={");
  debugInfo.Append(NS_ConvertUTF8toUTF16(mWebM.GetDebugInfo()));
  debugInfo.AppendLiteral("}");
  debugInfo.AppendPrintf(" isHDCP22Compatible=%d", mIsHDCP22Compatible);
  return debugInfo;
}

KeySystemConfig::SessionType ConvertToKeySystemConfigSessionType(
    dom::MediaKeySessionType aType) {
  switch (aType) {
    case dom::MediaKeySessionType::Temporary:
      return KeySystemConfig::SessionType::Temporary;
    case dom::MediaKeySessionType::Persistent_license:
      return KeySystemConfig::SessionType::PersistentLicense;
    default:
      MOZ_ASSERT_UNREACHABLE("Invalid session type");
      return KeySystemConfig::SessionType::Temporary;
  }
}

}  // namespace mozilla
