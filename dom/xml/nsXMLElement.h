/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXMLElement_h___
#define nsXMLElement_h___

#include "mozilla/dom/Element.h"

class nsXMLElement : public mozilla::dom::Element {
 public:
  explicit nsXMLElement(already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
      : mozilla::dom::Element(std::move(aNodeInfo)) {}

  // nsISupports
  NS_INLINE_DECL_REFCOUNTING_INHERITED(nsXMLElement, mozilla::dom::Element)

  // nsINode interface methods
  nsresult Clone(mozilla::dom::NodeInfo*, nsINode** aResult) const override;

  void UnbindFromTree(UnbindContext&) override;

 protected:
  virtual ~nsXMLElement() = default;
};

#endif  // nsXMLElement_h___
