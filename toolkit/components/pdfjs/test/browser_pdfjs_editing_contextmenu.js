/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const RELATIVE_DIR = "toolkit/components/pdfjs/test/";
const TESTROOT = "http://example.com/browser/" + RELATIVE_DIR;

// This is a modified version from browser_contextmenuFillLogins.js.
async function openContextMenuAt(browser, x, y) {
  const contextMenu = document.getElementById("contentAreaContextMenu");

  const contextMenuShownPromise = BrowserTestUtils.waitForEvent(
    contextMenu,
    "popupshown"
  );

  // Synthesize a contextmenu event to actually open the context menu.
  await BrowserTestUtils.synthesizeMouseAtPoint(
    x,
    y,
    {
      type: "contextmenu",
      button: 2,
    },
    browser
  );

  await contextMenuShownPromise;
  return contextMenu;
}

/**
 * Open a context menu and get the pdfjs entries
 * @param {Object} browser
 * @param {Object} box
 * @returns {Promise<Map<string,HTMLElement>>} the pdfjs menu entries.
 */
function getContextMenuItems(browser, box) {
  return new Promise(resolve => {
    setTimeout(async () => {
      const { x, y, width, height } = box;
      const menuitems = [
        "context-pdfjs-undo",
        "context-pdfjs-redo",
        "context-sep-pdfjs-redo",
        "context-pdfjs-cut",
        "context-pdfjs-copy",
        "context-pdfjs-paste",
        "context-pdfjs-delete",
        "context-pdfjs-selectall",
        "context-sep-pdfjs-selectall",
        "context-pdfjs-highlight-selection",
      ];

      await openContextMenuAt(browser, x + width / 2, y + height / 2);
      const results = new Map();
      const doc = browser.ownerDocument;
      for (const menuitem of menuitems) {
        const item = doc.getElementById(menuitem);
        results.set(menuitem, item || null);
      }

      resolve(results);
    }, 0);
  });
}

/**
 * Open a context menu on the element corresponding to the given selector
 * and returs the pdfjs menu entries.
 * @param {Object} browser
 * @param {string} selector
 * @returns {Promise<Map<string,HTMLElement>>} the pdfjs menu entries.
 */
async function getContextMenuItemsOn(browser, selector) {
  const box = await SpecialPowers.spawn(
    browser,
    [selector],
    async function (selector) {
      const element = content.document.querySelector(selector);
      const { x, y, width, height } = element.getBoundingClientRect();
      return { x, y, width, height };
    }
  );
  return getContextMenuItems(browser, box);
}

/**
 * Hide the context menu.
 * @param {Object} browser
 */
async function hideContextMenu(browser) {
  await new Promise(resolve =>
    setTimeout(async () => {
      const doc = browser.ownerDocument;
      const contextMenu = doc.getElementById("contentAreaContextMenu");

      const popupHiddenPromise = BrowserTestUtils.waitForEvent(
        contextMenu,
        "popuphidden"
      );
      contextMenu.hidePopup();
      await popupHiddenPromise;
      resolve();
    }, 0)
  );
}

async function clickOnItem(browser, items, entry) {
  const editingPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "editingaction",
    false,
    null,
    true
  );
  const contextMenu = document.getElementById("contentAreaContextMenu");
  contextMenu.activateItem(items.get(entry));
  await editingPromise;
}

/**
 * Asserts that the enabled pdfjs menuitems are the expected ones.
 * @param {Map<string,HTMLElement>} menuitems
 * @param {Array<string>} expected
 */
function assertMenuitems(menuitems, expected) {
  Assert.deepEqual(
    [...menuitems.values()]
      .filter(
        elmt =>
          !elmt.id.includes("-sep-") &&
          !elmt.hidden &&
          [null, "false"].includes(elmt.getAttribute("disabled"))
      )
      .map(elmt => elmt.id),
    expected
  );
}

async function waitAndCheckEmptyContextMenu(browser) {
  // check that PDF is opened with internal viewer
  await waitForPdfJSAllLayers(browser, TESTROOT + "file_pdfjs_test.pdf", [
    ["annotationEditorLayer", "annotationLayer", "textLayer", "canvasWrapper"],
    ["annotationEditorLayer", "textLayer", "canvasWrapper"],
  ]);

  const spanBox = await getSpanBox(browser, "and found references");
  const menuitems = await getContextMenuItems(browser, spanBox);

  // Nothing have been edited, hence the context menu doesn't contain any
  // pdf entries.
  Assert.ok(
    [...menuitems.values()].every(elmt => elmt.hidden),
    "No visible pdf menuitem"
  );
  await hideContextMenu(browser);
}

// Text copy, paste, undo, redo, delete and select all in using the context
// menu.
add_task(async function test_copy_paste_undo_redo() {
  makePDFJSHandler();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async function (browser) {
      SpecialPowers.clipboardCopyString("");

      await SpecialPowers.pushPrefEnv({
        set: [["pdfjs.annotationEditorMode", 0]],
      });

      await waitAndCheckEmptyContextMenu(browser);
      const spanBox = await getSpanBox(browser, "and found references");

      await enableEditor(browser, "FreeText", 1);
      await addFreeText(browser, "hello", spanBox);

      // Unselect.
      await escape(browser);

      info("Wait for the editor to be unselected");
      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".selectedEditor")) !== 1
      );
      Assert.equal(await countElements(browser, ".selectedEditor"), 0);

      let menuitems = await getContextMenuItems(browser, spanBox);
      assertMenuitems(menuitems, [
        "context-pdfjs-undo", // Last created editor is undoable
        "context-pdfjs-selectall", // and selectable.
      ]);
      // Undo.
      await clickOnItem(browser, menuitems, "context-pdfjs-undo");

      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".freeTextEditor")) !== 2
      );

      Assert.equal(
        await countElements(browser, ".freeTextEditor"),
        1,
        "The FreeText editor must have been removed"
      );

      menuitems = await getContextMenuItems(browser, spanBox);

      // The editor removed thanks to "undo" is now redoable
      assertMenuitems(menuitems, [
        "context-pdfjs-redo",
        "context-pdfjs-selectall",
      ]);
      await clickOnItem(browser, menuitems, "context-pdfjs-redo");

      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".freeTextEditor")) !== 1
      );

      Assert.equal(
        await countElements(browser, ".freeTextEditor"),
        2,
        "The FreeText editor must have been added back"
      );

      await clickOn(browser, "#pdfjs_internal_editor_0");
      menuitems = await getContextMenuItemsOn(
        browser,
        "#pdfjs_internal_editor_0"
      );

      assertMenuitems(menuitems, [
        "context-pdfjs-undo",
        "context-pdfjs-cut",
        "context-pdfjs-copy",
        "context-pdfjs-delete",
        "context-pdfjs-selectall",
      ]);

      await clickOnItem(browser, menuitems, "context-pdfjs-cut");

      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".freeTextEditor")) !== 2
      );

      Assert.equal(
        await countElements(browser, ".freeTextEditor"),
        1,
        "The FreeText editor must have been cut"
      );

      menuitems = await getContextMenuItems(browser, spanBox);
      assertMenuitems(menuitems, [
        "context-pdfjs-undo",
        "context-pdfjs-paste",
        "context-pdfjs-selectall",
      ]);

      await clickOnItem(browser, menuitems, "context-pdfjs-paste");

      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".freeTextEditor")) !== 1
      );

      Assert.equal(
        await countElements(browser, ".freeTextEditor"),
        2,
        "The FreeText editor must have been pasted"
      );

      await clickOn(browser, "#pdfjs_internal_editor_1");
      menuitems = await getContextMenuItemsOn(
        browser,
        "#pdfjs_internal_editor_1"
      );

      assertMenuitems(menuitems, [
        "context-pdfjs-undo",
        "context-pdfjs-cut",
        "context-pdfjs-copy",
        "context-pdfjs-paste",
        "context-pdfjs-delete",
        "context-pdfjs-selectall",
      ]);

      await clickOnItem(browser, menuitems, "context-pdfjs-delete");

      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".freeTextEditor")) !== 2
      );

      Assert.equal(
        await countElements(browser, ".freeTextEditor"),
        1,
        "The FreeText editor must have been deleted"
      );

      menuitems = await getContextMenuItems(browser, spanBox);
      await clickOnItem(browser, menuitems, "context-pdfjs-paste");

      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".freeTextEditor")) !== 1
      );

      Assert.equal(
        await countElements(browser, ".freeTextEditor"),
        2,
        "The FreeText editor must have been pasted"
      );

      await clickOn(browser, "#pdfjs_internal_editor_2");
      menuitems = await getContextMenuItemsOn(
        browser,
        "#pdfjs_internal_editor_2"
      );

      await clickOnItem(browser, menuitems, "context-pdfjs-copy");

      menuitems = await getContextMenuItemsOn(
        browser,
        "#pdfjs_internal_editor_2"
      );
      await clickOnItem(browser, menuitems, "context-pdfjs-paste");

      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".freeTextEditor")) !== 2
      );

      Assert.equal(
        await countElements(browser, ".freeTextEditor"),
        3,
        "The FreeText editor must have been pasted"
      );

      menuitems = await getContextMenuItems(browser, spanBox);
      await clickOnItem(browser, menuitems, "context-pdfjs-selectall");
      menuitems = await getContextMenuItems(browser, spanBox);
      await clickOnItem(browser, menuitems, "context-pdfjs-delete");

      await BrowserTestUtils.waitForCondition(
        async () => (await countElements(browser, ".freeTextEditor")) !== 3
      );

      Assert.equal(
        await countElements(browser, ".freeTextEditor"),
        0,
        "All the FreeText editors must have been deleted"
      );

      await waitForPdfJSClose(browser);
      await SpecialPowers.popPrefEnv();
    }
  );
});

add_task(async function test_highlight_selection() {
  makePDFJSHandler();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async function (browser) {
      await SpecialPowers.pushPrefEnv({
        set: [["pdfjs.annotationEditorMode", 0]],
      });

      await waitAndCheckEmptyContextMenu(browser);
      const spanBox = await getSpanBox(browser, "and found references");

      const changePromise = BrowserTestUtils.waitForContentEvent(
        browser,
        "annotationeditorstateschanged",
        false,
        null,
        true
      );
      await clickAt(
        browser,
        spanBox.x + spanBox.width / 2,
        spanBox.y + spanBox.height / 2,
        2
      );
      await changePromise;
      await TestUtils.waitForTick();

      const mozBox = await getSpanBox(browser, "Mozilla automated testing");
      const menuitems = await getContextMenuItems(browser, mozBox);

      assertMenuitems(menuitems, ["context-pdfjs-highlight-selection"]);

      const telemetryPromise = BrowserTestUtils.waitForContentEvent(
        browser,
        "reporttelemetry",
        false,
        null,
        true
      );
      await clickOnItem(
        browser,
        menuitems,
        "context-pdfjs-highlight-selection"
      );
      await telemetryPromise;

      Assert.equal(
        await countElements(browser, ".highlightEditor"),
        1,
        "An highlight editor must have been added"
      );

      await waitForPdfJSClose(browser);
    }
  );
});
