# Open issues

- `Gui::AutoSizedTextBox`, `AutoSizedEditBox` and `AutoSizedButton` each declare a `std::string mFontSize` (`components/widgets/box.hpp`) that nothing writes or reads.
- `components/widgets/widgets.hpp` declares `extern int GuiFontHeight`, which nothing defines or reads.
- `tools/omw/spellings.py` checks only the files under `FORK`'s prefixes (`tools/omw/system.py`), so the fork's own files elsewhere — `apps/openmw_tests/mwgui/fontsize.cpp`, `apps/components_tests/widgets/`, `components/widgets/listbox.cpp`, `components/fontloader/interfacefonts.cpp` among them — are not checked.
