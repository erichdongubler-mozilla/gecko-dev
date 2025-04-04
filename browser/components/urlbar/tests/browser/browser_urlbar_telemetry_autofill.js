/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This file tests urlbar autofill telemetry.
 */

"use strict";

const SCALAR_URLBAR = "browser.engagement.navigation.urlbar";

function assertSearchTelemetryEmpty(search_hist) {
  const scalars = TelemetryTestUtils.getProcessScalars("parent", true, false);
  Assert.ok(
    !(SCALAR_URLBAR in scalars),
    `Should not have recorded ${SCALAR_URLBAR}`
  );

  // SEARCH_COUNTS should not contain any engine counts at all. The keys in this
  // histogram are search engine telemetry identifiers.
  Assert.deepEqual(
    Object.keys(search_hist.snapshot()),
    [],
    "SEARCH_COUNTS is empty"
  );
  let sapEvent = Glean.sap.counts.testGetValue();
  Assert.equal(sapEvent, null, "Should not have recorded any SAP events");

  // Also check events.
  let events = Services.telemetry.snapshotEvents(
    Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
    false
  );
  events = (events.parent || []).filter(
    e => e[1] == "navigation" && e[2] == "search"
  );
  Assert.deepEqual(
    events,
    [],
    "Should not have recorded any navigation search events"
  );
}

function snapshotHistograms() {
  Services.telemetry.clearScalars();
  Services.telemetry.clearEvents();
  Services.fog.testResetFOG();
  return {
    search_hist: TelemetryTestUtils.getAndClearKeyedHistogram("SEARCH_COUNTS"),
  };
}

/**
 * Performs a search and picks the first result.
 *
 * @param {string} searchString
 *   The search string. Assumed to trigger an autofill result
 * @param {string} autofilledValue
 *   The input's expected value after autofill occurs.
 * @param {string} unpickResult
 *   Optional: If true, do not pick any result. Default value is false.
 * @param {string} urlToSelect
 *   Optional: If want to select result except autofill, pass the URL.
 */
async function triggerAutofillAndPickResult(
  searchString,
  autofilledValue,
  unpickResult = false,
  urlToSelect = null
) {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: searchString,
      fireInputEvent: true,
    });

    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    Assert.ok(details.autofill, "Result is autofill");
    Assert.equal(gURLBar.value, autofilledValue, "gURLBar.value");
    Assert.equal(gURLBar.selectionStart, searchString.length, "selectionStart");
    Assert.equal(gURLBar.selectionEnd, autofilledValue.length, "selectionEnd");

    if (urlToSelect) {
      for (let row = 0; row < UrlbarTestUtils.getResultCount(window); row++) {
        const result = await UrlbarTestUtils.getDetailsOfResultAt(window, row);
        if (result.url === urlToSelect) {
          UrlbarTestUtils.setSelectedRowIndex(window, row);
          break;
        }
      }
    }

    if (unpickResult) {
      // Close popup without any action.
      await UrlbarTestUtils.promisePopupClose(window);
      return;
    }

    let loadPromise = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    EventUtils.synthesizeKey("KEY_Enter");
    await loadPromise;

    let url;
    if (urlToSelect) {
      url = urlToSelect;
    } else {
      url = autofilledValue.includes(":")
        ? autofilledValue
        : "http://" + autofilledValue;
    }
    Assert.equal(gBrowser.currentURI.spec, url, "Loaded URL is correct");
  });
}

function createOtherAutofillProvider(searchString, autofilledValue) {
  return new UrlbarTestUtils.TestProvider({
    priority: Infinity,
    type: UrlbarUtils.PROVIDER_TYPE.HEURISTIC,
    results: [
      Object.assign(
        new UrlbarResult(
          UrlbarUtils.RESULT_TYPE.URL,
          UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
          {
            title: "Test",
            url: "http://example.com/",
          }
        ),
        {
          heuristic: true,
          autofill: {
            value: autofilledValue,
            selectionStart: searchString.length,
            selectionEnd: autofilledValue.length,
            // Leave out `type` to trigger "other"
          },
        }
      ),
    ],
  });
}

// Allow more time for Mac machines so they don't time out in verify mode.
if (AppConstants.platform == "macosx") {
  requestLongerTimeout(3);
}

add_setup(async function () {
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearInputHistory();

  // Enable local telemetry recording for the duration of the tests.
  const originalCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;

  // Make sure autofill is tested without upgrading pages to https
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.security.https_first", false],
      ["dom.security.https_first_schemeless", false],
    ],
  });

  registerCleanupFunction(async () => {
    Services.telemetry.canRecordExtended = originalCanRecord;
    await PlacesTestUtils.clearInputHistory();
    await PlacesUtils.history.clear();
  });
});

// Checks adaptive history, origin, and URL autofill.
add_task(async function history() {
  const testData = [
    {
      useAdaptiveHistory: true,
      visitHistory: ["http://example.com/test"],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "ex",
      autofilled: "example.com/",
    },
    {
      useAdaptiveHistory: true,
      visitHistory: ["http://example.com/test"],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "exa",
      autofilled: "example.com/test",
    },
    {
      useAdaptiveHistory: true,
      visitHistory: ["http://example.com/test"],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "exam",
      autofilled: "example.com/test",
    },
    {
      useAdaptiveHistory: true,
      visitHistory: ["http://example.com/test"],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "example.com",
      autofilled: "example.com/test",
    },
    {
      useAdaptiveHistory: true,
      visitHistory: ["http://example.com/test"],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "example.com/",
      autofilled: "example.com/test",
    },
    {
      useAdaptiveHistory: true,
      visitHistory: ["http://example.com/test"],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "example.com/test",
      autofilled: "example.com/test",
    },
    {
      useAdaptiveHistory: true,
      visitHistory: ["http://example.com/test", "http://example.org/test"],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "example.org",
      autofilled: "example.org/",
    },
    {
      useAdaptiveHistory: true,
      visitHistory: ["http://example.com/test", "http://example.com/test/url"],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "example.com/test/",
      autofilled: "example.com/test/",
    },
    {
      useAdaptiveHistory: true,
      visitHistory: [{ uri: "http://example.com/test" }],
      inputHistory: [
        { uri: "http://example.com/test", input: "http://example.com/test" },
      ],
      userInput: "http://example.com/test",
      autofilled: "http://example.com/test",
    },
    {
      useAdaptiveHistory: false,
      visitHistory: [{ uri: "http://example.com/test" }],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "example",
      autofilled: "example.com/",
    },
    {
      useAdaptiveHistory: false,
      visitHistory: [{ uri: "http://example.com/test" }],
      inputHistory: [{ uri: "http://example.com/test", input: "exa" }],
      userInput: "example.com/te",
      autofilled: "example.com/test",
    },
  ];

  for (const {
    useAdaptiveHistory,
    visitHistory,
    inputHistory,
    userInput,
    autofilled,
  } of testData) {
    const histograms = snapshotHistograms();

    await PlacesTestUtils.addVisits(visitHistory);
    for (const { uri, input } of inputHistory) {
      await UrlbarUtils.addToInputHistory(uri, input);
    }
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

    UrlbarPrefs.set("autoFill.adaptiveHistory.enabled", useAdaptiveHistory);

    await triggerAutofillAndPickResult(userInput, autofilled);

    assertSearchTelemetryEmpty(histograms.search_hist);

    UrlbarPrefs.clear("autoFill.adaptiveHistory.enabled");
    await PlacesTestUtils.clearInputHistory();
    await PlacesUtils.history.clear();
  }
});

// Checks about-page autofill (e.g., "about:about").
add_task(async function about() {
  let histograms = snapshotHistograms();
  await triggerAutofillAndPickResult("about:abou", "about:about");

  assertSearchTelemetryEmpty(histograms.search_hist);

  await PlacesUtils.history.clear();
});

// Checks the "other" fallback, which shouldn't normally happen.
add_task(async function other() {
  let searchString = "exam";
  let autofilledValue = "example.com/";
  let provider = createOtherAutofillProvider(searchString, autofilledValue);
  UrlbarProvidersManager.registerProvider(provider);

  let histograms = snapshotHistograms();
  await triggerAutofillAndPickResult(searchString, autofilledValue);

  assertSearchTelemetryEmpty(histograms.search_hist);

  await PlacesUtils.history.clear();
  UrlbarProvidersManager.unregisterProvider(provider);
});

// Checks autofill deletion telemetry.
add_task(async function deletion() {
  await PlacesTestUtils.addVisits(["http://example.com/"]);
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  info("Delete autofilled value by DELETE key");
  await doDeletionTest({
    firstSearchString: "exa",
    firstAutofilledValue: "example.com/",
    trigger: () => {
      EventUtils.synthesizeKey("KEY_Delete");
      Assert.equal(gURLBar.value, "exa");
    },
    expectedScalar: 1,
  });

  info("Delete autofilled value by BACKSPACE key");
  await doDeletionTest({
    firstSearchString: "exa",
    firstAutofilledValue: "example.com/",
    trigger: () => {
      EventUtils.synthesizeKey("KEY_Backspace");
      Assert.equal(gURLBar.value, "exa");
    },
    expectedScalar: 1,
  });

  info("Delete autofilled value twice");
  await doDeletionTest({
    firstSearchString: "exa",
    firstAutofilledValue: "example.com/",
    trigger: () => {
      // Delete autofilled string.
      EventUtils.synthesizeKey("KEY_Delete");
      Assert.equal(gURLBar.value, "exa");

      // Re-autofilling.
      EventUtils.synthesizeKey("m");
      Assert.equal(gURLBar.value, "example.com/");
      Assert.equal(gURLBar.selectionStart, "exam".length);
      Assert.equal(gURLBar.selectionEnd, "example.com/".length);

      // Delete autofilled string again.
      EventUtils.synthesizeKey("KEY_Backspace");
      Assert.equal(gURLBar.value, "exam");
    },
    expectedScalar: 2,
  });

  info("Delete one char after unselecting autofilled string");
  await doDeletionTest({
    firstSearchString: "exa",
    firstAutofilledValue: "example.com/",
    trigger: () => {
      // Cancel selection.
      EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
      Assert.equal(gURLBar.selectionStart, "example.com/".length);
      Assert.equal(gURLBar.selectionEnd, "example.com/".length);

      EventUtils.synthesizeKey("KEY_Backspace");
      Assert.equal(gURLBar.value, "example.com");
    },
    expectedScalar: 0,
  });

  info("Delete autofilled value after unselecting autofilled string");
  await doDeletionTest({
    firstSearchString: "exa",
    firstAutofilledValue: "example.com/",
    trigger: () => {
      // Cancel selection.
      EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
      Assert.equal(gURLBar.selectionStart, "example.com/".length);
      Assert.equal(gURLBar.selectionEnd, "example.com/".length);

      // Delete autofilled string one by one.
      for (let i = 0; i < "mple.com/".length; i++) {
        EventUtils.synthesizeKey("KEY_Backspace");
      }
      Assert.equal(gURLBar.value, "exa");
    },
    expectedScalar: 0,
  });

  info(
    "Delete autofilled value after unselecting autofilled string then selecting them manually again"
  );
  await doDeletionTest({
    firstSearchString: "exa",
    firstAutofilledValue: "example.com/",
    trigger: () => {
      // Cancel selection.
      const previousSelectionStart = gURLBar.selectionStart;
      const previousSelectionEnd = gURLBar.selectionEnd;
      EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
      Assert.equal(gURLBar.selectionStart, "example.com/".length);
      Assert.equal(gURLBar.selectionEnd, "example.com/".length);

      // Select same range again.
      gURLBar.selectionStart = previousSelectionStart;
      gURLBar.selectionEnd = previousSelectionEnd;

      EventUtils.synthesizeKey("KEY_Backspace");
      Assert.equal(gURLBar.value, "exa");
    },
    expectedScalar: 1,
  });

  await PlacesUtils.history.clear();
});

async function doDeletionTest({
  firstSearchString,
  firstAutofilledValue,
  trigger,
  expectedScalar,
}) {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: firstSearchString,
      fireInputEvent: true,
    });
    const details = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    Assert.ok(details.autofill, "Result is autofill");
    Assert.equal(gURLBar.value, firstAutofilledValue, "gURLBar.value");
    Assert.equal(
      gURLBar.selectionStart,
      firstSearchString.length,
      "selectionStart"
    );
    Assert.equal(
      gURLBar.selectionEnd,
      firstAutofilledValue.length,
      "selectionEnd"
    );

    await trigger();

    const scalars = TelemetryTestUtils.getProcessScalars("parent", false, true);
    if (expectedScalar) {
      TelemetryTestUtils.assertScalar(
        scalars,
        "urlbar.autofill_deletion",
        expectedScalar
      );
    } else {
      TelemetryTestUtils.assertScalarUnset(scalars, "urlbar.autofill_deletion");
    }

    await UrlbarTestUtils.promisePopupClose(window);
  });
}
