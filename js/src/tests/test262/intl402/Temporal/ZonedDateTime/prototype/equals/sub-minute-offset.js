// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.zoneddatetime.prototype.equals
description: Fuzzy matching behaviour for UTC offset in ISO 8601 string with named time zones
features: [Temporal]
---*/

const expectedNanoseconds = BigInt((44 * 60 + 30) * 1e9);
const instance = new Temporal.ZonedDateTime(expectedNanoseconds, "Africa/Monrovia");

let result = instance.equals("1970-01-01T00:00:00-00:45[Africa/Monrovia]");
assert.sameValue(result, true, "UTC offset rounded to minutes is accepted");

result = instance.equals("1970-01-01T00:00:00-00:44:30[Africa/Monrovia]");
assert.sameValue(result, true, "Unrounded sub-minute UTC offset also accepted");

assert.throws(
  RangeError,
  () => instance.equals("1970-01-01T00:00:00-00:44:30[-00:45]"),
  "minute rounding not supported for offset time zones"
);

const properties = {
  offset: "-00:45",
  year: 1970,
  month: 1,
  day: 1,
  minute: 44,
  second: 30,
  timeZone: "Africa/Monrovia"
};
assert.throws(RangeError, () => instance.equals(properties), "no fuzzy matching is done on offset in property bag");

reportCompare(0, 0);
