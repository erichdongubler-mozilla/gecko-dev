// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plainyearmonth.prototype.tolocalestring
description: A time zone in resolvedOptions with a large offset still produces the correct string
locale: [en]
features: [Temporal]
---*/

const month = new Temporal.PlainYearMonth(2021, 8, "gregory");
const result = month.toLocaleString("en", { timeZone: "Pacific/Apia" });
assert.sameValue(result, "8/2021");

reportCompare(0, 0);
