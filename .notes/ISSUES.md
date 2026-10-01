# Open issues

- `openmw-tests` hangs now and then on the Windows runner after `cs`, `crash.matrix` and `components`
  pass, and holds the job to its time limit, with no output from it (CI runs on `95dc0c5231`,
  `0ffb93bfee` and the upstream merge PR #10). 587 rounds of the four suites started together on the
  same runner image did not reproduce it.
