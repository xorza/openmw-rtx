# Open issues

- `Gui::AutoSizedTextBox`, `AutoSizedEditBox` and `AutoSizedButton` each declare a `std::string mFontSize` (`components/widgets/box.hpp`) that nothing writes or reads.
- `components/widgets/widgets.hpp` declares `extern int GuiFontHeight`, which nothing defines or reads.
