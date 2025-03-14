// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindate.prototype.tostring
description: Properties on an object passed to toString() are accessed in the correct order
includes: [compareArray.js, temporalHelpers.js]
features: [Temporal]
---*/

const expected = [
  "get options.calendarName",
  "get options.calendarName.toString",
  "call options.calendarName.toString",
];
const actual = [];

const instance = new Temporal.PlainDate(2000, 5, 2, "iso8601");

const options = TemporalHelpers.propertyBagObserver(actual, {
  calendarName: "auto",
}, "options");

instance.toString(options);
assert.compareArray(actual, expected, "order of operations");

reportCompare(0, 0);
