# Open issues

- **A refused counter ioctl reads as a thread never counted**: `ThreadCounters::start` and `stop`
  (`threadcounterslinux.cpp`) ignore the results of `PERF_EVENT_IOC_RESET`, `_ENABLE` and `_DISABLE`, so a kernel
  that refused them yields zero counts, reported as a thread the processor never ran.
- **The monitor's log can lose entries without a word**: `MonitorLog::finish` discards the result of opening the
  fallback file, and its five-second wait for the writer ends the same whether or not every entry was written.
