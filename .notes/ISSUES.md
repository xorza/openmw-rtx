# Open issues

- A float setting accepts `inf` and `nan`: `Settings::parseNumberFromSetting` reads them through
  `std::from_chars`, and the float sanitizers (`MaxStrict`, `Max`, `Clamp`) pass `nan` through and
  `MaxStrict` and `Max` pass `inf`.
