/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Component } from "resource://devtools/client/shared/vendor/react.mjs";
import PropTypes from "resource://devtools/client/shared/vendor/react-prop-types.mjs";
import { createFactories } from "resource://devtools/client/shared/react-utils.mjs";

import ToolbarClass from "resource://devtools/client/jsonview/components/reps/Toolbar.mjs";

const { Toolbar, ToolbarButton } = createFactories(ToolbarClass);

/**
 * This template is responsible for rendering a toolbar
 * within the 'Headers' panel.
 */
class HeadersToolbar extends Component {
  static get propTypes() {
    return {
      actions: PropTypes.object,
    };
  }

  constructor(props) {
    super(props);
    this.onCopy = this.onCopy.bind(this);
  }

  // Commands

  onCopy() {
    this.props.actions.onCopyHeaders();
  }

  render() {
    return Toolbar(
      {},
      ToolbarButton(
        { className: "btn copy", onClick: this.onCopy },
        JSONView.Locale["jsonViewer.Copy"]
      )
    );
  }
}

export default { HeadersToolbar };
