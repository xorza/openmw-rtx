# A pipeline's code forms under AMD's Windows driver

Measured 2026-11-13, on a low-end AMD integrated GPU under AMD's Windows driver 26.8.1, with
`openmw-rtxtool shot --views=seyda-neen-ship --pipeline-code=<dir>` and `Device::writeCode` asking
`vkGetPipelineExecutableInternalRepresentationsKHR` as the specification lays out: once for the forms and the size of
each, then with a buffer of that size for each body.

## What the driver answers

- **The sizes hold still.** Of 732 forms, asked for their sizes twice in a row, none answered two sizes.
- **Every ray tracing launch answers `VK_INCOMPLETE`** whatever room its bodies are given: the fog's depth and scatter
  launches and the sprites' emitters, shelter and composite, each of their executables. Each has two forms and neither
  is text: a binary one, and one with no name that reports a size of nought. With every buffer doubled, then grown to
  64 times what the driver reported, the call still answered `VK_INCOMPLETE`.
- **The compute pipelines answer whole**, their text forms among them.

## What follows

A body the driver reports short and never gives whole is no race a retry outlasts: the specification's own idiom,
asking the sizes again and the bodies with them, meets the same answer each time. The code written is the text forms'
alone, so the binary forms are asked for their sizes and no body, which a null `pData` asks for; the run then writes
318 files and ends.
