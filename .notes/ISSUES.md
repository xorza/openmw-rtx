# Open issues

- A Lua script writes a gate script's local through `getLocalScript(obj).variables`, and the visibility
  gates' scan of the content files does not see that write: such a gate decides from a local at nought.
- `RtxTool::GpuBreakdown::add` reserves one counter reading a frame for a zone and appends one a span,
  so under `--counters` a zone that opens more than once in a frame (`tlas`) outgrows its row inside a
  measured frame.
