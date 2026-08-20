`runView` frees its semaphores and fences as plain statements after the frame loop, so any exception
leaving the loop skips all seven of them. The validation layers report it as leaked objects at
`vkDestroyDevice`, and with them off it is silent.

Running `view` under `--gpu-validation` leaves the frame loop after about twenty seconds without
printing its own summary and without reaching that teardown. The same session without that layer
runs to its frame limit.
