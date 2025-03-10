======================
Firefox Home (New Tab)
======================

All files related to Firefox Home, which includes content that appears on ``about:home`` and
``about:newtab``, can be found in the ``browser/extensions/newtab`` directory.
Some of these source files (such as ``.js``, ``.jsx``, and ``.scss``) require an additional build step.
We are working on migrating this to work with ``mach``, but in the meantime, please
follow the following steps if you need to make changes in this directory:

For ``.sys.mjs`` files (system modules)
---------------------------------------------------

No build step is necessary. Use ``mach`` and run mochitests according to your regular Firefox workflow.

For ``.js``, ``.jsx``, ``.scss``, or ``.css`` files
---------------------------------------------------

Prerequisites
`````````````

You will need the following:

- Node.js 10+ (On Mac, the best way to install Node.js is to use the install link on the `Node.js homepage`_)
- npm (packaged with Node.js)

To install dependencies, run the following from the root of the mozilla-central repository.
(Using ``mach`` to call ``npm`` and ``node`` commands will ensure you're using the correct versions of Node and npm.)

.. code-block:: shell

  (cd browser/extensions/newtab && ../../../mach npm install)


Which files should you edit?
````````````````````````````

You should not make changes to ``.js`` or ``.css`` files in ``browser/extensions/newtab/css`` or
``browser/extensions/newtab/data`` directory. Instead, you should edit the ``.jsx``, ``.js``, and ``.scss`` source files
in ``browser/extensions/newtab/content-src`` directory. These files will be compiled into the ``.js`` and ``.css`` files.


Building assets and running Firefox
-----------------------------------

To build assets and run Firefox, run the following from the root of the mozilla-central repository:

.. code-block:: shell

  ./mach npm run bundle --prefix=browser/extensions/newtab && ./mach build && ./mach run

Continuous development / debugging
----------------------------------

For near real-time reloading, run the following commands in separate terminals to automatically rebuild bundled files whenever JSX or SCSS files change. After making a change, `restart your local instance </devtools-user/browser_console/index.html#controlling-the-browser>`_ to apply the updates.

.. code-block:: shell

  ./mach npm run watchmc --prefix=browser/extensions/newtab
  ./mach run
  ./mach watch


**IMPORTANT NOTE**: This task will add inline source maps to help with debugging, which changes the memory footprint. Do not use the ``watchmc`` task for profiling or performance testing! After finalizing your changes, be sure to run `the bundle command <./index.html#building-assets-and-running-firefox>`_ again before committing to remove the inline source maps.


Running tests
-------------
The majority of New Tab / Messaging unit tests are written using
`mocha <https://mochajs.org>`_, and other errors that may show up there are
`SCSS <https://sass-lang.com/documentation/syntax>`_ issues flagged by
`stylelint <https://stylelint.io>`_.  These things are all run using
``npm test`` under the ``newtab`` slug in Treeherder/Try, so if that slug turns
red, these tests are what is failing.  To execute them, do this:

.. code-block:: shell

  ./mach npm test --prefix=browser/extensions/newtab

These tests are not currently run by ``mach test``, but there's a
`task filed to fix that <https://bugzilla.mozilla.org/show_bug.cgi?id=1581165>`_.

Windows isn't currently supported by ``npm test``
(`path/invocation difference <https://bugzilla.mozilla.org/show_bug.cgi?id=1737419>`_).
To run newtab specific tests that aren't covered by ``mach lint`` and
``mach test``:

.. code-block:: shell

  ./mach npm run lint:stylelint --prefix=browser/extensions/newtab
  ./mach npm run testmc:build --prefix=browser/extensions/newtab
  ./mach npm run testmc:unit --prefix=browser/extensions/newtab

Mochitests and xpcshell tests run normally, using ``mach test``.

Code Coverage
-------------
Our testing setup will run code coverage tools in addition to just the unit
tests. It will error out if the code coverage metrics don't meet certain thresholds.

If you see any missing test coverage, you can inspect the coverage report by
running

.. code-block:: shell

  ./mach npm test --prefix=browser/extensions/newtab &&
  ./mach npm run debugcoverage --prefix=browser/extensions/newtab

Discovery Stream Developer tools
--------------------------------

You can access the developer tools for the Discovery Stream components of about:newtab by
visiting `about:config` and setting `browser.newtabpage.activity-stream.asrouter.devtoolsEnabled`
to `true`.

Then, go to any `about:newtab` page and click on the wrench icon in the top-right corner.

Detailed Docs
-------------
.. toctree::
  :titlesonly:
  :glob:

  v2-system-addon/*

..  _Node.js homepage: https://nodejs.org/
