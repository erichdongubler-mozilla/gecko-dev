/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ColorPickerParent_h
#define mozilla_dom_ColorPickerParent_h

#include "mozilla/dom/PColorPickerParent.h"
#include "nsIColorPicker.h"

namespace mozilla::dom {

class ColorPickerParent : public PColorPickerParent {
 public:
  ColorPickerParent(BrowsingContext* aBrowsingContext, const nsString& aTitle,
                    const nsString& aInitialColor,
                    const nsTArray<nsString>& aDefaultColors)
      : mBrowsingContext(aBrowsingContext),
        mTitle(aTitle),
        mInitialColor(aInitialColor),
        mDefaultColors(aDefaultColors.Clone()) {}

  NS_INLINE_DECL_REFCOUNTING(ColorPickerParent, final)

  virtual mozilla::ipc::IPCResult RecvOpen() override;
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  class ColorPickerShownCallback final : public nsIColorPickerShownCallback {
   public:
    explicit ColorPickerShownCallback(ColorPickerParent* aColorPickerParnet)
        : mColorPickerParent(aColorPickerParnet) {}

    NS_DECL_ISUPPORTS
    NS_DECL_NSICOLORPICKERSHOWNCALLBACK

    void Destroy();

   private:
    ~ColorPickerShownCallback() = default;
    RefPtr<ColorPickerParent> mColorPickerParent;
  };

 private:
  virtual ~ColorPickerParent() = default;

  bool CreateColorPicker();

  RefPtr<BrowsingContext> mBrowsingContext;
  RefPtr<ColorPickerShownCallback> mCallback;
  nsCOMPtr<nsIColorPicker> mPicker;

  nsString mTitle;
  nsString mInitialColor;
  nsTArray<nsString> mDefaultColors;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ColorPickerParent_h
