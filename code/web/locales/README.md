# Workbench language resources

This directory provides the display dictionaries imported by
[`i18n.js`](../i18n.js).

- [`en.js`](en.js) defines English labels and messages.
- [`zh-CN.js`](zh-CN.js) defines Simplified Chinese labels and messages.

## Translation lookups

The translator checks the selected language, then English, and finally
returns the key if neither dictionary contains it.

The current Workbench updates its visible labels through
[`workbench-labels.js`](../workbench-labels.js) and the bilingual text in
[`index.html`](../index.html).
