# Open issues

- `openmw-tests` hangs now and then on the Windows runner after `cs`, `crash.matrix` and `components`
  pass, and holds the job to its time limit, with no output from it (CI runs on `95dc0c5231`,
  `0ffb93bfee` and the upstream merge PR #10). 587 rounds of the four suites started together on the
  same runner image did not reproduce it.
- `RtxCellRingTest.referencesStandWhereTheGameWouldStandThemOnOneMeshEach` failed once under
  `./omw gate`: "a steady walk of the ring reached the heap 7 times". It passed 200 runs alone and
  5 full shuffled runs of `components-tests` with the same seed (80648).
