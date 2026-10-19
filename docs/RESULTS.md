# Test results

Device: Fire TV Stick 4K (AFTKM), Fire OS 8 (Android 11, sdk 30), Wi-Fi. App: Prime Video 6.23.23
(Morphe patches 1.40.5), content with ads included in the subscription (France).

| Date (2026) | Title | Breaks | Ad responses emptied | Ad on screen |
|---|---|---|---|---|
| 19 Oct | Carrie S1E1 (pre-roll, ~11 min, ~33 min) | 3 | 3 / 3 | none |
| 19 Oct | Carrie S1E2 (pre-roll, ~8 min) | 2 | 2 / 2 | none |
| 19 Oct | Twilight | pre-roll | 1 / 1 | none |
| 19 Oct | The Hunger Games (pre-roll, ~51 min, +1) | 3 | 3 / 3 | none |
| 19 Oct | Carrie S1E2 rapid scrubbing across a break | 1 | 1 / 1 | none |

Chunks per response: 2 to 6 (17 to 70 KB of decompressed JSON). Before the inflate hook, the `memcpy`-level approach
emptied 6 of 22 breaks in a 4-day log.

Not explained: one ad (a French newspaper spot) seen on screen that afternoon with no hook log line. The screenshot was
1920×810, the display mode of another app (SmartTube) that had been opened by accident; it is most likely not Prime.
Never reproduced.

Raw numbers come from the hook's own log (`AD-GATED` / `AD-KILLED`). Please add yours.
