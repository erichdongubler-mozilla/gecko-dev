// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.instant.prototype.until
description: Type conversions for roundingMode option
includes: [compareArray.js, temporalHelpers.js]
features: [Temporal]
---*/

const earlier = new Temporal.Instant(1_000_000_000_000_000_000n);
const later = new Temporal.Instant(1_000_090_061_123_987_500n);
TemporalHelpers.checkStringOptionWrongType("roundingMode", "trunc",
  (roundingMode) => earlier.until(later, { smallestUnit: "microsecond", roundingMode }),
  (result, descr) => TemporalHelpers.assertDuration(result, 0, 0, 0, 0, 0, 0, 90061, 123, 987, 0, descr),
);

reportCompare(0, 0);
