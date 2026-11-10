# Open issues

- **`test_script_placed` fails on Windows**: "a script placed object joins the navigation mesh when an
  object is disabled" fails three runs of three on an AMD integrated GPU under Windows 11 (full flavour,
  `7b26b022b8`): the physics meets the placed `hubtemple_pillar01` at x 4272.2, and the navigation ray
  reaches x 4500 through the whole 600-frame wait. The cell grid case of the same suite passes there, and
  both pass on Linux. CI runs the integration tests on Linux alone.

- **The launcher writes over a profile's encoding on its first run**: `DataFilesPage::saveSettings` writes
  `encoding` from the language box alone, and a profile with no `launcher.cfg` opens the box on English. A
  Russian profile's `encoding=win1251` became `win1252` at the first Play, and the game drew its Cyrillic text
  as Latin-1.
