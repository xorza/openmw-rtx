# Check on Windows: a fail-fast crash leaves a dump through the WER module

This check is for an agent on a Windows machine. Nothing on the Linux machine can run it.
Report the result to the user, with the evidence that each step asks for.

## What to check, and why

A fail-fast ends the process before any handler in the game runs. Examples are a failed `/GS`
security check, a corrupted heap, and `__fastfail`. Before commit `7e8413354c`, such a crash left
no dump. The monitor logged only "The game ended with STATUS_STACK_BUFFER_OVERRUN and left no dump".

Commit `7e8413354c` on branch `finish-redesign` adds these things:

- The build makes Crashpad's WER module as `openmw-wer.dll`, in the same folder as the
  executables (`components/crashcatcher/link.cmake`). The install's `*.dll` rule puts it in the
  package.
- At start, the Windows client (`Client::catchPastTheProcess` in
  `components/crashcatcher/crashpadclientwin32.cpp`) does four things:
  1. It writes a `REG_DWORD` value under
     `HKEY_CURRENT_USER\Software\Microsoft\Windows\Windows Error Reporting\RuntimeExceptionHelperModules`.
     The value's name is the full path of the `openmw-wer.dll` beside the executable.
  2. It deletes the values in that key whose file name is `openmw-wer.dll` and whose file no longer
     exists: the copies of the game that moved or were deleted.
  3. It calls `CrashpadClient::RegisterWerModule` with the same path.
  4. If a step fails, the game log has the warning `The crash catcher goes without a fail-fast's
     dump: ...` and the reason.
- When a fail-fast occurs, the WER service loads the module. The module asks the crash monitor for
  a dump, so the result is a normal crash report.
- In the crash matrix, the `fast-fail` mode now expects a crash summary that names
  `STATUS_STACK_BUFFER_OVERRUN`, and a dump (`apps/components_tests/crashcatcher/crashtestswin32.cpp`).

## Before you start

1. Get the branch: `git fetch origin` and `git checkout finish-redesign`. It must contain
   `7e8413354c` (`git log --oneline -1 7e8413354c`).
2. Make sure that WER can run. Each of these stops the check, so record the values:
   - The Windows build is 19041 or later (`winver`). The module does nothing on an older build.
   - The WER service exists: `Get-Service WerSvc` in PowerShell. Its start type is usually
     "Manual (trigger start)". Do not change it.
   - WER is not turned off: `HKLM\Software\Microsoft\Windows\Windows Error Reporting\Disabled` and
     the same value under `HKCU` are missing or `0`.
3. Build: `omw.cmd build` from the repository root. On a new machine, run `omw.cmd bootstrap`
   first, for the pinned Vulkan SDK. `omw.cmd help` lists the verbs.

## Step 1: the crash matrix in the build folder

1. Run `omw.cmd test -R crash.matrix --output-on-failure`.
2. The test must pass. If it fails, record the full output.
3. Open the mode's folder, `build-debug\test-output\crash-matrix\fast-fail\`. Record these:
   - the last lines of `crash-tests.log`. A summary line must start with `Crash: ` and name
     `STATUS_STACK_BUFFER_OVERRUN`;
   - the `.dmp` files under `user data\crashes\reports\`. There must be one;
   - `stderr.txt`, if it is not empty.
4. Read the registry key from the section above:
   `reg query "HKCU\Software\Microsoft\Windows\Windows Error Reporting\RuntimeExceptionHelperModules"`.
   One value must be the full path of the `openmw-wer.dll` beside the build's `crash-tests.exe`
   (`dir /s /b build-debug\crash-tests.exe` finds the folder). Record the output.

## Step 2: the packaged game

1. Make the package: `omw.cmd archive wer-check`. It writes a portable folder into `dist\`.
2. Make sure that `openmw-wer.dll` is in that folder, beside `openmw.exe`. Record the listing.
3. The packaged game has no command that causes a fail-fast. So copy the `package` flavour's
   `crash-tests.exe` into the package folder (`dir /s /b build-package\crash-tests.exe` finds it).
   There it uses the package's DLLs and the package's `openmw-wer.dll`.
4. From the package folder, run `crash-tests.exe fast-fail C:\wer-check\run1`. Use an empty folder
   that does not exist yet. The process must end with a non-zero exit code.
5. Record these:
   - `C:\wer-check\run1\crash-tests.log`, all of it. It must have a `Crash: ` summary that names
     `STATUS_STACK_BUFFER_OVERRUN`, and a `Crash package:` line. It must not have the line "left
     no dump";
   - the dump under `C:\wer-check\run1\user data\crashes\reports\`, and the `.zip` package that the
     `Crash package:` line names.
6. Run `reg query` from Step 1 again. A value must now name the package folder's `openmw-wer.dll`.
   The value from Step 1 must still be there, because that file still exists.
7. Start the packaged `openmw.exe` once, then quit it from the menu. Its log
   (`%USERPROFILE%\Documents\My Games\OpenMW\openmw.log`) must not have the warning `The crash
   catcher goes without`.

## Step 3: a copy that moved

1. Rename the package folder from Step 2, for example add `-moved` to its name.
2. Run `crash-tests.exe fast-fail C:\wer-check\run2` from the renamed folder.
3. Run `reg query` again. The value for the old folder name must be gone, because its file no
   longer exists. A value for the new folder name must be there. The value from Step 1 must still
   be there.

## What to report

For each step: passed or failed, and the evidence the step asks for. If a step fails, record the
exact output and stop. Do not change the code. If a cause is clear, describe it, with the file and
the line.

After the check, you can remove the test values: delete the values that name `wer-check` folders
from the key. Remove the value from Step 1 only if the user agrees.
