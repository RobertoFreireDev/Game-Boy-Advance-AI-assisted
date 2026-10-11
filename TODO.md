1 - Fix use Hold A issue

Issue: when user hold A, it uses move 1 first if charged and then next, it start to charging the move 3

Requirement:

- Remove the logic to hold A to use move 3. Instead use button R1
- Rule for A vs. hold-A. At zero seconds (no delay) a small ring appears around the player

Update monster-survival.md

2 - Eletric monster move 1 makes too easy

- Add a penalty. When user uses move 1, Eletric monster doesn't move for 0.5 seconds

Update monster-survival.md