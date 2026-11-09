# Open issues

- On a low-end AMD laptop under Windows, `components-tests` filtered to the preparation, platform, resource and image suites ended once with `0xC0000409` after all 84 tests passed (random seed 61386); the same filter passed and exited 0 seven more times, the same seed among them.
- CI's `linux-tsan` on `master` (`736479fb8b`, run 42689072803) reports a data race in `components-tests`: two preparation workers inside `Rtx::ContentPreparation::walk` (`contentpreparation.cpp:408`) write OSG's shared notify stream at once, one through `osg::notify` and `fputs`, the other through `std::basic_streambuf::xsputn`.
