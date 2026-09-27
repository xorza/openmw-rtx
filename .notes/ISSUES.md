# Open issues

- `rtx-gpu-tests --gtest_filter='RtxDlssTest.*'`, or any one test of that suite run alone, ends with `vkDeviceWaitIdle failed: VK_ERROR_DEVICE_LOST` and a device fault "an invalid write at 0x9f8c000" in the test environment's tear-down. The whole binary run does not.
