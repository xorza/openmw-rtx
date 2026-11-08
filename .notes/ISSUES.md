# Open issues

- On the Fullrest Repack, `bench --suite=streaming` draws frames of 396 to 451 ms whose report gives each 21.9 to 24.2 ms of the game's update and under 3 ms of every other zone: some 370 ms of each such frame is in no zone the report names. The PBR profiles' worst frames on the same route are 76 and 268 ms, the second with 5.2 ms of update.
