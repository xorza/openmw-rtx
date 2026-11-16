# What the host holds under the PBR replacers, and what the device is handed — 2026-11-16

A high-end laptop CPU beside a high-end NVIDIA laptop GPU, release build at `fee0878105`, the PBR replacers' profile:
the whole `tour`, 93 places in about 100 s.

## Host memory

The process's anonymous memory peaked at 11.6 GiB. `heaptrack` at the peak: 8.48 GiB the decoded texture files
(`ImageManager::getImage`), 1.81 GiB the chains made again held by their slots, 0.39 GiB the NVIDIA driver, everything
else under 0.2 GiB each. An image is in use while a cached template's texture holds it, whether or not anything holds
the template: 94% of the image cache's 8,096 MiB was in use at the end, where the device stood 2,275 MiB of textures.

## What the device is handed

| | slots taken | descriptions | bytes described | the tour |
|---|---|---|---|---|
| a texture freed when nothing names it | 50,533, 42,853 of them a file taken before | 58,586 | 51.9 GiB | 96.7 s |
| resting until its room is needed | 7,722, and 42,621 woken | 16,158 | 8.1 GiB | 83.7 s |

Resting, the device stood 8,979 MiB of textures at the end, with 2,756 MiB of room left.

## Models holding headers

With textures resting, a model's textures holding the file's header (`Resource::ImageHeader`) and the texels let go of
once the device has its copy:

| | peak anonymous memory | image cache at the end | images read again |
|---|---|---|---|
| models holding texels | 11.6 GiB | 8,096 MiB | 9 |
| headers swapped in after the load | 9.1 GiB | 4,840 MiB | 2,345 |
| headers from the loaders | 7.0 GiB | 1,733 MiB | 349 |
| and the facts read kept with the header | 6.1 GiB | 1,287 MiB | 171 |

Swapped in after the load, every later load of a model read its texels again to make the header, so the loaders ask for
the header themselves (`ImageManager::getModelImage`). A fact one thread read of a file was not another's, and the
frame's thread read a file again for a fact the cell reader had read, so the facts are kept with the header for every
thread. What remained at the peak was 2.3 GiB the game's cell preloader read for cells ahead of the flight.
