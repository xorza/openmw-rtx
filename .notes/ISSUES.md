# Open issues

- Under the PBR profile the handheld's tour flies all 93 places but at the edge of its memory: the system had 131 MiB available at the least and 5 GiB of swap in use, the process's anonymous memory, resident and swapped together, peaked at 9.5 GiB while the resource caches held 2.3 GiB of it, and with the caches past their budget all in use the images were loaded 60,777 times, 52,066 of them again, the tour taking 1,209 s against the desk's 94.
- A model let go of and loaded again lays the ray tracer's scene out differently from a run that kept it: two stepped runs behind the PBR replacers that dropped different models laid the scene out two ways from `island-crossing` on (`RtxRenderer::setResourceExpiry`), so a run whose caches pass their budget does not repeat.
