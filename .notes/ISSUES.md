# Open issues

- `Rtx::Image::read`'s documentation says it "leaves the image in
  `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` rather than putting it back" and that a caller wanting it
  back must transition it again. The implementation transitions it back to the layout it was handed,
  and says so in a comment beside the barrier that does it.
