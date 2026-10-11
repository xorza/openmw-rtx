# Pre-release audit: questions

## Item 20: the window size drift on a mixed-scale Wayland desktop

**Status**: the fault is confirmed in SDL 3's source, and the item is skipped until a fix can be
tested.

SDL's Wayland backend gives a new hidden window the largest scale of any display
(`Wayland_CreateWindow`, `data->scale_factor = SDL_max(...)` over every display).
`SDLUtil::setVideoMode` divides the stored pixel size by that density. When the window opens on a
display of a smaller scale, the compositor's preferred scale changes the window's pixels and keeps
its points (`Wayland_HandlePreferredScaleChanged`, with `SDL_WINDOW_HIGH_PIXEL_DENSITY`). So the
window opens smaller, and `WindowManager::windowResized` stores the smaller size. A single display,
Windows, macOS and X11 are not affected.

This desk has one display, so no fix can be tested here. The fix changes how every platform opens
and sizes the window.

| Option | What it does | Cost |
|---|---|---|
| A: re-apply the stored size once the window is mapped (recommended) | The first pixel density change after the window is shown sets the window again from the settings' pixels, and `windowResized` stores nothing until then. | Needs a test on two outputs of different scales, and a check on Windows and macOS that no first change arrives that would size the window twice. |
| B: size by the target display's scale | Before the window is mapped, divide by the scale of the display it opens on. | SDL reports a display's content scale, which is not the window's pixel density on Windows and macOS, so this needs a platform rule. |
| C: leave it and log it | Move the item to `.notes/ISSUES.md`. | The drift stays on mixed-scale Wayland desktops. |

**Blocked**: item 20 only. No other item depends on it.
