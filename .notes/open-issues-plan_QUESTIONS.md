# Open issues plan: checks left to the user

No question is open. Item 3 tests on a hidden window on the session's display, and item 5 leaves a
settings file from before the change as it is.

## Item 5: two displays at different scales

The check needs the internal eDP enabled next to the G9 (`kscreen-doctor output.eDP-1.enable`),
which changes the desktop and waits for the user's approval: start windowed on each display three
times, and the stored size must not change.

## Item 4: the check in the real game

`components-tests` covers the rule. The check in the game is left to the user: minimize
`./omw game` through KWin and read `nvidia-smi --query-gpu=utilization.gpu --format=csv -l 1`; the
use must fall to the desktop's own, and come back on restore.
