// Copyright (C) 2023 Anthony Frehner and Kevin Gibbons. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-set.prototype.difference
description: Set.prototype.difference length property
info: |
    Set.prototype.difference ( other )
includes: [propertyHelper.js]
features: [set-methods]
---*/
assert.sameValue(typeof Set.prototype.difference, "function");

verifyProperty(Set.prototype.difference, "length", {
  enumerable: false,
  writable: false,
  configurable: true,
  value: 1,
});

reportCompare(0, 0);
