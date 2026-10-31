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
- **A missed rebind is loud.** `TrackedText::doRender` debug-asserts that its texture is its font's
  current one, so a text that escaped the registry fails the first frame after a fit, and the
  test that fits, rather than drawing a destroyed texture somewhere else later.
- **A book cannot outlive its fonts.** `BookPage` registers with `InterfaceFonts` too, and takes a
  re-typeset callback when it is made, which its two owners — `DialogueWindow`'s history and
  `JournalWindow` — supply: on a fit the page drops its draw items at once and asks for a new book
  at the same page. A typeset book records the fit it was made under, and `PageDisplay` asserts
  it is current when it draws. Refused: re-typesetting from each window's `onResChange`, which
  `layOutInterface` calls for a resolution change as well and which a new owner of a book page
  could forget.
- **A text's size never reads the raster.** A glyph is drawn at the text's height over the font's
  default height (the note), so a text whose height is set keeps its size in layout units through
  a fit; `TrackedText` gives a text with no height of its own the interface's font size rather than
  the font's raster height, which is what `FontWrapper` does for the wrapped widgets today. The
  raster then cannot reach the layout from any widget, wrapped or not. Auto-sized boxes are
  re-measured after a rebind all the same, because hinting at another raster moves an advance by
  a pixel; `TrackedText` tells its owning widget, and `Gui::AutoSizedWidget` re-notifies its size.
- **The pointer is kept in frame pixels.** `MouseManager` stores the GUI cursor in the frame's
  pixels and divides by the scale where it hands it to MyGUI, and `layOut` injects it again after
  a fit, so what the pointer hovers is where it is drawn. Refused: rescaling the stored position
  at each fit, which is a second copy of the scale to keep in step.
- **Every window is held on screen after a layout, not only the tracked ones.** `layOutInterface`
  clamps each window it lays out, which the `TODO` at its end asks for; a larger scale otherwise
  leaves an untracked window partly off the frame.
- **The map follows the raster.** `LocalMap` reads the current raster scale where it asks for a
  tile rather than once in its constructor, and a fit that changes the raster re-requests the
  current cells' tiles, which `LocalMap::requestMap` already draws frames later. Refused: keeping
  the old tiles, which leaves the map at one sharpness and the text beside it at another.
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

Done: 1 and 2. Next: 3.

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

### 3 — Every text is registered

`TrackedText` and `TrackedSimpleText`, their factories, the registry and the rebind, and the
debug assert. Tests: the registry holds exactly the texts of a layout made and then destroyed;
after a fit and a rebind every text draws against its font's new texture, and a text kept out of
the registry trips the assert (`Testing::expectAssertDies`); a text's size in layout units is the
same before and after a fit to twice the raster.

### 4 — One owner and one fit

`MWGui::InterfaceFonts`, `FontFit` and `WindowManager::fitFonts` beside `fitCursors` in `layOut`;
`mRasterScale` goes. Tests: `FontFit` from hand-computed inputs (an interface scale of 1.5 on a
display of 1.0 rasters at 2; the book raster follows the view size); a display change, a
resolution change and a setting change each refit once, and a layout with nothing changed refits
nothing.

### 5 — Books re-typeset by their owners

The `BookPage` callback, the fit a typeset book records, and the assert; the dialogue history and
the journal supply the callback. Tests: a fit asks each live page once for a new book at the same
page, and a page shown a book from an older fit asserts.

### 6 — What else holds the scale

`MouseManager` in frame pixels and the injection after a fit; every window clamped in
`layOutInterface`; `LocalMap` at the current raster and its tiles re-requested; the inventory
preview's viewport recomputed on a layout. Tests: the cursor hovers the same widget through a fit
with the mouse still; a window placed at the right edge stays inside the frame after a fit to
twice the scale; a tile asked after a fit is the new resolution.

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
