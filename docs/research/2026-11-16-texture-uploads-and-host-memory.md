# What the host holds under the PBR replacers, and what the device is handed

Measured 2026-11-16 on a high-end laptop CPU beside a high-end NVIDIA laptop GPU, release build at `fee0878105`, the
PBR replacers' profile: the whole `tour`, 93 places in about 100 s.

## Host memory

The process's anonymous memory peaked at 11.6 GiB. `heaptrack` at the peak (13.1 GiB of heap with its own
overhead):

| held at the peak | from |
|---|---|
| 8.48 GiB | `ImageManager::getImage`: the decoded texture files |
| 1.81 GiB | `ContentStore::Store::readFile` through `PreparedImages`: the chains made again, held by their slots |
| 0.39 GiB | the NVIDIA driver, 0.21 GiB of it compiled pipelines |
| 0.17 GiB | NIF files read |
| 0.16 GiB | `ContentStore::Reach` |
| 0.15 GiB | the ring's prepared meshes |
| 0.14 GiB | Bullet's shapes |
| under 0.1 GiB each | the rest: the navigation mesh, placements, the scene's tables |

The resource caches at the tour's end, with the bytes of entries something outside the cache holds counted apart
(a temporary counter):

| cache | entries | holds | of it in use |
|---|---|---|---|
| images | 9,557 | 8,096 MiB | 7,588 MiB |
| templates | 5,401 | 166 MiB | 17 MiB |
| NIF files | 5,401 | 166 MiB | 0 |
| shapes | 5,316 | 52 MiB | 5 MiB |

An image is in use while a cached template's texture holds it, whether or not anything holds the template: 10% of
the templates' bytes are in use and 94% of the images'. The device stood 1,247 textures, 2,275 MiB, at the end.
With the game's cell preloader off, the images in use at the end were 6,546 MiB.

## What the device is handed

Counted per texture slot taken and per description handed to the device (temporary counters):

| | slots taken | of them a file taken before and freed | descriptions | bytes described | distinct files |
|---|---|---|---|---|---|
| freed when nothing names it | 50,533 | 42,853 | 58,586 | 51.9 GiB | 7,636 |
| resting until its room is needed | 7,722 | — (42,621 woken) | 16,158 | 8.1 GiB | |

Resting, the device stood 7,858 textures, 8,979 MiB, at the end, with 2,756 MiB of room left for textures; the tour
took 83.7 s against 96.7 s.

In the tour no slot stood its file and then its chain (settled hand-overs wait for the chain); in play a slot whose
chain was still being read stood its file first and was handed over again when the chain landed.

## Models holding headers

With the textures that rest, a model's textures holding the file's header (`Resource::ImageHeader`), and the texels let
go of once the device has its copy (the same tour, release build):

| | peak anonymous memory | heap at the peak | image cache at the end | images read again |
|---|---|---|---|---|
| models holding texels | 11.6 GiB | 13.1 GiB | 8,096 MiB | 9 |
| headers, swapped in after the load | 9.1 GiB | | 4,840 MiB | 2,345 |
| headers from the loaders | 7.0 GiB | 6.3 GiB | 1,733 MiB | 349 |
| and the facts read kept with the header | 6.1 GiB | | 1,287 MiB | 171 |

Swapped in after the load, every later load of a model read its files' texels again to make the header it had: the
loaders ask for the header themselves (`ImageManager::getModelImage`). At the peak the heap's images were 3.53 GiB:
2.3 GiB read by the game's cell preloader for the cells ahead of the flight, which no slot has stood yet, and 1.0 GiB
the ground's layers, before they too were read as headers. A fact one thread read of a file was not another's, and the
frame's thread read a file again for a fact the cell reader's had read once the texels were let go of: the facts are
kept with the header for every thread (`Resource::ImageHeader::derived`). The tour took 82.0 s.
