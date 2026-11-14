# The prerelease tour

On its own branch, `rtx-tour`, cut from `master`, where the review's refactoring landed (`300bc5a92c`): it
stands on the harness that left, the one `ExitStatus`, `Verb` and `VerbSet`, and a `plan<Verb>` a verb. Its one
upstream change, the log's listener chained, is entered in `.notes/UPSTREAM.md` on that branch with the change
itself, and its first tours and their figures are its own. The tour adds a verb nothing else calls, and is
reviewed, merged or dropped by itself.

A verb of the harness that flies the camera over every land cell a content names and through its most
significant interiors, in the running game, and reports every fault it met with the place it met it at: a
shader that stored a NaN, a frame that hitched, a model or a texture refused, a claim of `check` that failed, a
warning or an error in the log, a stall, a crash. Run on demand, once a content: vanilla with both expansions,
a large mod list with Tamriel Rebuilt, the PBR stack.

When it lands, each decision here folds into `docs/architecture.md` and the measurements into `docs/research`,
and this file is deleted.

## What the tree already has for it

The tour is mostly a new arrangement of parts that exist, which is what keeps it small.

- **A camera that flies through many keys**: `CameraTrack`, `CameraPath` and `Cruise`, which a film's take flies.
- **A plan made by a pure function and described before it runs**: `planFilm`, `describePlan`, `film --plan`.
- **Sixteen claims about what the renderer was handed and drew**: `Check`, asked by `check`.
- **A count of every store that was not finite, by the module that wrote it**: `NotFiniteCensus`, and the first
  such frame kept with its texel (`NotFiniteKeep`).
- **What a frame cost and why**: `FrameSamples`, `Crossings`, `Arrivals` and its worst frames, `MemoryReport`.
- **What the content could not give**: `Rtx::Refusals`, the store's `missed` and `dropped`.
- **A place as text a command can stand at**: `describeBlock`, the block Home prints.
- **A sky that crosses from one weather into another, and a clock that runs fast**: `SkyCrossing` and a
  stop's `mTurnThrough`, which `bench --turn-weather` crosses in `sTurnSeconds`, and a film's `--clock`.

What it lacks: a route made from the content, legs that follow one another without a cut, a fault tied to a
place, and the log's warnings counted.

## The tour's decisions

- **A verb of the harness, never a test of the gate.** `./omw release tour` and nothing runs it but a person:
  not `gate`, not CTest, not CI. Rare ran its core tests before every check-in and its long ones periodically
  (Masella, GDC 2019, https://gdcvault.com/play/1026366/Automated-Testing-of-Gameplay-Features); this is the
  long tier. Unreal's performance tests have the same parts: a camera on a sequence, a warm-up before figures
  are taken, a soak a subject
  (https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/AutomatedPerfTesting).
  Refused: a CTest entry, which the gate would run and which needs content a clone lacks; `bench --views=all`,
  which stands still at thirty-seven places and crosses nothing.
- **The route is made from the content that loaded, by a pure function.** `planTour(WorldMap, TourPacing)` in
  `model/`, beside `planFilm` and tested as it is; the executable reads the `WorldMap` once from the game's
  store. So Tamriel Rebuilt, or whatever a profile adds, is covered by loading it. Refused: a keys file a
  content, which a mod's release outdates; a plan made before the load, as a film's is, since the cells are not
  known then; a planning run that writes a file the flying run reads back, two loads and a file read for
  nothing.
- **Exteriors by a boustrophedon sweep, rows `2·CellGridRadius + 1` cells apart.** Back and forth along the
  rows, the canonical cover of an area (Choset and Pignon 1997,
  https://publications.ri.cmu.edu/storage/publications/pub_files/pub1/choset_howie_1997_5/choset_howie_1997_5.pdf),
  so every land cell stands in the active grid once: that is where its references, actors and scripts are made,
  and where the walk, the skinning and the lights meet them. Refused: rows a ring's reach apart, which see the
  statics of every cell and activate a third of them; a space-filling curve, which has no straight run to time a
  crossing on.
- **A leg is a run of land in a row, and the sea between two runs is a cut.** A gap of more than a row's
  spacing is not flown: the large profile's land spans 189 cells across with open sea between its provinces.
  Each leg is one stop with a `CameraTrack`; a leg that continues the last one's flight restages nothing.
- **The height is the land's own.** Each key stands a clearance above the highest ground of the cells under it,
  from the land records' bounds, and `CameraPath` splines between keys; the eye looks ahead and down. Refused: one
  altitude for a world, which `island-crossing`'s note records put frames inside the ground at 3,000 units and
  which no figure fits both an island and a mountain range.
- **Flown through a living world, unsettled, at the streaming suite's speed; coverage is counted.** 12,000
  units a second, what `island-crossing` is measured at, with the ring arriving as it does for a player
  (`benches.cfg`: a settled run puts a bake of 28 ms on the frame path). A planned cell is covered when a frame
  drew the whole world while the player stood in it (`FrameReport::isWhole`); one the flight outran is a
  finding, and `--speed` slows it. Refused: a settled
  flight, whose every hitch would be the harness's own wait; a player's walking pace, hours a content; a camera
  that waits for the world, which is no flight a player makes.
- **Interiors: a share for each content file, and within it the rooms with the most references**, decided
  2026-11-13. A file's visits are its share of sixty by the rooms it adds, never under eight nor over what it
  has: proportional allocation with a minimum a stratum, as a survey keeps every group in its sample
  (https://support.sas.com/documentation/cdl/en/statug/67523/HTML/default/statug_surveyselect_details19.htm).
  Within a file, by references, which is what a room costs and where its lamps and clutter are, at most a few a
  settlement by the prefix before a name's comma ("Vivec, Arena Pit"); `[interiors]` in `benches.cfg` adds what
  a person knows matters. At each, the eye stands where `coc` puts a player and turns once round. So the two
  expansions get eight rooms each under every content that loads them. Refused: the sixty largest of the whole
  content, under which the large list's sixtieth room has 1,072 references and `Morrowind.esm`'s largest 959, so
  the base game and both expansions get none; a fixed total with the minimum inside it, which eight files at
  eight each already pass; every room of the two expansions, 194 and some thirteen minutes; every interior,
  5,980 in the large list; a flight between a room's doors, which needs a path through walls the harness has no
  plan for (a later step, if a turn proves blind).
- **A room's references are counted by the tour's own read of the content**, decided 2026-11-13: the world-map
  reader walks each interior's references through the game's readers at the plan and keeps the count, and the
  plan prints what that took. The cell reader's storage answers for exterior squares alone. Refused: keeping the
  count upstream's start already computes and discards (`ESMStore::countAllCellRefsAndMarkKeys`), nothing read
  twice but a change in a function upstream has marked for rework; a count of distinct records a room, another
  measure behind the same private table.
- **The vanilla tour names both expansions on its own line**, decided 2026-11-13:
  `--content=Tribunal.esm --content=Bloodmoon.esm`, which the configuration appends after the profile's own
  list, so the profile and every figure measured on it stand as they are, and the report's header says what
  loaded. Solstheim is then land the sweep covers, and Mournhold rooms the share above. Refused: the two lines
  in the profile itself, which changes what `bench`, `shot` and `noise` load and what the played game loads; a
  profile of its own, a folder that repeats what the PBR stack's already names.
- **Shown in a window, as fast as the card draws; `--pace=real` holds it to the step**, decided 2026-11-13.
  `bench`'s `--window`, on by default, whose mailbox swapchain paces nothing, and the title says the leg, the
  cell and the findings so far (`StandingNote`). Unpaced, a watcher sees the flight some two and a half times
  over at the desk's frame rate, which is enough to see a black block, a hole or a flicker and is the hardest
  the streaming is driven; paced, the world's second is the wall's, a slow frame is the limiter's and is not
  reported, and a hitch still is. Home marks where the watcher is looking as a finding of its own kind, with
  its block and its picture (`HomeKey`), to go back to with `view`. Refused: real time by default, two to three
  times the wait and a gentler streaming test; a pause, a state to enter and leave where a mark and a `view`
  serve.
- **The sky turns and the clock runs all through the flight**, decided 2026-11-13. A change of weather and of
  hour is itself what a tour tests: an emitter made and freed on an ordinary frame, the lamps a
  `NightDaySwitch` lights at dusk, the eye following the light, the moons' phases. One crossing follows
  another with none standing between, `sTurnSeconds` each as `bench --turn-weather` crosses, through the ten
  weathers in an order that holds every ordered pair of them once: an Euler circuit of the complete digraph on
  the ten, ninety crossings and 360 s of world, which a vanilla tour's 380 s flies whole and the large list's
  three times. The game's clock runs at twenty-four times its own speed, a film's `--clock`, a day in two
  minutes of world: three days over vanilla, nine over the large list. Both are functions of the frames
  flown and nothing else, worked out by `planTour`, so two runs agree, `--plan` prints each leg's first hour
  and crossing, and `--from=<leg>` begins where that leg would have stood. Refused: the next of four fixed
  skies a leg, this part's first decision, which stood under four skies and crossed none; the game's rolled
  weather, which no second run repeats; a seeded draw, which covers the pairs probably where the circuit
  covers them exactly; only the weathers the region under the eye rolls, which is what a player meets there
  and leaves every pair a content's regions never roll to the mod that adds one. The cost: a sky no player
  meets at a place, snow over the Ashlands, and a day faster than any played, so a finding names the
  crossing and the hour it stood in and is stood at again under them.
- **Every fault is a finding with a place.** One record: what kind, which leg and frame, the cell, the eye and
  its look, the hour, the sky as it crossed (the weather left, the one gone to, how far), and the detail (a
  module, a texture's name, a log line). The report is the findings, the same kind and detail counted once
  with its first place, each printed as the block `describeBlock` writes, so `view`, `shot` or `bench --cell
  --pos --look --hour --turn-weather` stands there under that crossing; a hitch within a crossing's swap of
  precipitation, or at the hour the lamps light, says so and reads as the sky's. Refused: a pass or
  fail a leg, which says a row of forty cells failed; a log to read afterwards.
- **A hitch is a frame over the budget that took one and a half times the frame before it.** A slow frame is
  over the budget, one frame of the step rate; a hitch is a slow frame that also stands out from its
  neighbour, which is Unreal's definition with the budget in place of its 60 ms (`t.HitchFrameTimeThreshold`
  and `t.HitchVersusNonHitchRatio`, 1.5,
  https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FEnginePerformanceTargets), so a
  slow stretch is one hitch and not a hundred. The two tiers are the ones Android's vitals keep apart
  (https://developer.android.com/topic/performance/vitals/frozen). Each hitch carries what `WorstFrame` holds:
  the meshes that arrived, whether it crossed or rebuilt, and where the time went. Refused: Unreal's 60 ms
  itself, a different claim on every card; a multiple of the leg's median, under which a town's frames are
  hitches of the wilderness's leg.
- **The tour ends by flying its first leg again.** What a soak is read by is whether the figures come back to
  where they began once the load is the same again (https://loadster.com/guides/endurance-testing/): the
  device's heaps, the content's memory and the leg's median, first pass against last. Growth is a cost finding.
  One leg more, some thirty cells, flown under the first leg's hours and crossings again, the turn and the
  clock begun anew, so the two passes differ by the flight between them and not by the sky. Refused: a slope
  over the whole flight, which the content's own density draws.
- **The game's frames, without layers, under the counting shaders: a policy row of its own.** `VerbPolicy`
  ties the three together today: a verb that does not measure gets the counting set, the layers of a debugging
  build and the reference's radiance width, and a tour must trace what a player sees. So which shader set a
  verb runs becomes a field of the row, beside `mMeasures`, and `HarnessShaders` reads that field.
- **A leg that fails is a finding, and the tour goes on.** A stall, a pause nobody closes, a cell that cannot
  be staged: recorded, and the next leg begins. The plan is the content's, so `--from=<leg>` flies the rest
  after a crash, and the run's record, written a leg at a time, says which leg it died in.
- **`--plan` prints what a tour will fly and what it will cost, and flies nothing**, as `film --plan` does:
  the legs, their cells and lengths, the interiors chosen with their counts, the frames, and the minutes at a
  frame time the line names.
- **One profile a process; the driver runs several back to back.** `./omw release tour --config=<profile>` is
  one content. `tools/omw/tour.py` takes several, runs them one after another, never two at once since they
  share the card, and prints one table with the worst status; a Zed task a profile and one for all three, as the
  profiles are the machine's.
- **One pass, under the counting shaders, in the release flavour with no layers**, decided 2026-11-13. The
  census lives only in the counting set, and a figure taken under it is not the build's
  (`VerbPolicy::mMeasures`): NaNs are named by module, hitches are judged relatively, which holds under a
  constant overhead, every millisecond in the report is marked as the census's, and a hitch worth a number is
  measured again with `bench` at its block. Refused: two passes, the shipped set for the times, honest figures
  at twice the time and two flights whose streaming does not line up; the shipped set alone with the frame
  searched for a NaN, which finds one only after a history spread it and names no module.
- **The log's warnings and errors are counted through its listener, chained**, decided 2026-11-13, an upstream
  change to record in `.notes/UPSTREAM.md`: `Debug::setLogListener` holds one listener, which the game's debug
  window takes, so it returns the listener it replaced, as `std::set_terminate` returns the handler, and the
  tour's calls it. Counted in memory by distinct message and by leg, in a bounded table. The cost is a conflict
  with an upstream edit to those lines of `components/debug/debugging.{hpp,cpp}`, a file the fork already
  changes. Refused: the log file read after the run, a file written and read back that counts nothing when the
  process dies.
- **Faults fail the run, costs are reported**, decided 2026-11-13. A NaN, a refusal, a failed check, an error
  in the log, an outrun cell, a stall or a crash sets the exit status; hitches, slow frames, warnings and
  memory are reported and ranked, each with its place, and `--fail-on=<kind>` adds a cost for a run that should
  hold it. Refused: everything fails, under which a large mod list's own warnings fail every tour; nothing
  fails but a crash, under which a NaN has to be noticed by reading.

## The tour's open questions

- **The speed, and the sixty.** 12,000 a second and a share of sixty give the tables below. Half the speed or
  twice the rooms is a line's option; the defaults are what a person waits for, and the first tours set them.
- **How fast the sky and the day turn.** Four seconds a crossing with no weather standing between, and a day
  in two minutes, are a first setting: `--crossing`, `--weather-hold` and `--clock` are a line's options as a
  film's are, a hold doubles the world a circuit takes, and the first tours say whether a weather must stand
  for its own faults to show.
- **A map of the flight.** The report as a picture: a square a cell, coloured by its median frame, marked
  where a finding stands, written beside the report through the PNG writer the harness has. A frame-rate heat
  map from a box run through the whole map is what one studio's tests draw
  (https://realtimevfx.com/t/gdc-2023-vfx-roundtable-discussion-notes/23367, a roundtable note, the studio
  unnamed). Cheap once findings carry their cell; left out of the first version unless asked for.
- **Whether a tour after a shader change waits for its pipelines.** The counting set's cache is its own, and a
  first flight that compiles as it goes reports the compiles as hitches; to be read in how `bench`'s warm-up
  leg and the kernel compile's job meet, before the first tour is believed.

## What a tour is made of

Counted from the three profiles' content files on 2026-11-13: records read by header, a later file's cell
replacing an earlier one's.

| content | land cells | extent in cells | legs | cells flown | interiors | rooms visited |
|---|---|---|---|---|---|---|
| vanilla with both expansions | 1,540 | 52 × 48 | 19 | 556 | 1,328 | 67 |
| the large mod list with Tamriel Rebuilt | 4,725 | 189 × 93 | 56 | 1,689 | 5,980 | 93 |
| the PBR stack | 1,540 | 52 × 48 | 19 | 556 | 1,328 | 67 |

`Morrowind.esm` alone is 1,390 land cells, 16 legs and 1,134 interiors; Bloodmoon adds Solstheim's 150 land
cells and Tribunal none. One of the large list's eleven content files is a script list and holds no cell.

The interiors each file adds, and the visits the rule gives it:

| content file | interiors | most references | visits, vanilla | visits, the large list |
|---|---|---|---|---|
| `Morrowind.esm` | 1,134 | 959 | 51 | 11 |
| `Tribunal.esm` | 96 | 393 | 8 | 8 |
| `Bloodmoon.esm` | 98 | 434 | 8 | 8 |
| Tamriel Rebuilt's mainland | 3,390 | 1,567 | | 34 |
| the Cyrodiil and Skyrim provinces | 504 and 349 | 1,482 and 2,193 | | 8 and 8 |
| the repack's own file | 396 | 2,288 | | 8 |
| the shared data file | 13 | 763 | | 8 |

A cell is 8,192 units, so at 12,000 units a second and sixty steps a second a cell is 41 frames. What follows
is arithmetic, not a measurement: the frame times are assumed from the bench places' medians on the desk
(6.0 ms at `seyda-neen-ship`, `research/2026-11-10-staged-host-writes-cost.md`), the counting shaders are
slower by an amount nobody has measured, and a cut or a room costs a load whose length is the content's.

| content | frames flown | at 7 ms | at 12 ms | its rooms at about 4 s | held to the step |
|---|---|---|---|---|---|
| vanilla with both expansions | 22,800 | 2.7 min | 4.6 min | 4.5 min | 6.3 min |
| the large mod list | 69,200 | 8.1 min | 13.8 min | 6.2 min | 19.2 min |
| the PBR stack | 22,800 | 2.7 min | 4.6 min | 4.5 min | 6.3 min |

So three contents are somewhere between half an hour and three quarters on the desk, before the loads, and
about an hour with every flight held to the step. `--plan` replaces this table with the content's own figures,
and the first run with a measurement.

## The tour's shape

- `model/tour.{hpp,cpp}` — `WorldMap` (land cells with their height bounds, interiors with their counts),
  `TourPacing` (the speed, the clock's rate, the crossing), `TourPlan`, `planTour`, `describePlan`, `stopsFor`.
  No game header; tested with hand-made maps: an empty map, one cell, two islands a sea apart, a row with a
  gap at the limit and one past it; and the circuit of weathers, which holds each of the ninety ordered pairs
  once and ends where it began.
- `model/finding.{hpp,cpp}` — `Finding`, its kinds as an enum, the table that counts one kind and detail once,
  with its bound. Tested for the count, the first place and the overflow.
- `worldmap.{hpp,cpp}` in the executable — the one place that reads the game's store into a `WorldMap`: the
  land cells and their bounds, and each interior with the file that first names it and its references, counted
  by a walk of its own through the game's readers.
- `tourwatch.{hpp,cpp}` in the executable — what a frame's report, the census's and the log's listener hand it,
  turned into findings; the only new thing that runs each frame, and it allocates nothing there.
- `Session` takes a tour's stops where the world first stands, the moment it already reads the game's
  timescale, and a stop whose stand names no cell begins measuring at its first frame.
- `tools/omw/tour.py`, and the tasks in `.zed/tasks.json`.

## The tour's steps

On `rtx-tour`. Whoever addresses an item deletes it.

- [ ] `model/tour`: `WorldMap`, `planTour` for exteriors (rows, legs, heights, keys), `describePlan`; tests.
- [ ] `worldmap`: the store read into a `WorldMap`; `Verb::Tour`, its row in `verbs.cpp` and its options, and
  the tour's stops from its plan (`stopsFor`, beside the film's); `tour --plan` prints the plan and its cost at
  the frame time the line names.
- [ ] `Session`: stops taken where the world stands; a leg that continues a flight; a failed leg recorded.
- [ ] `model/finding` and `tourwatch`: not-finite stores by module with the kept frame's place; slow frames and
  hitches with `WorstFrame`'s spend; refusals; the checks a leg can answer, asked at its end; outrun cells.
- [ ] The policy row: the shader set a field of its own, the tour's frames at the game's width with no layers.
- [ ] Interiors: the counts and the files in the `WorldMap`, the share a file, the turn on the spot; a test
  that a file of few rooms keeps its eight and one of fewer than eight gives all it has.
- [ ] The window: `--window` and `--pace=real`, the title's note, Home's mark as a finding.
- [ ] The closing leg, and the first pass against the last as a finding.
- [ ] The turning sky and the running clock: the circuit of weathers and the clock's rate in `TourPacing`, each
  leg's first hour and crossing from the plan, the closing leg under the first leg's; a finding carries its
  crossing and hour; `--speed`, `--crossing`, `--weather-hold` and `--clock` on the line, as a film's are. Tests: a leg begins under the same hour and crossing whichever leg the run began from,
  and two plans of one map agree frame for frame.
- [ ] The log's warnings and errors, by the route decided above.
- [ ] The report: findings as blocks, the table a leg, memory at the first leg and the last; the record a leg
  at a time; `--from`; the exit status the faults set (`ExitStatus::Failed`) and `--fail-on=<kind>`.
- [ ] `tools/omw/tour.py`, the tasks (the vanilla one naming both expansions), and a line in `AGENTS.md`'s
  verification list saying when a tour is run.
- [ ] The first tours of the three contents: whether a tour after a shader change waits for its pipelines
  read first, the speed, the share of sixty and the sky's pace set by what they show, their figures to
  `docs/research`, and this file folded away.
