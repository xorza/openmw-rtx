# Open issues

- The hardware cursor is made once, at the `scaling factor` setting alone
  (`WindowManager::createCursors`). The interface is drawn at a scale that follows the frame and
  the display, and the frame is shown scaled into the window, so the cursor and the interface
  differ in size whenever either scale is not one.
- `VisibilityGatesTest.aRunAnswersWhatTheReferencesFramesInAnActiveCellWouldLeaveItAs` and
  `VisibilityGatesTest.aScriptGatesWhatItTogglesBesideItAndMovesWithItsInputs` fail on every run of
  `openmw-tests --gtest_shuffle` (`visibilitygates.cpp:207`, `:210`, `:431`): they depend on state an
  earlier test leaves behind.
