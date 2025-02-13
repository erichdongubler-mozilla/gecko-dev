/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ErrorList.h"
#include "mozilla/dom/quota/ForwardDecls.h"
#include "mozilla/ipc/ProtocolUtils.h"

namespace mozilla::dom::quota {

template <typename ResolverType>
class ResolveAndReturn {
 public:
  explicit ResolveAndReturn(const ResolverType& aResolver)
      : mResolver(aResolver) {}

  mozilla::ipc::IPCResult operator()(const nsresult rv) {
    mResolver(rv);
    return IPC_OK();
  }

 private:
  const ResolverType& mResolver;
};

using ResolveNSResultAndReturn =
    ResolveAndReturn<mozilla::ipc::NSResultResolver>;

using ResolveBoolResponseAndReturn =
    ResolveAndReturn<mozilla::ipc::BoolResponseResolver>;
using ResolveUInt64ResponseAndReturn =
    ResolveAndReturn<mozilla::ipc::UInt64ResponseResolver>;
using ResolveCStringArrayResponseAndReturn =
    ResolveAndReturn<mozilla::ipc::CStringArrayResponseResolver>;

using ResolveOriginUsageMetadataArrayResponseAndReturn =
    ResolveAndReturn<OriginUsageMetadataArrayResponseResolver>;
using ResolveUsageInfoResponseAndReturn =
    ResolveAndReturn<UsageInfoResponseResolver>;

}  // namespace mozilla::dom::quota
