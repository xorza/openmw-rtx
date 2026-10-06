# Open issues

- `film` stops with "--length has nothing to set" when the keys file's only flight carries `seconds` of its own.
- No view or suite in `files/rtx/views.cfg` and `files/rtx/benches.cfg` runs with a replacer's companion maps: a measured run forces the shipped `[Shaders] auto use object normal/specular maps` and `[RTX] specular map layout = ignore` (`applyShippedContentRules`, `RtxSettings::derive` in `apps/rtxtool/main.cpp`), so `shot`, `check`, `noise` and `bench` never trace a `_spec` map, and only `view` shows what the maps do.
- Upstream-merge PR #14 (run 37535135005) fails the Windows job at CMake configuration: `Could NOT find BZip2 (missing: BZIP2_LIBRARIES) (found version "1.0.8")`, reached from vcpkg's `boost_iostreams-config.cmake`. The PR brings upstream's runtime-dependency deployment changes to `CMakeLists.txt` and `cmake/FindOSGPlugins.cmake`; the other four jobs pass.
