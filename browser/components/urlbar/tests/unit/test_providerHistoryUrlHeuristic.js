/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for the behavior of UrlbarProviderHistoryUrlHeuristic.

add_setup(async function () {
  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);
  Services.prefs.setBoolPref("browser.urlbar.autoFill", false);
  Services.prefs.setBoolPref("browser.urlbar.suggest.quickactions", false);
  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("browser.search.suggest.enabled");
    Services.prefs.clearUserPref("browser.urlbar.autoFill");
    Services.prefs.clearUserPref("browser.urlbar.suggest.quickactions");
    await PlacesUtils.history.clear();
    await PlacesUtils.bookmarks.eraseEverything();
  });
});

add_task(async function test_after_clear_history() {
  await PlacesTestUtils.addVisits([
    { uri: "https://example.com/", title: "VISIT" },
  ]);
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: "https://example.com/",
    title: "BOOKMARK",
  });

  const before = createContext("example.com", { isPrivate: false });
  await check_results({
    context: before,
    matches: [
      makeVisitResult(before, {
        uri: "http://example.com/",
        title: "VISIT",
        iconUri: "page-icon:https://example.com/",
        heuristic: true,
        providerName: "HistoryUrlHeuristic",
      }),
      makeBookmarkResult(before, {
        uri: "https://example.com/",
        title: "BOOKMARK",
      }),
    ],
  });

  await PlacesUtils.history.clear();

  const after = createContext("example.com", { isPrivate: false });
  await check_results({
    context: after,
    matches: [
      makeVisitResult(after, {
        uri: "http://example.com/",
        title: "BOOKMARK",
        iconUri: "page-icon:https://example.com/",
        heuristic: true,
        providerName: "HistoryUrlHeuristic",
      }),
      makeBookmarkResult(after, {
        uri: "https://example.com/",
        title: "BOOKMARK",
      }),
    ],
  });

  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_basic() {
  await PlacesTestUtils.addVisits([
    { uri: "https://example.com/", title: "Example COM" },
  ]);

  const testCases = [
    {
      input: "https://example.com/",
      expected: context => [
        makeVisitResult(context, {
          uri: "https://example.com/",
          title: "Example COM",
          iconUri: "page-icon:https://example.com/",
          heuristic: true,
          providerName: "HistoryUrlHeuristic",
        }),
      ],
    },
    {
      input: "https://www.example.com/",
      expected: context => [
        makeVisitResult(context, {
          uri: "https://www.example.com/",
          title: "Example COM",
          iconUri: "page-icon:https://example.com/",
          heuristic: true,
          providerName: "HistoryUrlHeuristic",
        }),
      ],
    },
    {
      input: "http://example.com/",
      expected: context => [
        makeVisitResult(context, {
          uri: "http://example.com/",
          title: "Example COM",
          iconUri: "page-icon:https://example.com/",
          heuristic: true,
          providerName: "HistoryUrlHeuristic",
        }),
        makeVisitResult(context, {
          uri: "https://example.com/",
          title: "Example COM",
          iconUri: "page-icon:https://example.com/",
          providerName: "Places",
        }),
      ],
    },
    {
      input: "example.com",
      expected: context => [
        makeVisitResult(context, {
          uri: "http://example.com/",
          title: "Example COM",
          iconUri: "page-icon:https://example.com/",
          heuristic: true,
          providerName: "HistoryUrlHeuristic",
        }),
        makeVisitResult(context, {
          uri: "https://example.com/",
          title: "Example COM",
          iconUri: "page-icon:https://example.com/",
          providerName: "Places",
        }),
      ],
    },
    {
      input: "www.example.com",
      expected: context => [
        makeVisitResult(context, {
          uri: "http://www.example.com/",
          title: "Example COM",
          iconUri: "page-icon:https://example.com/",
          heuristic: true,
          providerName: "HistoryUrlHeuristic",
        }),
      ],
    },
    {
      input: "htp:example.com",
      expected: context => [
        makeVisitResult(context, {
          uri: "http://example.com/",
          title: "Example COM",
          iconUri: "page-icon:https://example.com/",
          heuristic: true,
          providerName: "HistoryUrlHeuristic",
        }),
      ],
    },
  ];

  for (const { input, expected } of testCases) {
    info(`Test with "${input}"`);
    const context = createContext(input, { isPrivate: false });
    await check_results({
      context,
      matches: expected(context),
    });
  }

  await PlacesUtils.history.clear();
});

add_task(async function test_null_title() {
  await PlacesTestUtils.addVisits([{ uri: "https://example.com/", title: "" }]);

  const context = createContext("https://example.com/", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "https://example.com/",
        fallbackTitle: "https://example.com/",
        iconUri: "page-icon:https://example.com/",
        heuristic: true,
        providerName: "HeuristicFallback",
      }),
    ],
  });

  await PlacesUtils.history.clear();
});

add_task(async function test_over_max_length_text() {
  let uri = "https://example.com/";
  for (; uri.length < UrlbarUtils.MAX_TEXT_LENGTH; ) {
    uri += "0123456789";
  }

  await PlacesTestUtils.addVisits([{ uri, title: "Example MAX" }]);

  const context = createContext(uri, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri,
        fallbackTitle: uri,
        iconUri: "page-icon:https://example.com/",
        heuristic: true,
        providerName: "HeuristicFallback",
      }),
    ],
  });

  await PlacesUtils.history.clear();
});

add_task(async function test_unsupported_protocol() {
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: "about:robots",
    title: "Robots!",
  });

  const context = createContext("about:robots", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "about:robots",
        fallbackTitle: "about:robots",
        heuristic: true,
        providerName: "HeuristicFallback",
      }),
      makeBookmarkResult(context, {
        uri: "about:robots",
        title: "Robots!",
      }),
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "about:robots",
        title: "about:robots",
        iconUri: "page-icon:about:robots",
        tags: null,
        providerName: "AboutPages",
      }),
    ],
  });

  await PlacesUtils.bookmarks.eraseEverything();
});
