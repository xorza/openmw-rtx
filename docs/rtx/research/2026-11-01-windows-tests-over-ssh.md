# Running the tests on Windows from another machine — 2026-11-01

Radeon 680M, Windows 11, OpenSSH's server, `bounce-depth` at `a2052b0ba0`.

- **A process SSH or WMI starts has no interactive desktop, and its window no surface a swapchain
  takes**: `vkCreateSwapchainKHR` returns `VK_ERROR_FORMAT_NOT_SUPPORTED` in every
  `RtxPresenterTest`, as in a service. The same build passes all three from the logged-on user's
  session.
- **The logged-on session is reached through a scheduled task made with `/IT`**:
  `schtasks /create /tn <name> /tr "<command>" /sc once /st 23:59 /it /f`, then `schtasks /run`,
  and `schtasks /query /tn <name> /fo csv /nh` says `Running` until it ends. The task's output goes
  to a file the command writes, since the task has no console to read.
- **`cmd` splits a batch file's arguments at `=`**, so `--gtest_filter=<pattern>` reaches `%1` as two
  words and the test binary prints its help. The filter goes through the environment instead,
  `set GTEST_FILTER=<pattern>`, which Google Test reads where no flag names one.
