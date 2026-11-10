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
