# Open issues

The local map keeps its radius from `distant terrain` and `viewing distance`
(`mapwindow.cpp:94`), which the ray tracing path no longer builds its world to — it pages regardless
of the first and measures itself against `distant land cells` rather than the second, so the map
shows a smaller area than the world it is a map of.
