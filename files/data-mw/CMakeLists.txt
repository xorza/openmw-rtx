set(BUILTIN_DATA_MW_FILES
    # Month names and date formatting
    l10n/Calendar/de.yaml
    l10n/Calendar/en.yaml
    l10n/Calendar/gmst.yaml
    l10n/Calendar/ru.yaml
    l10n/Calendar/sv.yaml
    l10n/Calendar/fr.yaml
    l10n/Calendar/pl.yaml
    # Generic UI messages that can be reused by mods
    l10n/Interface/gmst.yaml
    # L10n for game-specific mechanics
    l10n/Mechanics/gmst.yaml
    # L10n for OpenMW menus and non-game-specific messages
    l10n/OMWEngine/gmst.yaml
    # Game-specific settings for calendar.lua
    openmw_aux/calendarconfig.lua
    scripts/omw/cellhandlers.lua
    scripts/omw/esmfallbacks.lua
    scripts/omw/weather.lua
    scripts/omw/combat/common.lua
    scripts/omw/combat/global.lua
    scripts/omw/combat/local.lua
    scripts/omw/combat/settingspage.lua
    scripts/omw/music/helpers.lua
    scripts/omw/music/music.lua
    scripts/omw/music/settings.lua
    scripts/omw/playerskillhandlers.lua
    scripts/omw/spellcasting/common.lua
    scripts/omw/spellcasting/global.lua
    scripts/omw/spellcasting/local.lua
    scripts/omw/spellcasting/lockable.lua
    scripts/omw/surfacelight/common.lua
    scripts/omw/surfacelight/global.lua
    scripts/omw/surfacelight/player.lua
)

foreach(f IN LISTS BUILTIN_DATA_MW_FILES)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${f}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/vfs-mw/${f}"
        COPYONLY
    )
endforeach()

# Concat data/builtin.omwscripts and data-mw/builtin.omwscripts.in to create vfs-mw/builtin.omwscripts
set(builtinBase "${CMAKE_CURRENT_SOURCE_DIR}/../data/builtin.omwscripts")

# The file is among the configure's inputs already, as `files/data` copies it with `configure_file`: a second
# registration, spelt from here, is Ninja's "multiple rules generate" (CMake issue 20181).

file(READ "${builtinBase}" BUILTIN_SCRIPTS)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/builtin.omwscripts.in"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/vfs-mw/builtin.omwscripts"
)
