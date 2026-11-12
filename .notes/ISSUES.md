# Open issues

- **The monitor's log can lose entries without a word**: `MonitorLog::finish` discards the result of opening the
  fallback file, and its five-second wait for the writer ends the same whether or not every entry was written.
