var code = cacheEntry("(x => JSON.stringify(x))`bar`;");
var g = newGlobal({ cloneSingletons: true });
assertEq("[\"bar\"]", evaluate(code, { global: g, saveBytecodeWithDelazifications: true }));
assertEq("[\"bar\"]", evaluate(code, { global: g, saveBytecodeWithDelazifications: true }));
