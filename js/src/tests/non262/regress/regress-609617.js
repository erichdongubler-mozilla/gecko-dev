/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/licenses/publicdomain/
 */

// SKIP test262 export
// https://github.com/tc39/ecma262/pull/2193

var actual;
var expect = "pass";

var x = "fail";
function f() {
    var x = "pass";
    delete(eval("actual = x"));
}
f();
assertEq(actual, expect);

function g() { return 1 }
function h() { function g() { throw 2; } eval('g()')++; }

assertThrowsValue(h, 2);

var lhs_prefix = ["",        "++", "--", "",   "",   "[",             "[y, "      ];
var lhs_suffix = [" = 'no'", "",   "",   "++", "--", ", y] = [3, 4]", "] = [5, 6]"];

for (var i = 0; i < lhs_prefix.length; i++) {
    var expected;
    if (/\[/.test(lhs_prefix[i])) {
        expected = "invalid destructuring target";
    } else {
        /*
         * NB: JSOP_SETCALL throws only JSMSG_ASSIGN_TO_CALL, it does not
         * specialize for ++ and -- as the compiler's error reporting does. See
         * the next section's forked assertEq code.
         */
        expected = "cannot assign to function call";
    }
    assertThrownErrorContains(
        () => eval(lhs_prefix[i] + "eval('x')" + lhs_suffix[i]),
        expected);
}

/* Now test for strict mode rejecting any SETCALL variant at compile time. */
for (var i = 0; i < lhs_prefix.length; i++) {
    var expected;
    if (/\+\+|\-\-/.test(lhs_prefix[i] || lhs_suffix[i]))
        expected = "invalid increment/decrement operand";
    else if (/\[/.test(lhs_prefix[i]))
        expected = "invalid destructuring target";
    else
        expected = "invalid assignment left-hand side";
    assertThrownErrorContains(
        () => eval("(function () { 'use strict'; " + lhs_prefix[i] + "foo('x')" + lhs_suffix[i] + "; })"),
        expected);
}

/*
 * The useless delete is optimized away, but the SETCALL must not be. It's not
 * an early error, though.
 */
var fooArg;
function foo(arg) { fooArg = arg; }
assertThrownErrorContains(
    () => eval("delete (foo('x') = 42);"),
    "cannot assign to function call");
assertEq(fooArg, 'x');

/* Delete of a call expression is not an error at all, even in strict mode. */
function g() {
    "use strict";
    assertEq(delete Object(), true);
}
g();

reportCompare(0, 0, "ok");
