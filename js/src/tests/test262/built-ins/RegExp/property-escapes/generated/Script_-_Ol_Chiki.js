// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Script=Ol_Chiki`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v16.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x001C50, 0x001C7F]
  ]
});
testPropertyEscapes(
  /^\p{Script=Ol_Chiki}+$/u,
  matchSymbols,
  "\\p{Script=Ol_Chiki}"
);
testPropertyEscapes(
  /^\p{Script=Olck}+$/u,
  matchSymbols,
  "\\p{Script=Olck}"
);
testPropertyEscapes(
  /^\p{sc=Ol_Chiki}+$/u,
  matchSymbols,
  "\\p{sc=Ol_Chiki}"
);
testPropertyEscapes(
  /^\p{sc=Olck}+$/u,
  matchSymbols,
  "\\p{sc=Olck}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x001C4F],
    [0x001C80, 0x00DBFF],
    [0x00E000, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script=Ol_Chiki}+$/u,
  nonMatchSymbols,
  "\\P{Script=Ol_Chiki}"
);
testPropertyEscapes(
  /^\P{Script=Olck}+$/u,
  nonMatchSymbols,
  "\\P{Script=Olck}"
);
testPropertyEscapes(
  /^\P{sc=Ol_Chiki}+$/u,
  nonMatchSymbols,
  "\\P{sc=Ol_Chiki}"
);
testPropertyEscapes(
  /^\P{sc=Olck}+$/u,
  nonMatchSymbols,
  "\\P{sc=Olck}"
);

reportCompare(0, 0);
