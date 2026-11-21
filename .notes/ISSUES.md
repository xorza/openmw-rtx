# Open issues

- **The camera jumps when the right-click menu closes** (upstream
  [#8483](https://gitlab.com/OpenMW/openmw/-/issues/8483)): closing the inventory with the pointer anywhere but the
  screen's centre turns the view. Not yet checked under the fork's SDL3 changes.
- **Numpad 2 and 8 move a focused slider with NumLock on** (upstream
  [#8552](https://gitlab.com/OpenMW/openmw/-/issues/8552)): typing a number on the numpad in a menu, the Scripts menu
  among them, also moves the slider down or up.
- **A one-hit stealth kill gives a bounty** (upstream [#8818](https://gitlab.com/OpenMW/openmw/-/issues/8818)): a
  perfect-stealth one-hit kill while sneaking with two NPCs about gives 1,040 gold, where Morrowind.exe gives none; a
  perfect-stealth hit that does not kill puts the victim in combat with a 40 gold bounty.
- **The launcher deletes every `content=` line when the game fails to start** (upstream
  [#7300](https://gitlab.com/OpenMW/openmw/-/issues/7300)), as when a data folder's drive is not mounted: the player's
  load order is gone from `openmw.cfg`.
