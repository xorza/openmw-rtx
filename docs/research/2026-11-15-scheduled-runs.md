# When GitHub starts a scheduled run — 2026-11-15

Every `schedule` run of this repository's workflows from 2026-10-19 to 2026-11-15, from
`gh api "repos/<repository>/actions/runs?event=schedule"`: the time the cron names, in UTC, against the time the
run was created. Each cron named minute nought of its hour, which GitHub's documentation calls the scheduler's
busiest ("Events that trigger workflows", `schedule`: a run may be delayed, and under enough load dropped).

## Upstream merge, once a day

| days | cron | created, earliest to latest | late by |
|---|---|---|---|
| 10-19 to 10-26 | `0 4 * * *` | 08:28 to 09:27 | 4 h 28 min to 5 h 27 min |
| 10-27 to 11-04 | `0 4 * * *` | 09:49 to 12:26 | 5 h 49 min to 8 h 26 min |
| 11-05 to 11-12 | `0 5 * * *` | 11:24 to 13:28 | 6 h 24 min to 8 h 28 min |
| 11-13, 11-14 | `0 5 * * *` | never | dropped |

The workflow file parsed at every commit of those two days, with the same cron, and no run of any event was
created for it on either.

## Auto review, once an hour

`17 * * * *`, off the top of the hour, added on 11-15 at 14:58. No scheduled run by 17:24: three ticks, none started.
Its dispatched runs, which a new item starts within the minute, are the only ones it has had.

## What follows

- Minute nought was late by hours on every day measured, and later as the month went on.
- A cron off the top of the hour had not started either in its first two and a half hours, so the minute alone does
  not explain the delay: the measurement of `23 0 * * *` is the runs after this date.
- A sweep that the schedule starts cannot be what bounds a wait. The auto review's debounce of five hours is five
  hours and however late its hourly run is.
