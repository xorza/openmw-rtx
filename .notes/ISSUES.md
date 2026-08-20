# Open issues

`files/data/mygui/openmw_settings_window.layout:884` — the `ScriptBox` widget carries the `name`
attribute twice, so the file is not well-formed XML. MyGUI's parser accepts it; every other XML tool
rejects the file.
