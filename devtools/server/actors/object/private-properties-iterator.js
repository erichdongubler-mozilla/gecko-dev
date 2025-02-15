/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Actor } = require("resource://devtools/shared/protocol.js");
const {
  privatePropertiesIteratorSpec,
} = require("resource://devtools/shared/specs/private-properties-iterator.js");

const DevToolsUtils = require("resource://devtools/shared/DevToolsUtils.js");

loader.lazyRequireGetter(
  this,
  "propertyDescriptor",
  "resource://devtools/server/actors/object/property-descriptor.js",
  true
);

/**
 * Creates an actor to iterate over an object's private properties.
 *
 * @param objectActor ObjectActor
 *        The object actor.
 */
class PrivatePropertiesIteratorActor extends Actor {
    constructor(objectActor, conn) {
      super(conn, privatePropertiesIteratorSpec);

      let privateProperties = [];
      if (DevToolsUtils.isSafeDebuggerObject(objectActor.obj)) {
        try {
          privateProperties = objectActor.obj.getOwnPrivateProperties();
        } catch (err) {
          // The above can throw when the debuggee does not subsume the object's
          // compartment, or for some WrappedNatives like Cu.Sandbox.
        }
      }

      this.iterator = {
        size: privateProperties.length,
        propertyDescription(index) {
          // private properties are represented as Symbols on platform
          const symbol = privateProperties[index];
          return {
            name: symbol.description,
            descriptor: propertyDescriptor(objectActor, symbol, 0),
          };
        },
      };
    }

    form() {
      return {
        type: this.typeName,
        actor: this.actorID,
        count: this.iterator.size,
      };
    }

    slice({ start, count }) {
      const privateProperties = [];
      for (let i = start, m = start + count; i < m; i++) {
        privateProperties.push(this.iterator.propertyDescription(i));
      }
      return {
        privateProperties,
      };
    }

    all() {
      return this.slice({ start: 0, count: this.iterator.size });
    }
  }

exports.PrivatePropertiesIteratorActor = PrivatePropertiesIteratorActor;
