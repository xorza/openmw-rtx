# Open issues

- On the Fullrest Repack, `bench --suite=streaming` draws a frame of 29 to 129 ms at about every cell crossing, in the `update` row; the main thread's profile over the run puts that row in `MWWorld::Scene::changeCellGrid` → `insertCell`, building Bullet triangle-mesh shapes for references the cell preloader had not reached at the suite's 12 000 units a second, and in `World::getFocusObject`.
- On the low-end AMD integrated GPU under AMD's Windows driver 26.8.1, at `6610d328e6`, `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` fails: the water kernel of the whole tuple holds 248 registers against its stated ceiling of 244.
