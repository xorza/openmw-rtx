# What MyGUI 3.4.3 caches of a font, and what a rebuild leaves stale — 2026-10-31

Read from the MyGUI 3.4.3 sources (tag `MyGUI3.4.3`, the version `CMakeLists.txt` requires). What `architecture.md`'s
"A live scale and font size" builds on.

## A TrueType font rebuilds itself in place only through `textureInvalidate`

- `ResourceTrueTypeFont::setResolution` and `setSize` only store the value; `initialise` builds the atlas from them,
  destroying the font's texture and creating a new one: the `ITexture*` changes, the `IFont*` does not.
- `createGlyph` fills the glyph table with `insert`, which keeps an entry already there, so a second `initialise` keeps
  every old glyph's size and UV rectangle. `textureInvalidate` clears the table and calls `initialise`: the one public
  call that rebuilds a font whole.
- `initialise` reads the font file again through `DataManager::getData`, resolved against the resource path at the time
  of the call, which `Gui::FontLoader` sets only while it loads; so the file's bytes are held instead.

## The first build of a font differs from every later one

- The first build creates a glyph for every code point in the font's ranges, a code point the face lacks among them, and
  only then drops that code point from the character map; the glyph stays, answered with the face's "not defined"
  glyph. A later build makes no such glyph, and the code point is answered with the font's `SubstituteCode`.
- With no `TabWidth` stated, the first build sets the tab's width from a space's advance and keeps it.

## What a text keeps of its font

- `EditText::setFontName` stores the `IFont*`, the font's `ITexture*` and, unless a height was set, the font's default
  height; nothing refreshes them later. `EditText` is the only MyGUI class that holds an `IFont*`.
- `TextView` stores each glyph's UV rectangle and size when the text is laid out, and lays it out again only when the
  text is out of date. A glyph is drawn at the text's height over the font's default height, so a raster change leaves
  a text's size unchanged once its height is set.
- Every text comes from one factory: `SubWidgetManager` registers `EditText` and `SimpleText` under `BasisSkin`, and a
  skin's text is made by name through it, so a replacement registered under that name sees every text.

## Textures

- `RenderItem::removeDrawItem` nulls the item's texture when its last draw item goes, and an item is found by comparing
  texture pointers: a text that leaves an item after its font's texture was replaced leaves nothing dangling; one that
  does not keeps the destroyed texture.
- The ray tracer's GUI backend keeps a dropped texture's image until the submit that last used it, so replacing a font's
  texture between frames is safe for the device.

## A book font follows the frame, not the interface scale

`FontLoader` rasterizes a book font at its resolution times the book layer's scale into the view, times the raster: at
one frame size a scale change leaves a book font where it was, and a resolution change moves it.

## Cost

A fit rasterizing the three shipped fonts at a 2560x1440 frame, debug build: 6.1 ms at a raster of 1, 7.0–7.5 ms at 2,
7.9–8.2 ms at 3.
