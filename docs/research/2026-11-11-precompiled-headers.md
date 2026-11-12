# Precompiled headers for the four heaviest targets — 2026-11-11

A desk with a 24-core Intel laptop CPU, Linux, GCC 16.2, the `debug-linux` preset (`-O2 -g1`, x86-64-v2) against
the static vcpkg set, `-j32`.

## Where a compile's time goes

64 compiles drawn at random from `openmw-lib`, `openmw-components`, `openmw-cs-lib` and `components-tests`, each
run alone with `-ftime-report`: 139 s in all, of which parsing 63 s (45%), template instantiation ("lang.
deferred") 27 s (19%), optimisation and code generation 48 s (35%). Median compile 1.32 s, its parsing 0.76 s.

## The A/B

Each side configured in a folder of its own and its four targets built clean three times: with ccache off
(cold), with an empty ccache of its own that the build fills (fill), and again from that full ccache (warm, every
object a hit), `CCACHE_SLOPPINESS=pch_defines,time_macros` on both sides. One round.

The precompiled side gave each target `target_precompile_headers` with `<string> <vector> <map> <memory>
<functional> <filesystem> <iostream> <algorithm> <optional>` and OSG's `Node`, `Group`, `ref_ptr`, `Vec3f` and
`Matrixf`; `openmw-lib` also `<sol/sol.hpp>`, `openmw-cs-lib` `<QObject> <QWidget> <QString>`, and
`components-tests` Google Test's and Google Mock's headers.

| | without | with | |
|---|---|---|---|
| cold | 359 s | 310 s | −14% |
| fill | 403 s | 327 s | −19% |
| warm | 30 s | 35 s | +5 s |

ccache hit every object of the precompiled side on the warm build; the 5 s are the four headers precompiled
again, which no cache holds. The headers took 127 to 228 MB each (`openmw-components` the least, `openmw-cs-lib`
the most), 0.7 GB in all a build folder. MSVC was not measured.
