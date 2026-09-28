# Open issues

The sky's guide values differ from the DLSS-RR guide. We write diffuse 0 and roughness 1. The guide suggests diffuse 0.5 and roughness 0. A change needs a measurement with Ray Reconstruction.
A far-away normal map does not make the roughness wider (Toksvig), so distant glossy mapped surfaces can sparkle.
Reflection cones from curved glossy surfaces do not get wider with the curvature.
The shaft-aim change (6) and the debug-line change (7) have no direct test. The shaft bias only shows through the statistics of the aim, and the line change only shows at exact pixel boundaries.
