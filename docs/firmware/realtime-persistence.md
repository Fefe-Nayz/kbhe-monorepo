# Real-time scan and persistence

The STM32F723 used by the keyboard has a single internal Flash bank. A sector
erase stalls instruction fetch for much longer than the 125 us period of an
8 kHz scan, even when the HAL erase API is entered in interrupt mode. Runtime
code must therefore never start a physical sector erase.

There is also a smaller, irreducible limit: DS11853 table 53 specifies up to
100 us for one 32-bit Flash program. Programming starts just after the next ADC
DMA scan has been armed, so conversion and programming overlap, but only 25 us
of a 125 us deadline remain in the silicon worst case for input processing,
interrupts and scheduler overhead. Consequently, the internal-Flash design
can minimize and measure disruption, but it cannot honestly prove a hard
"above 8 kHz under every voltage/temperature/workload condition" guarantee.

The persistence path uses these rules:

- sectors 6 and 7 are a power-loss-safe A/B journal;
- the inactive 128 KiB sector is erased only during boot, before USB and ADC
  scan startup;
- settings/profile snapshots, payload CRCs, journal CRCs, compaction copies and
  Flash programs are incremental;
- the 476-byte global SETTINGS object contains only calibration, options and
  profile metadata; each profile's settings and actions are one atomic
  `ProfileDocument`;
- schema-3 `ProfileDocument`s use a bounded deterministic LZ stream. Schema-2
  13,279-byte raw documents remain readable and migrate on their next save;
- settings and profiles share one explicitly owned async writer, and a caller
  can neither advance nor consume another caller's transaction;
- the main loop grants one global writer slice, and therefore at most one
  32-bit Flash-program attempt, per completed scan;
- concurrent macros share a round-robin budget of 32 action steps per scan
  (instead of multiplying an eight-step allowance by every active instance);
- Flash is unlocked only around that single synchronous word program and is
  locked again before control returns to the scheduler;
- a bank switch writes its commit word last and never erases the old committed
  bank at runtime;
- after a runtime bank switch, a second compaction is deferred until the next
  reboot prepares the spare sector. Normal appends remain available while the
  active bank has space.

This trades end-to-end save latency for bounded per-scan disturbance. The
global payload is 476 bytes (516 bytes including its journal record). A raw
profile payload is 13,279 bytes (13,320-byte record), while the representative
compressed stress fixture is 1.8--2.6 KiB. At one word per 8 kHz scan, the
Flash-program portion is therefore about 16 ms for global metadata, about
60--85 ms for that compressed fixture, and about 416 ms for incompressible raw
profile data; bounded copy, validation, hashing and compression slices add
latency without adding another Flash word to a scan. HID responses that require
durability are held until their transaction completes, repeated settings edits
are coalesced, and lost-response retry matching reads at most 32 stored bytes
per scheduler invocation.

Capacity is tested rather than assumed. With one compact global record and four
incompressible raw documents, exactly 11 same-profile replacements fit across
the prepared A/B banks before `NO_SPACE`; one GC occurs and no runtime erase is
attempted. This is an explicit physical limit, not a silently dropped save. A
representative compressed four-profile fixture completes 100 mutations, one
power-loss-safe GC and a reboot round-trip with zero runtime erases and zero
`NO_SPACE` results. After the only prepared spare bank has been consumed, a
reboot is required before another GC can be performed safely.

`CMD_GET_MCU_METRICS` exposes the lifetime maximum and p99 scan intervals, the
lifetime count of intervals at or above 125 us, and Flash
word/step/GC/boot-erase counters. It also exposes the last detailed Flash status
(`NO_SPACE` is value 5), the lifetime deferred-`NO_SPACE` count, async ownership
state and spare-bank readiness. The p99 is the conservative upper edge of a
4-us lifetime histogram bucket (values above 1,020 us share its last bucket).
These are the acceptance metrics for hardware testing, not a replacement for
the datasheet bound.
`flash_max_words_per_step` must remain at most one in the operational firmware
and `runtime_erase_count` must remain zero.

Detailed analog timing is deliberately amortized. While a diagnostic session
is active, the firmware profiles one rotating logical key per scan and
publishes `analog_*_us` only after a complete 82-scan sweep. The previous
complete sweep remains visible while the next one is collected, and disabling
diagnostics discards any partial sweep. This caps the profiler at 11 DWT reads
per scan; the former all-key burst performed 902 DWT reads in one scan every 32
scans and could itself increment `scan_deadline_miss_count`. Consequently, HIL
acceptance must use firmware with the amortized profiler before attributing a
deadline delta to USB, RGB or persistence workload.

Hardware-in-the-loop testing must exercise saves and GC across voltage and
temperature while checking both deadline metrics. If an absolute guarantee is
mandatory, the safe policies are either to prohibit internal-Flash programming
while the 8 kHz loop is active, or to move persistence to external FRAM/MRAM.
Executing only the HAL primitive from RAM is insufficient: every interrupt,
scan, trigger and HID path that may run while the single Flash bank is busy
would also have to be closed over into SRAM and then re-qualified against the
remaining 25 us budget.

USB re-enumeration advances
`disconnect -> 120 ms deadline -> deinit -> 120 ms deadline -> reinit` from the
main loop, so ADC scans continue during both host-visible settling periods.
There is nevertheless a known hard-real-time exception at the final step:
TinyUSB's STM32F7 embedded-HS-PHY bring-up currently calls the platform delay
hook for about 2 ms from inside `tusb_init()`. The command therefore cannot yet
claim the 125 us deadline; removing that pause requires splitting the PHY/core
bring-up into an explicitly qualified asynchronous state machine. The separate
20 ms delay in `updater_app.c` is reachable only after ADC/DMA and timers have
been stopped for an immediate reset or updater handoff.

The ADC DMA half-transfer interrupt is disabled after every DMA start (including
watchdog recovery), because the scanner consumes only complete mux rows. This
removes eight empty half-transfer IRQs per full scan, or about 64,000 IRQ/s at
8 kHz, without changing the captured samples.

The WS2812 streamer is also quiescent between complete frames. Once the reset
latch has completed, TIM2 and its DMA request are gated while the already-armed
circular transfer is retained; `ws2812_show()` resumes it atomically when a
complete pending frame is available. This removes the former 30 us idle
half-buffer interrupt cadence (about 33,333 IRQ/s) without changing the active
800 kHz waveform. Input/ADC interrupts use NVIC priority 0, LED DMA priority 1
and USB priority 2, so best-effort lighting and control traffic cannot preempt
the input pipeline.

The Pixel Flow effect caches its immutable nearest-upstream LED map and rebuilds
it only when angle or direction changes. Its steady-state render path therefore
no longer repeats an 82-by-82 geometry search on every animation frame.
