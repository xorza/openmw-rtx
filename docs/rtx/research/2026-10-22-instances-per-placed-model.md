# Instances per placed model, and what the top level costs — 2026-10-22

RTX 4090 Laptop, driver 615.78.08, Linux, release build at `956f8730c8`. Two readings that bound
what one bottom level per template model, a geometry per shape, could give before a prototype is
made of it.

**How many instances a placed model makes.** A temporary count in `CellPlacer`, over a bench of
`vivec,balmora,seyda-neen-shore,ald-ruhn`: 983 held cells placed 291,137 instances for 88,034
references, 3.3 shapes a reference. A bottom level per model would place one instance where the
mirror places 3.3, so the exteriors' 55,000 to 80,000 instances would come to about 17,000 to 24,000.

**What the top level costs a frame** (`.notes/perf/duplicate-ab/a1.json`, frame medians):

| place | instances | frame | top level | refit | trace |
|---|---|---|---|---|---|
| seyda-neen-shore | 80,370 | 5.47 ms | 0.461 ms | 0.143 ms | 0.721 ms |
| balmora | 75,177 | 5.34 ms | 0.466 ms | 0.195 ms | 0.531 ms |
| vivec | 61,632 | 6.71 ms | 0.433 ms | 0.303 ms | 0.567 ms |
| ald-ruhn | 55,073 | 5.24 ms | 0.414 ms | 0.200 ms | 0.530 ms |
| balmora-mages-guild | 1,221 | 5.65 ms | 0.217 ms | 0.181 ms | 0.356 ms |

The top level's build stands at about 0.2 ms with 1,221 instances and about 0.45 ms with 55,000 to
80,000, so the instances' own share is about 0.25 ms. A third of the instances would save about
0.17 ms of it, 3% of an exterior's frame. What the trace would gain from bottom levels that overlap
less is the figure only a prototype gives.

**Against it**: a shape's own visibility gate and a `NightDaySwitch` branch, which today drop or
stand one instance, would become a geometry the trace skips by its index, and a model whose shapes
a script changes would build its bottom level again.
