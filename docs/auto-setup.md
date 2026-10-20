# Auto setup and live signal hints

Run the scope in time-domain **AUTO or NORMAL** trigger mode, enable the voltage
channels you want to inspect, then press **Auto setup** in the acquisition bar.
This is separate from AUTO *trigger mode*.

Auto waits for two distinct, fresh live acquisitions started after the request.
It discards the first block for settling and analyzes the second. A request
times out after 2.5 seconds without changing settings. NORMAL mode with no
trigger may keep returning a held trace: select AUTO trigger mode and retry.
Stopped, SINGLE, ROLL, XY, calibration and manual-command modes are excluded.
Manual setup changes, Stop and disconnect cancel a pending request.

The initial implementation:

- Fits enabled physical voltage channels to roughly six divisions, respecting
  the configured probe factor and DC headroom, and centers them on screen.
- Widens a clipped channel by one range per click; it does not infer amplitude
  or timing from clipped samples. Inspect fresh data and repeat if needed.
- Chooses the largest relative, repeatable signal for a rising-edge midpoint
  trigger and AUTO trigger mode. At least two complete stable cycles are required.
- Selects a 1–2–5 timebase rounded upward from three periods across the screen
  (typically three to 7.5 periods). The normal controller selects the sample rate
  when the timebase changes.
- Leaves timebase/trigger unchanged when timing is unreliable; a vertical fit
  can still be applied. Flat/very small signals are not magnified automatically.
- Offers **Undo Auto** until another manual setup/acquisition change. Undo
  restores the previous range, offsets, trigger and timebase/sample rate.

Hints describe clipping, little variation (DC or a missing/small signal), small
display amplitude, an out-of-range trigger, irregular crossings, too few cycles
and insufficient sample/edge resolution. They refer to **live incoming inputs**,
not the selected history/library capture. The banner is debounced over three
observations at most four times per second and expires when live data is stale.
Hints never change settings; use Auto explicitly.

Probe attenuation, coupling, channel enablement, calibration, math, saved
captures, references and recording controls are not modified. Software range
selection does not change the instrument's safe input ratings. Sampled data can
look stable while aliased; neither Auto nor the hints can rule that out.

## Scope and validation

This is a conservative fresh-frame fit, **not** a complete unattended range or
frequency sweep. It does not enable disabled channels, search all sample rates,
identify PAL/NTSC/S-video, diagnose probe wiring, or confirm its result in a
multi-pass feedback loop. Composite video may produce irregular timing hints;
use the established manual video settings in that case.

Synthetic regression tests cover sine/DC/clipping/noise/undersampling, probe
factors, untouched disabled channels, no mutation during analysis, held-frame
identity through triggering/postprocessing, stale/duplicate rejection, undo,
manual cancellation, timeout, Stop and disconnect. Hardware accuracy, analog
settling across every range and physical video signals still need bench testing.
The acquisition fence is specific to this assistant; it does not complete the
roadmap's immutable-settings/race-free-pipeline redesign or general undo system.
