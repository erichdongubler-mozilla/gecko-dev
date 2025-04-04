/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_GpuProcessD3D11FencesHolderMap_H
#define MOZILLA_GFX_GpuProcessD3D11FencesHolderMap_H

#include <d3d11.h>
#include <vector>

#include "mozilla/layers/LayersTypes.h"
#include "mozilla/Maybe.h"
#include "mozilla/Monitor.h"
#include "mozilla/StaticPtr.h"

namespace mozilla {
namespace layers {

class FenceD3D11;

/**
 * A class to manage FenceD3D11 that is shared in GPU process.
 */
class GpuProcessD3D11FencesHolderMap {
 public:
  static void Init();
  static void Shutdown();
  static GpuProcessD3D11FencesHolderMap* Get() { return sInstance; }

  GpuProcessD3D11FencesHolderMap();
  ~GpuProcessD3D11FencesHolderMap();

  void Register(GpuProcessFencesHolderId aHolderId);
  void Unregister(GpuProcessFencesHolderId aHolderId);

  void SetWriteFence(GpuProcessFencesHolderId aHolderId,
                     RefPtr<FenceD3D11> aWriteFence);
  void SetReadFence(GpuProcessFencesHolderId aHolderId,
                    RefPtr<FenceD3D11> aReadFence);

  bool WaitWriteFence(GpuProcessFencesHolderId aHolderId,
                      ID3D11Device* aDevice);
  bool WaitAllFencesAndForget(GpuProcessFencesHolderId aHolderId,
                              ID3D11Device* aDevice);

 private:
  struct FencesHolder {
    FencesHolder() = default;

    RefPtr<FenceD3D11> mWriteFence;
    std::vector<RefPtr<FenceD3D11>> mReadFences;
  };

  mutable Monitor mMonitor MOZ_UNANNOTATED;

  std::unordered_map<GpuProcessFencesHolderId, UniquePtr<FencesHolder>,
                     GpuProcessFencesHolderId::HashFn>
      mFencesHolderById;

  static StaticAutoPtr<GpuProcessD3D11FencesHolderMap> sInstance;
};

}  // namespace layers
}  // namespace mozilla

#endif /* MOZILLA_GFX_GpuProcessD3D11FencesHolderMap_H */
