# Open issues

- `rtx-gpu-tests`: `RtxDeviceTest.theDeviceAndTheRendererAreMadeWithoutAValidationError` takes 3.4 s, over the 1 s a single test may take.
- On Ubuntu's OpenSceneGraph 3.6.5, `osg::Image::computeImageSizeInBytes` gives a two-by-two BC3 level 4 bytes where `layoutOf` gives 16, so `Rtx::describeImage` refuses an image with such a level. Whether real textures reach this there (small mip levels, sides that are not multiples of four) is not checked.
