# Interface font size: one metric, read live, refitted where it is baked

Written at `f876daae2d`, after stages 1 to 7 of `.notes/gui-scale-plan.md`, which this builds on:
`Gui::InterfaceFonts`, its `Gui::FontUsers` registries, the tracked texts and the settling scroll
bar. The facts behind it are in `docs/rtx/research/2026-10-31-mygui-list-line-height.md`.

## The goal

`[GUI] font size` becomes a slider beside the interface scale that changes every text's size, and
every row, list line and book sized by it, the moment it is released, with nothing left at the old
size and no restart.

Done: 1 to 4. Stages 5 and 6 are not started.

## Why the scale's path is not enough

A scale change leaves every size in layout units where it was, so a fit of the fonts and a rebind of
the texts carry it. A font size change moves sizes in layout units: a text's height, and every
height computed from it. Those were computed once, where each thing is made, in 31 places: the
fonts' load, the line height written into every skin as the skins loaded, the rows twelve windows
build, the book typesetter and the journal's index, Lua's interface templates, and `FontWrapper`.

## Decisions

- **One owner of the metric, and no other reader.** `Gui::InterfaceFonts` holds the font size the
  interface is laid out at (`FontFit::mFontSize`, which every fit states) and answers it
  (`getFontSize`, through `MWBase::WindowManager::getFontSize`). Every consumer asks it, not
  `Settings::gui().mFontSize`; a source scan (`InterfaceFontSizeTest`) fails where any file of the
  game or its components but the window manager's fit and the setting's declaration reads the
  setting. Each row keeps its own padding on the size: one `lineHeight` would not fit spell
  making's 3, the books' 0 or the item counts' threshold. Refused: leaving the readers on the
  setting, which a new window copies and the change then misses without a sound.
- **What reads it as it draws, follows by construction.** The fonts are set at it in their fit
  (`ResourceTrueTypeFont::setSize`, rasterized as the raster is); a text with no height of its own
  takes its registry's (`FontUsers::getTextHeight`); `FontWrapper`, which stamped it into each
  widget as a height of its own, is gone. Refused: keeping `FontWrapper` and stamping each wrapped
  widget again at a change, a second record of one number.
- **A list follows through a registry of its own.** `Gui::ListBox` stands in MyGUI's `ListBox`
  factory, so every list a layout, a skin or a combo box makes is one; its lines stand at the
  registry's text height and 2, unless its skin states a `HeightLine`, and the lists' registry is
  refitted after the texts', since a list's refit makes lines and so texts. MyGUI 3.4.3 keeps the
  line height private and fixes it as the list is made; the fork's list is MyGUI master's
  `setItemHeight` (`3c5c50c`) ported to 3.4.3's private members, reached through an explicit
  instantiation and held to 3.4.3 by a `static_assert`, so MyGUI's public call replaces it once a
  release has it (the user's choice). The skins' `HeightLine` is not written at the load
  (`FontLoader::overrideLineHeight` is gone). Refused: vendoring MyGUI's master, a new dependency
  of unreleased code; and lists that follow at the next start, which is not live.
- **A window follows through one hook.** `WindowBase::refitText`, whose default does nothing, runs
  over every window the window manager holds and the character creation's dialogs after a fit to
  another size. The stats, spell, inventory and settings windows and the race, birth and review
  dialogs keep rows across the main menu and lay them out again; the service windows lay theirs
  out as they open and are closed whenever the menu is. Refused: one rebuild of the whole
  interface, which loses every open window's state, its scroll and its selection, and every Lua
  element under a script.
- **Books follow through their pages.** A book is typeset at a stated size and drawn at the same
  one (`BookTypesetter::create`), which its owner reads from the window manager, and the journal
  picks its index columns by it; a font size change drops every page's book, and the pages'
  registry has the owners typeset again, as a scale change does.
- **Lua hears it, and the built-in templates follow.** `ui._getDefaultFontSize` answers the owner
  (stage 1). A new engine handler beside `onViewportResized` reaches the menu and the player
  scripts, and `omw/mwui` rewrites the text sizes of its templates that are still at the old size,
  in place, and calls `ui.updateAll()`: a template a mod overrode keeps the mod's size. A mod's
  interface that copied the number follows when it handles the event; one that does not keeps its
  size, which is the contract Lua's interface already has for the viewport. Refused: changing the
  default text size of a Lua text with none stated, which is 10 and is Lua's API.
- **Applied when it settles, beside the scale.** A settling slider in the Video page, 12 to 18 as
  the setting clamps, in whole points; `processChangedSettings` refits on it as on the scale.

## The stages

Each a commit, tested where a fixture reaches. Each change to upstream code joins `AGENTS.md`'s
"Accepted diff".

1. **The owner and the scan — done.** Tests: a fit to another size makes every font read as a load
   at that size does, in place; a text with a height keeps it and one with none takes the size;
   the scan finds planted reads and no reader in the tree but the two allowed.
2. **No stamped heights — done.** `FontWrapper` is gone, and its widgets derive from MyGUI's. Four
   upstream files that took `Settings` through it include it. The auto-sized widgets' `mFontSize`
   strings are never read, and stay: upstream's code is not cleaned. Tests: a text box and a button
   made by MyGUI's names take a fit to another size.
3. **Lists — done.** Tests: a list's lines stand at the new height and place, its range and pixel
   position follow, its selection stays, and a list whose skin states its height keeps it.
4. **Windows — done.** `openmw-tests` stands no window, so the hooks have no test.
5. **Books and Lua.** The engine handler and `omw/mwui`'s handling of it. Tests: a page typeset
   again at the new size; a Lua test that the handler fires once a change.
6. **The setting, live.** The slider, its label in six languages, and `processChangedSettings`.

## Risks

- The windows laid out by fixed positions in their layouts (the settings window's rows among them)
  were drawn for sixteen points and do not grow with the text: 18 is the setting's top, and stage
  6 reads each window at 12 and at 18.
- The list's port reads six private members of MyGUI 3.4.3; any other MyGUI stops the build at its
  `static_assert`, where the port goes for MyGUI's own `setItemHeight` or is read again.
- Upstream changes to the windows with a hook conflict at merge time.
