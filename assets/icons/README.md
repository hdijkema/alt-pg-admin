# Toolbar icons

SVG artwork from [Lucide](https://github.com/lucide-icons/lucide), tag 0.468.0,
commit f12b0de177fbc2a6795e99be065887e72b237123, under the ISC licence. Some
Feather-derived artwork is covered by the MIT notice included in LICENSE.txt.
The full upstream licence is retained beside these files and deployed with the app.

Source: icons/{name}.svg in that commit. Only stroke colours have been changed
for the toolbar; shapes and the original 24 by 24 view boxes are unchanged.
Blue indicates query/file actions, amber opening files, purple profiles, green
connect/run and red disconnect/stop. Disabled rendering uses the native Qt style.

The eight SVGs are embedded through assets/icons.qrc. There is no runtime download,
JavaScript package or icon framework. Qt's SVG icon-engine plugin renders them.

## Application icon

`alt-pg-admin.svg` is the application artwork supplied by Hans, separate from the
Lucide toolbar set. Qt uses the derived ICO for Windows window/dialog/taskbar icons,
avoiding SVG font rendering at startup; other platforms use the embedded SVG.
Linux packaging installs the SVG under the existing desktop application ID.

`alt-pg-admin.ico` contains 16, 24, 32, 48, 64, 128 and 256 pixel images for the
Windows executable. `alt-pg-admin.icns` supplies macOS bundle sizes through 1024
pixels, including retina variants. Both are generated from the SVG and committed.
To regenerate them after changing the SVG, run:

```powershell
py -3 tools/generate-app-icons.py
```

The generator uses Inkscape (or an explicit `--inkscape` path) and Python's standard
library. Normal builds use the checked-in assets and need neither Inkscape nor
additional Python packages. Native macOS/Linux icon display still needs validation
on those platforms.
