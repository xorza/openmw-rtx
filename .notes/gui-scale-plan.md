# Interface scale: one derived state, refitted live

Written at `5234767c33`. The facts it builds on, with their sources in MyGUI 3.4.3 and the tree,
are in `docs/rtx/research/2026-10-31-mygui-font-lifetimes.md`; this file cites that note as
"the note". Nothing here is measured yet; what has to be is listed under "Measurements".

## The goal

`[GUI] scaling factor` becomes a slider in the settings window that resizes the interface and
re-rasterizes its text the moment it is released, with no restart and nothing left soft or stale.
And the same refit runs on every other change of what the interface is drawn at — a display of
another density, another `[Video] resolution` — which today leaves the text soft, the journal's
type mis-sized and the map tiles at their start resolution until the next start.

## What is wrong today

Every reader in the note's last table that reads "once" is a cache of the scale with no owner
that refreshes it. The interface's layout already follows the scale live (`layOutInterface`), and
the cursors do (`fitCursors`, compared through `CursorFit`); the fonts, the texts that hold them,
the books typeset from them, the pointer's position and the map's tiles do not. Two of those are
latent faults on the build as it stands:

- a display change to a denser screen, or a resolution change, leaves the fonts at the raster the
  game started with (`mRasterScale` is fixed in the constructor), and the book fonts at the view
  size the game started with (`FontLoader`, `ScalingLayer::getScaleFactor`);
- the GUI cursor is kept in layout units (`MouseManager::mGuiCursorX/Y`), so a scale change moves
  what the pointer hovers without the pointer moving, until the mouse moves.

## Decisions

- **Derived, not cached: the interface's raster is a function of its inputs, refitted where they
  change.** One value type, `Gui::FontFit` — the raster and the interface's view, from which the
  loader derives each font's resolution and each book font's (the book layer's scale into the view,
  times the raster: the frame over the layer, which a scale change alone leaves where it was) — is
  computed from the presentation, the display scale, the view size and the settings in
  `WindowManager::layOut`, and compared with the last one as `CursorFit` is. `mRasterScale` goes,
  and `getRasterScale` answers the current fit. Refused: rasterizing once at start for the largest
  scale the slider reaches, which costs the atlas memory of the largest scale always and is still
  wrong after a resolution change; and a restart to apply, which the setting does not need.
- **One owner, one atomic call.** `MWGui::InterfaceFonts` owns the font loader, the text registry
  and the book subscribers, and its one mutating call is `fit(const FontFit&)`: rebuild every font,
  rebind every text, re-typeset every book, re-measure every auto-sized box, in that order, before
  the next frame draws. Nothing else can rebuild a font, so no caller can do the first step without
  the rest. Refused: separate `refitFonts` / `rebindTexts` calls on the window manager, which lets
  a future caller do one and not the other — the note's "render item" section is what that costs.
- **A font is rebuilt in place: its `IFont*` stays, its texture is replaced.** `setResolution`
  (and `setSize`), then `textureInvalidate`, which is the one public call that clears the glyph
  table before building (the note, first section). Refused: removing the font resources and
  loading them again by name, which leaves every `EditText` holding a deleted `IFont*` — and
  `getFontName`, which a rebind would call, dereferences it; refused: a TrueType font class of the
  fork's own, which re-implements MyGUI's thousand lines of FreeType packing for a property MyGUI
  already exposes.
- **A font's file is read once.** The loader keeps each font file's bytes and the data manager
  serves a font source from them, so a refit reads nothing from the VFS and does not depend on the
  data manager's resource path at the time of the call (the note: `initialise` reads the file again,
  resolved against a path the loader only sets while it loads). Refused: setting the path around
  every refit as the loader does around a load, which keeps a modal toggle and a read per refit.
- **Every text is known by construction.** The fork's `Gui::TrackedText` and `Gui::TrackedSimpleText`
  replace MyGUI's `EditText` and `SimpleText` sub-widget factories, as `Gui::Button` replaces
  MyGUI's `Button` and `PageDisplay` registers under `BasisSkin` already (the note, "one factory").
  Each enters the registry `InterfaceFonts` owns when it is made and leaves when it is destroyed;
  the factory's delegate is a method of the owner, so the registry is handed to each text and is
  no global. A rebind calls `setFontName` with the name the text kept itself. Refused: a walk of
  the widget tree, which misses skin children, popups, tooltips and widgets not attached to a
  layer — exactly the texts a missed rebind would crash on.
- **No text can miss a rebind, and a test holds every shipped skin to it.** The factory is the one
  maker of a text, and `FontUsersTest` makes a text box of every text skin MyGUI loads and finds
  a tracked text in each. Refused: an assert at the draw that a text's texture is its font's, which
  cannot see a missed text: a text whose glyphs are laid out is drawn from its render item's
  vertices without being asked, and a replaced atlas's address may be the new one's.
- **A book cannot outlive its fonts.** Every book page is a font user of a registry of its own,
  refitted after the texts': each page drops its book and the draw items holding its fonts'
  textures, then a page that dropped one asks its owner, through the callback the owner gave it
  (`BookPage::adviseRetypeset`), to typeset its books again — `DialogueWindow`'s history and
  `JournalWindow`'s stack and topic index. A page an owner already showed a new book asks nothing,
  so an owner of several pages typesets once. A page shown a book with no callback asserts, as a
  page from a layout cannot take one at construction. Refused: re-typesetting from each window's
  `onResChange`, which a new owner of a book page could forget; and a record in each book of the
  fit it was typeset under, which the typesetter has no way to know but a global.
- **A text's size never reads the raster.** A glyph is drawn at the text's height over the font's
  default height (the note), so a text whose height is set keeps its size in layout units through
  a fit; `TrackedText` gives a text with no height of its own the interface's font size rather than
  the font's raster height, which is what `FontWrapper` does for the wrapped widgets today. The
  raster then cannot reach the layout from any widget, wrapped or not. Auto-sized boxes are
  re-measured after a rebind all the same, because hinting at another raster moves an advance by
  a pixel; `TrackedText` tells its owning widget, and `Gui::AutoSizedWidget` re-notifies its size.
- **The pointer is kept in frame pixels.** `MouseManager` stores the GUI cursor in the frame's
  pixels and divides by the scale of the moment where it hands it to MyGUI or to Lua, so a press
  after a layout lands where the pointer is drawn: MyGUI moves its pointer to a press's place before
  it presses. Refused: rescaling the stored position at each fit, which is a second copy of the
  scale to keep in step; and injecting the pointer after a layout, which needs a way in from the
  window manager to the input manager for a hover that the next movement of the mouse puts right.
- **Every window is held on screen after a layout, not only the tracked ones.** `layOutInterface`
  clamps each window it lays out, which the `TODO` at its end asks for; a larger scale otherwise
  leaves an untracked window partly off the frame.
- **The map follows the raster.** `LocalMap` reads the current raster scale where it describes a
  tile rather than once in its constructor, and a tile is described again where its resolution
  moved, as one is where its depth range did; a fit that moves the raster has the player's cell
  drawn again, as a cell change does (`WindowManager::fitFonts`). Refused: keeping the old tiles,
  which leaves the map at one sharpness and the text beside it at another.
- **The slider applies when it settles.** A `Gui::ScrollBar` subclass raises `eventScrollSettled`
  on a release over its thumb, its buttons or its track, and on each wheel step; a slider with the
  user string `SettingApply="Settled"` writes its setting there and only its label while it drags.
  Refused: applying on every move, which rescales the window under the pointer while it drags and
  feeds the drag its own movement; and a list of fixed steps, which is not the slider asked for.
- **Placed in Video, under Window Border.** The setting is defined against the desktop's scale and
  the frame's resolution (`settings-default.cfg`), which is what that page holds, and the page's
  right column has the room. Refused: Preferences, the launcher's "Interface" counterpart, whose
  absolutely positioned rows fill its height to the last pixel and would have to be laid out
  again for one control.
- **50 % to 300 %, in steps of 5 %, shown as a percentage.** The setting's own clamp is 0.5 to 8;
  a value outside the slider's range in a file is kept until the slider moves.

## The stages

Done: 1 to 6. Next: 7.

Each is a commit, verified by `./omw test` on the binaries it touches and by `./omw gate` at the
end. Each stage's change to upstream code joins `AGENTS.md`'s "Accepted diff" in the same commit.

### 1 — A MyGUI instance a test can drive — done

Done: `Rtx::Testing::GuiInstance` (`apps/components_tests/rtx/support/guiinstance.hpp`) stands
MyGUI up over `myguirtx::RenderManager` and the tests' `CountingRenderer`, which now keeps the
last frame of interface it is handed, with the fork's `DataManager` over a VFS of `files/data`.
It loads the shipped TrueType fonts, the default font's name, the layers and the text skins; the
rest of the shipped skins name resource types of the game's own (`AutoSizedResourceSkin`), which
`components-tests` does not have, and a test of a window's whole layout belongs in `openmw-tests`.
The fallback seed names the fork's TrueType fonts. Its own test:
`RtxGuiInstanceTest.aTextDrawsTwoTrianglesAGlyphAgainstItsFontsAtlas`.

### 2 — The font loader owns its fonts — done

Done. `Gui::FontLoader` keeps each TrueType font it makes, with its base resolution, whether it is
a book font and whether its file states a tab's width; `MyGUIPlatform::DataManager::hold` keeps
each font file's bytes under its VFS path, which the font's source is spelt as, so the loader no
longer sets the data manager's resource path around a load. `fit(const FontFit&)` rasterizes again
each font whose resolution moves. Every rasterization goes one way (`FontLoader::rasterize`),
the load's included, which builds each font a second time at load: MyGUI's first build leaves a
glyph for each code point a face lacks, and every later build gives the font's substitute instead
(the note). `ScalingLayer::getScaleFactor` gained a form over a stated view. Tests
(`apps/components_tests/fontloader/fontfit.cpp`): a fit from 1 to 2 makes every font read as a
load at 2 makes it, in place, and leaves the book fonts alone at one frame; a fit to the same fit
makes no texture, and a view alone moves the book fonts alone; a fit reads nothing from the VFS;
a font whose file is missing is logged and left out, where the VFS's throw ended the start.

### 3 — Every text is registered — done

Done. `Gui::FontUsers` (`components/widgets/fontusers.hpp`; `TextRegistry` until stage 5) and `Gui::TrackedText` /
`TrackedSimpleText` (`trackedtext.hpp`), which stand in MyGUI's `EditText` and `SimpleText`
factories; each text enters the registry as it is made and leaves as it is destroyed, by an index
it keeps, with no search. A rebind sets the text's own font again, which takes its texture and
lays its glyphs out again. A text with no height of its own takes `[GUI] font size`. The window
manager owns a registry and registers its factories; nothing calls its rebind yet, which stage 4
does. Tests (`apps/components_tests/widgets/trackedtext.cpp`): the registry holds exactly the
texts there are; every shipped text skin makes a tracked text; after a fit to 3 and a rebind, texts
in two fonts draw against the new atlases with the letter A's new corners, where without the
rebind the draw used a destroyed texture; a text's height is the font size before and after a fit,
and in an interface loaded at 2.

### 4 — One owner and one fit — done

Done. `Gui::InterfaceFonts` (`components/fontloader/interfacefonts.hpp`, in `components` so
`components-tests` drives it) owns the loader and the registry, stands the texts in MyGUI's
factories as it is made, and its one `fit` rasterizes and, where a font moved, rebinds every
text. A rebind has a widget sized to its text (`Gui::AutoSizedWidget::remeasure`) size itself
again. The window manager owns one, destroyed after MyGUI's shutdown, and refits it in `layOut`
beside `fitCursors` (`WindowManager::fitFonts`, from `fontFitNow`); `mRasterScale` went, and
`getRasterScale` answers the fit. Tests: every font and text test now fits through the owner; a
label sized to its text follows a fit to 3, where without the remeasure it kept its old size.
Not tested: `fontFitNow`, a line of the window manager, and the layouts that call it, which no
fixture stands up; the harness's runs in the gate run the game's interface through them.

A layout refits while the game runs, so stage 4 is committed with stage 5, which has the book
pages typeset again: without it a journal or a dialogue history shown after a display or
resolution change drew a texture its font replaced.

### 5 — Books re-typeset by their owners — done

Done. `Gui::TextRegistry` became `Gui::FontUsers`, a registry of anything drawn with the fonts,
whose refit has every user drop and then every user refit; `Gui::InterfaceFonts` keeps one for the
texts and one for the book pages, refitted in that order, and a registry asserts that no user of
it is made or destroyed while it refits. `PageDisplay` is a user of the pages' registry, made by a
factory `BookPage::registerMyGUIComponents` is handed it for; it drops its book and its draw items,
then asks its owner. The dialogue history typesets again by `updateHistory`, as a resize does; the
journal keeps what typesets each book on its stack (`DisplayState::mTypeset`) and typesets each
again at the spread it was open at or its last, and its topic index where it shows. Tests
(`apps/openmw_tests/mwgui/bookpage.cpp`, `openmw-tests` now builds `Rtx::Testing::GuiInstance`):
a fit has a page's owner typeset its book again and the page draws against the new atlas, where
without the drop the owner was never asked; an owner of two pages is asked once and an empty page
not at all; a book shown with no callback asserts. Not tested: the two owners' callbacks, which no
fixture stands up a dialogue or a journal for.

### 6 — What else holds the scale — done

Done. `MouseManager` keeps the cursor in the frame's pixels (`mGuiCursor`, and the last warp as
an `std::optional`), and hands MyGUI, Lua and a warp what the scale of the moment makes of it;
its callers still speak the interface's units. `layOutInterface` holds every window inside the
view, tracked or not, and its `TODO` went. `LocalMap::mapResolution` reads the raster at each
tile, and a segment records the resolution its view was described at, rebuilding where it moved;
`WindowManager::fitFonts` has the player's cell drawn again where the raster moved. The inventory
places itself again at its mode's place and sizes its preview on a layout
(`InventoryWindow::onResChange`), which only a drag of the window did before. Not tested: each of
these is a member of a window or a manager that no fixture stands up; the gate's runs build and
start them.

### 7 — The setting, live

`processChangedSettings` refits on `[GUI] scaling factor`; the settled scroll bar and
`SettingApply`; the slider in the Video page, its label `InterfaceScaleValue` in all six
`OMWEngine` languages. Tests: a settled slider writes the setting once and a drag writes nothing;
`RtxSupportTest` finds the new control honoured.

### 8 — Then the font size, on the same path (optional)

`[GUI] font size` is already an input of `FontFit`, and `TrackedText`'s default height is the font
size: the setting can go live with a `processChangedSettings` case and a slider of its own, and
`FontWrapper` goes, since every text takes the font size by default. Its own risk is the layouts
whose rows were sized for one text height; it is worth its own read of the windows before it is
started.

## Measurements to take

- Taken in the debug flavour (the note, "Measurements"): 6 to 8 ms a fit for the three fonts, and
  the second build at load is about 12 ms more at start. Not yet taken: the release flavour, and
  a content set with large glyph ranges (the MFR profile's fonts).

## Risks

- A text made by MyGUI outside the `BasisSkin` factory would escape the registry. The sources
  show none (the note); the assert in stage 3 is what says so if one appears.
- `textureInvalidate` is MyGUI's device-lost path, used here as a rebuild. It is public and does
  exactly that; a MyGUI upgrade that changes it fails stage 2's tests.
- Upstream changes to `windowmanagerimp`, `fontloader`, `bookpage`, `dialogue`, `journalwindow`,
  `mousemanager` and `localmap` conflict with this work at merge time.
