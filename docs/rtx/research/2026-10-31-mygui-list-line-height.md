# Where MyGUI 3.4.3 keeps a list's line height, and the interface's font size — 2026-10-31

Read from the MyGUI 3.4.3 sources (tag `MyGUI3.4.3`, the version `CMakeLists.txt` requires),
MyGUI's master at 2026-09-16, and the fork at `f876daae2d`. What `.notes/font-size-plan.md`
builds on.

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

## Where the font size is read, and when

At `f876daae2d`, `[GUI] font size` is read directly in 31 places:

| reader | what it sizes | when |
|---|---|---|
| `FontLoader::loadTrueTypeFont` | every TrueType font's size | once, at load |
| `FontLoader::overrideLineHeight` | every skin's `HeightLine` | once, as each skin loads |
| `FontWrapper::setFontName` | the text height of `Gui::Button`, `TextBox`, `EditBox`, `NumericEditBox`, `SharedStateButton`, as a height of its own | as the font is set |
| `Tracked::keepInterfaceHeight` | a text with no height of its own | as its font or height is set |
| 12 windows' row builders | rows at the size and 2, and 3 for spell making's controller scroll | as each row is made |
| `ItemWidget`'s count text | 3 digits or 4 | as the count is set |
| `bookpage.cpp` | glyph scale, line height | as a book is typeset and drawn |
| `JournalBooks` | the Cyrillic index's columns | as the index is typeset |
| `Formatting::BookFormatter` | a book's line height | as a book or a scroll is formatted |
| `ui._getDefaultFontSize` | `omw/mwui`'s text templates | as the scripts load |

Of the windows, the service windows (travel, training, repair, spell buying, persuasion) lay their
rows out as they open and close whenever the main menu opens; the stats, spell, inventory and
settings windows and the character creation's dialogs keep theirs across it.
