# Open issues

- **The panes kernel spills past its ceiling on AMD's Windows driver**: on a low-end AMD integrated GPU under AMD's
  Windows driver 26.8.1, `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` reads 256 to 272 bytes of scratch a
  lane for the `panes` kernel's tuples with the moons or the dark map, against the 80 its ceiling allows
  (`apps/components_tests/rtxvulkan/trace/visibility/kernels.cpp`); its 208 without the moons is past it too.
