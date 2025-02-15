/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Helper module around `sharedData` object that helps storing the state
 * of all observed Targets and Resources, that, for all DevTools connections.
 * Here is a few words about the C++ implementation of sharedData:
 * https://searchfox.org/mozilla-central/rev/bc3600def806859c31b2c7ac06e3d69271052a89/dom/ipc/SharedMap.h#30-55
 *
 * We may have more than one DevToolsServer and one server may have more than one
 * client. This module will be the single source of truth in the parent process,
 * in order to know which targets/resources are currently observed. It will also
 * be used to declare when something starts/stops being observed.
 *
 * `sharedData` is a platform API that helps sharing JS Objects across processes.
 * We use it in order to communicate to the content process which targets and resources
 * should be observed. Content processes read this data only once, as soon as they are created.
 * It isn't used beyond this point. Content processes are not going to update it.
 * We will notify about changes in observed targets and resources for already running
 * processes by some other means. (Via JS Window Actor queries "DevTools:(un)watch(Resources|Target)")
 * This means that only this module will update the "DevTools:watchedPerWatcher" value.
 * From the parent process, we should be going through this module to fetch the data,
 * while from the content process, we will read `sharedData` directly.
 */

const { SessionDataHelpers } = ChromeUtils.importESModule(
  "resource://devtools/server/actors/watcher/SessionDataHelpers.sys.mjs",
  { global: "contextual" }
);

const { SUPPORTED_DATA } = SessionDataHelpers;
const SUPPORTED_DATA_TYPES = Object.values(SUPPORTED_DATA);

// Define the Map that will be saved in `sharedData`.
// It is keyed by WatcherActor ID and values contains following attributes:
// - targets: Set of strings, refering to target types to be listened to
// - resources: Set of strings, refering to resource types to be observed
// - sessionContext Object, The Session Context to help know what is debugged.
//     See devtools/server/actors/watcher/session-context.js
// - connectionPrefix: The DevToolsConnection prefix of the watcher actor. Used to compute new actor ID in the content processes.
//
// Unfortunately, `sharedData` is subject to race condition and may have side effect
// when read/written from multiple places in the same process,
// which is why this map should be considered as the single source of truth.
const sessionDataByWatcherActor = new Map();

// In parallel to the previous map, keep all the WatcherActor keyed by the same WatcherActor ID,
// the WatcherActor ID. We don't (and can't) propagate the WatcherActor instances to the content
// processes, but still would like to match them by their ID.
const watcherActors = new Map();

// Name of the attribute into which we save this Map in `sharedData` object.
const SHARED_DATA_KEY_NAME = "DevTools:watchedPerWatcher";

/**
 * Use `sharedData` to allow processes, early during their creation,
 * to know which resources should be listened to. This will be read
 * from the Target actor, when it gets created early during process start,
 * in order to start listening to the expected resource types.
 */
function persistMapToSharedData() {
  Services.ppmm.sharedData.set(SHARED_DATA_KEY_NAME, sessionDataByWatcherActor);
  // Request to immediately flush the data to the content processes in order to prevent
  // races (bug 1644649). Otherwise content process may have outdated sharedData
  // and try to create targets for Watcher actor that already stopped watching for targets.
  Services.ppmm.sharedData.flush();
}

export const ParentProcessWatcherRegistry = {
  /**
   * Tells if a given watcher currently watches for a given target type.
   *
   * @param WatcherActor watcher
   *               The WatcherActor which should be listening.
   * @param string targetType
   *               The new target type to query.
   * @return boolean
   *         Returns true if already watching.
   */
  isWatchingTargets(watcher, targetType) {
    const sessionData = this.getSessionData(watcher);
    return !!sessionData?.targets?.includes(targetType);
  },

  /**
   * Retrieve the data saved into `sharedData` that is used to know
   * about which type of targets and resources we care listening about.
   * `sessionDataByWatcherActor` is saved into `sharedData` after each mutation,
   * but `sessionDataByWatcherActor` is the source of truth.
   *
   * @param WatcherActor watcher
   *               The related WatcherActor which starts/stops observing.
   * @param object options (optional)
   *               A dictionary object with `createData` boolean attribute.
   *               If this attribute is set to true, we create the data structure in the Map
   *               if none exists for this prefix.
   */
  getSessionData(watcher, { createData = false } = {}) {
    // Use WatcherActor ID as a key as we may have multiple clients willing to watch for targets.
    // For example, a Browser Toolbox debugging everything and a Content Toolbox debugging
    // just one tab. We might also have multiple watchers, on the same connection when using about:debugging.
    const watcherActorID = watcher.actorID;
    let sessionData = sessionDataByWatcherActor.get(watcherActorID);
    if (!sessionData && createData) {
      sessionData = {
        // The "session context" object help understand what should be debugged and which target should be created.
        // See WatcherActor constructor for more info.
        sessionContext: watcher.sessionContext,
        // The DevToolsServerConnection prefix will be used to compute actor IDs created in the content process.
        // This prefix is unique per watcher, as we may have many watchers for a single connection.
        connectionPrefix: watcher.watcherConnectionPrefix,
      };
      sessionDataByWatcherActor.set(watcherActorID, sessionData);
      watcherActors.set(watcherActorID, watcher);
    }
    return sessionData;
  },

  /**
   * Given a Watcher Actor ID, return the related Watcher Actor instance.
   *
   * @param String actorID
   *        The Watcher Actor ID to search for.
   * @return WatcherActor
   *         The Watcher Actor instance.
   */
  getWatcher(actorID) {
    return watcherActors.get(actorID);
  },

  /**
   * Return an array of the watcher actors that match the passed browserId
   *
   * @param {Number} browserId
   * @returns {Array<WatcherActor>} An array of the matching watcher actors
   */
  getWatchersForBrowserId(browserId) {
    const watchers = [];
    for (const watcherActor of watcherActors.values()) {
      if (
        watcherActor.sessionContext.type == "browser-element" &&
        watcherActor.sessionContext.browserId === browserId
      ) {
        watchers.push(watcherActor);
      }
    }

    return watchers;
  },

  /**
   * Notify that a given watcher added or set some entries for given data type.
   *
   * @param WatcherActor watcher
   *               The WatcherActor which starts observing.
   * @param string type
   *               The type of data to be added
   * @param Array<Object> entries
   *               The values to be added to this type of data
   * @param String updateType
   *               "add" will only add the new entries in the existing data set.
   *               "set" will update the data set with the new entries.
   */
  addOrSetSessionDataEntry(watcher, type, entries, updateType) {
    const sessionData = this.getSessionData(watcher, {
      createData: true,
    });

    if (!SUPPORTED_DATA_TYPES.includes(type)) {
      throw new Error(`Unsupported session data type: ${type}`);
    }

    SessionDataHelpers.addOrSetSessionDataEntry(
      sessionData,
      type,
      entries,
      updateType
    );

    // Flush sharedData before registering the JS Actors as it is used
    // during their instantiation.
    persistMapToSharedData();

    // Register the JS Window Actor the first time we start watching for something (e.g. resource, target, …).
    if (watcher.sessionContext.type == "all") {
      registerBrowserToolboxJSProcessActor();
    } else {
      registerJSProcessActor();
    }
  },

  /**
   * Notify that a given watcher removed an entry in a given data type.
   *
   * @param WatcherActor watcher
   *               The WatcherActor which stops observing.
   * @param string type
   *               The type of data to be removed
   * @param Array<Object> entries
   *               The values to be removed to this type of data
   *
   * @return boolean
   *         True if we such entry was already registered, for this watcher actor.
   */
  removeSessionDataEntry(watcher, type, entries) {
    const sessionData = this.getSessionData(watcher);
    if (!sessionData) {
      return false;
    }

    if (!SUPPORTED_DATA_TYPES.includes(type)) {
      throw new Error(`Unsupported session data type: ${type}`);
    }

    if (
      !SessionDataHelpers.removeSessionDataEntry(sessionData, type, entries)
    ) {
      return false;
    }

    persistMapToSharedData();

    return true;
  },

  /**
   * Cleanup everything about a given watcher actor.
   * Remove it from any registry so that we stop interacting with it.
   *
   * The watcher would be automatically unregistered from removeWatcherEntry,
   * if we remove all entries. But we aren't removing all breakpoints.
   * So here, we force clearing any reference to the watcher actor when it destroys.
   */
  unregisterWatcher(watcherActorID) {
    sessionDataByWatcherActor.delete(watcherActorID);
    watcherActors.delete(watcherActorID);
    this.maybeUnregisterJSActors();
  },

  /**
   * Unregister the JS Actors if there is no more DevTools code observing any target/resource.
   */
  maybeUnregisterJSActors() {
    if (sessionDataByWatcherActor.size == 0) {
      unregisterBrowserToolboxJSProcessActor();
      unregisterJSProcessActor();
    }
  },

  /**
   * Notify that a given watcher starts observing a new target type.
   *
   * @param WatcherActor watcher
   *               The WatcherActor which starts observing.
   * @param string targetType
   *               The new target type to start listening to.
   */
  watchTargets(watcher, targetType) {
    this.addOrSetSessionDataEntry(
      watcher,
      SUPPORTED_DATA.TARGETS,
      [targetType],
      "add"
    );
  },

  /**
   * Notify that a given watcher stops observing a given target type.
   *
   * @param WatcherActor watcher
   *               The WatcherActor which stops observing.
   * @param string targetType
   *               The new target type to stop listening to.
   * @return boolean
   *         True if we were watching for this target type, for this watcher actor.
   */
  unwatchTargets(watcher, targetType) {
    return this.removeSessionDataEntry(watcher, SUPPORTED_DATA.TARGETS, [
      targetType,
    ]);
  },

  /**
   * Notify that a given watcher starts observing new resource types.
   *
   * @param WatcherActor watcher
   *               The WatcherActor which starts observing.
   * @param Array<string> resourceTypes
   *               The new resource types to start listening to.
   */
  watchResources(watcher, resourceTypes) {
    this.addOrSetSessionDataEntry(
      watcher,
      SUPPORTED_DATA.RESOURCES,
      resourceTypes,
      "add"
    );
  },

  /**
   * Notify that a given watcher stops observing given resource types.
   *
   * See `watchResources` for argument definition.
   *
   * @return boolean
   *         True if we were watching for this resource type, for this watcher actor.
   */
  unwatchResources(watcher, resourceTypes) {
    return this.removeSessionDataEntry(
      watcher,
      SUPPORTED_DATA.RESOURCES,
      resourceTypes
    );
  },
};

// Boolean flag to know if the DevToolsProcess JS Process Actor is currently registered
let isJSProcessActorRegistered = false;
let isBrowserToolboxJSProcessActorRegistered = false;

const JSProcessActorConfig = {
  parent: {
    esModuleURI:
      "resource://devtools/server/connectors/js-process-actor/DevToolsProcessParent.sys.mjs",
  },
  child: {
    esModuleURI:
      "resource://devtools/server/connectors/js-process-actor/DevToolsProcessChild.sys.mjs",
    // There is no good observer service notification we can listen to to instantiate the JSProcess Actor
    // reliably as soon as the process start.
    // So manually spawn our JSProcessActor from a process script emitting a custom observer service notification...
    observers: ["init-devtools-content-process-actor"],
  },
  // The parent process is handled very differently from content processes
  // This uses the ParentProcessTarget which inherits from BrowsingContextTarget
  // and is, for now, manually created by the descriptor as the top level target.
  includeParent: true,
};

const BrowserToolboxJSProcessActorConfig = {
  ...JSProcessActorConfig,

  // This JS Process Actor is used to bootstrap DevTools code debugging the privileged code
  // in content processes. The privileged code runs in the "shared JSM global" (See mozJSModuleLoader).
  // DevTools modules should be loaded in a distinct global in order to be able to debug this privileged code.
  // There is a strong requirement in spidermonkey for the debuggee and debugger to be using distinct compartments.
  // This flag will force both parent and child modules to be loaded via a dedicated loader (See mozJSModuleLoader::GetOrCreateDevToolsLoader)
  //
  // Note that as a side effect, it makes these modules and all their dependencies to be invisible to the debugger.
  loadInDevToolsLoader: true,
};

const PROCESS_SCRIPT_URL =
  "resource://devtools/server/connectors/js-process-actor/content-process-jsprocessactor-startup.js";

function registerJSProcessActor() {
  if (isJSProcessActorRegistered) {
    return;
  }
  isJSProcessActorRegistered = true;
  ChromeUtils.registerProcessActor("DevToolsProcess", JSProcessActorConfig);

  // There is no good observer service notification we can listen to to instantiate the JSProcess Actor
  // as soon as the process start.
  // So manually spawn our JSProcessActor from a process script emitting a custom observer service notification...
  Services.ppmm.loadProcessScript(PROCESS_SCRIPT_URL, true);
}

function registerBrowserToolboxJSProcessActor() {
  if (isBrowserToolboxJSProcessActorRegistered) {
    return;
  }
  isBrowserToolboxJSProcessActorRegistered = true;
  ChromeUtils.registerProcessActor(
    "BrowserToolboxDevToolsProcess",
    BrowserToolboxJSProcessActorConfig
  );

  // There is no good observer service notification we can listen to to instantiate the JSProcess Actor
  // as soon as the process start.
  // So manually spawn our JSProcessActor from a process script emitting a custom observer service notification...
  Services.ppmm.loadProcessScript(PROCESS_SCRIPT_URL, true);
}

function unregisterJSProcessActor() {
  if (!isJSProcessActorRegistered) {
    return;
  }
  isJSProcessActorRegistered = false;
  try {
    ChromeUtils.unregisterProcessActor("DevToolsProcess");
  } catch (e) {
    // If any pending query was still ongoing, this would throw
  }
  if (isBrowserToolboxJSProcessActorRegistered) {
    return;
  }
  Services.ppmm.removeDelayedProcessScript(PROCESS_SCRIPT_URL);
}

function unregisterBrowserToolboxJSProcessActor() {
  if (!isBrowserToolboxJSProcessActorRegistered) {
    return;
  }
  isBrowserToolboxJSProcessActorRegistered = false;
  try {
    ChromeUtils.unregisterProcessActor("BrowserToolboxDevToolsProcess");
  } catch (e) {
    // If any pending query was still ongoing, this would throw
  }
  if (isJSProcessActorRegistered) {
    return;
  }
  Services.ppmm.removeDelayedProcessScript(PROCESS_SCRIPT_URL);
}
