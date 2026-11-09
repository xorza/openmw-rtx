# Open issues

- On the Fullrest Repack, `bench --suite=streaming` draws a frame of 29 to 129 ms at about every cell crossing, in the `update` row; the main thread's profile over the run puts that row in `MWWorld::Scene::changeCellGrid` → `insertCell`, building Bullet triangle-mesh shapes for references the cell preloader had not reached at the suite's 12 000 units a second, and in `World::getFocusObject`.
