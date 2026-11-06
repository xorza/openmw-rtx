# Open issues

- `omw.tests.test_package.ReleaseBaseTest.test_the_container_runs_omw_on_the_tree_with_its_own_build_and_cache`
  fails in a git worktree: the container command adds a volume for the repository beside the tree, which the
  test's list of volumes does not hold, so `./omw gate` stops there in any worktree.
