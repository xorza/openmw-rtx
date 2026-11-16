# Where MyGUI 3.4.3 keeps a list's line height, and the interface's font size — 2026-10-31

Read from the MyGUI 3.4.3 sources (tag `MyGUI3.4.3`, the version `CMakeLists.txt` requires),
MyGUI's master at 2026-09-16, and the fork at `ccd26efb99`. What
`architecture.md`'s "A live scale and font size" builds on.

## A list's line height is fixed as the list is made

- `ListBox::mHeightLine` is private (`MyGUI_ListBox.h:303`), as are the lines (`mWidgetLines`,
  `:299`), the scroll bar (`mWidgetScroll`, `:295`) and the scroll state (`mTopIndex`,
  `mOffsetTop`, `mRangeIndex`, `:304-306`). No public or protected function writes the height.
- `initialiseOverride` reads it from the `HeightLine` user string once, and clamps it to 1
  (`MyGUI_ListBox.cpp:30-34`); a list whose skin states none has lines a pixel high.
- `updateLine` makes each line a `Button` of the line height and places the lines by it
  (`:315-345`); `_setScrollView` places them again from a scroll position (`:784-807`).
  `updateLine(true)` clears the size it last laid out at and redraws every line from the first.
- `ListBox::shutdownOverride` leaves `mWidgetLines` holding lines that `changeWidgetSkin`
  destroys with the client (`:56-61`, `MyGUI_Widget.cpp:143-173`): a list cannot be given its
  skin again to read a new height.
- The only reader of `HeightLine` in MyGUI and in the tree is `ListBox`. The font loader wrote
  it into every `ResourceLayout`'s root as the skins loaded, at the font size and 2.

## MyGUI's master changes it on a made list

- Commit `3c5c50c` (2026-09-14) adds `ListBox::setItemHeight`, `getItemHeight` and an
  `ItemHeight` property, and the same on `ComboBox` and `MultiListBox`. `setItemHeight` keeps the
  scroll position in pixels, clamped to the new range, resizes every line, and calls
  `updateLine(true)`.
- Commit `99ebcb5` (2026-09-16) redraws every line after a resize, which 3.4.3's
  `updateLine(true)` already does by resetting `mLastRedrawLine`.
- No release carries either at this date.

## A private member is named where an explicit instantiation names it

- C++20 [temp.explicit]: the usual access checking rules do not apply to names used to specify an
  explicit instantiation, a template argument naming a private member among them. A friend the
  instantiated template defines hands the member's pointer out.
