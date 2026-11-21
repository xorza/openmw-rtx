# Open issues

- **The camera keeps turning after a menu closes on a controller** (upstream
  [#8863](https://gitlab.com/OpenMW/openmw/-/issues/8863)): with the right stick held as the inventory, the main menu
  or the rest dialogue opens or closes, the view goes on turning in that direction once the menu is gone.
- **The camera jumps when the right-click menu closes** (upstream
  [#8483](https://gitlab.com/OpenMW/openmw/-/issues/8483)): closing the inventory with the pointer anywhere but the
  screen's centre turns the view. Not yet checked under the fork's SDL3 changes.
- **Numpad 2 and 8 move a focused slider with NumLock on** (upstream
  [#8552](https://gitlab.com/OpenMW/openmw/-/issues/8552)): typing a number on the numpad in a menu, the Scripts menu
  among them, also moves the slider down or up.
- **The mouse wheel over alchemy's dropdowns jumps to the first item after Create** (upstream
  [#8143](https://gitlab.com/OpenMW/openmw/-/issues/8143)), where it should move to the next one.
- **A one-hit stealth kill gives a bounty** (upstream [#8818](https://gitlab.com/OpenMW/openmw/-/issues/8818)): a
  perfect-stealth one-hit kill while sneaking with two NPCs about gives 1,040 gold, where Morrowind.exe gives none; a
  perfect-stealth hit that does not kill puts the victim in combat with a 40 gold bounty.
- **The launcher deletes every `content=` line when the game fails to start** (upstream
  [#7300](https://gitlab.com/OpenMW/openmw/-/issues/7300)), as when a data folder's drive is not mounted: the player's
  load order is gone from `openmw.cfg`.
