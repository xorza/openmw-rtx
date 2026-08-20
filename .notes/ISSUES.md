`view` under `--gpu-validation` loses the device — `vkWaitForFences` returns `VK_ERROR_DEVICE_LOST`
on three runs of four, between twenty seconds and a minute in. Three thousand headless traces of the
same frame under the same layer do not, so it is not the shader. It is why `view` leaves that layer
off unless it is asked for.
