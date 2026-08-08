# Third-Party Notices

ProPager itself is released under the MIT License (see `LICENSE`). The
distributed application also incorporates the following third-party software.

## Qt

ProPager is built with, and dynamically links against, the **Qt framework
(version 6.8.3 LTS)** — the Qt modules Core, Widgets, Network, and WebSockets,
together with the Qt platform and image plugins bundled inside the application.

Qt is used under the terms of the **GNU Lesser General Public License, version 3
(LGPLv3)**. The LGPLv3 incorporates by reference the GNU General Public License,
version 3 (GPLv3). Copies of both license texts are distributed with the
application:

- `licenses/LGPL-3.0.txt`
- `licenses/GPL-3.0.txt`

In a released `.app` bundle these files are located at
`ProPager.app/Contents/Resources/licenses/`, and are also reachable from the
menu-bar menu via **About ProPager → Show Licenses**.

### Dynamic linking / replaceability

Qt is **not** statically linked into ProPager. The Qt frameworks and plugins
ship as separate dynamic libraries inside the application bundle
(`ProPager.app/Contents/Frameworks` and `.../PlugIns`), so a user may replace
them with a modified build of Qt of the same major/minor version, as permitted
by section 4 of the LGPLv3.

> Note: a code-signed and/or notarized release invalidates its signature if the
> bundled Qt libraries are replaced. To run ProPager with a modified Qt, re-sign
> the bundle with your own signing identity (or run an unsigned local build).

### Corresponding source

The complete corresponding source code for the exact version of Qt used
(6.8.3) is available from the Qt Project:

- <https://download.qt.io/archive/qt/6.8/6.8.3/single/>

Qt is a trademark of The Qt Company Ltd. and/or its subsidiaries. For more
information about Qt licensing, see <https://www.qt.io/licensing/> and
<https://doc.qt.io/qt-6/licenses-used-in-qt.html>.
