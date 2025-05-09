// Copyright (C) 2024 Mozilla Corporation. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
includes: [sm/non262.js, sm/non262-shell.js]
flags:
  - noStrict
description: |
  pending
esid: pending
---*/
let arrow;

class foo extends class { } {
    constructor() {
        arrow = () => this;
        super();
    }
}

assert.sameValue(new foo(), arrow());


reportCompare(0, 0);
