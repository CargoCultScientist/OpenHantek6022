# OpenHantek Lab capture JSON v1

Extension: `.ohl.json`. UTF-8 JSON, root object:

| Field | Meaning |
| --- | --- |
| `format` | Exactly `OpenHantekLabCapture` |
| `version` | Integer `1`; unknown versions are rejected |
| `tag` | Unsigned 32-bit acquisition sequence number, not globally unique |
| `capturedAtMs` | Decimal string: milliseconds since Unix epoch at block transfer completion; not a synchronized sample clock |
| `triggerPosition` | Zero-based trigger sample index; 0 when untriggered |
| `triggered` | Whether this record triggered |
| `channels` | One to three channel objects in acquisition order, including empty/disabled channels |
| `metadata` | Descriptive device and UI settings; never applied to hardware when loading |

Channel objects contain `name` (up to 256 characters), `unit` (1 = V, 7 = W,
8 = V²), `interval` (positive seconds between samples, or 0 for an empty channel),
`valid` (false for clipped/invalid channels), and `samples` (finite JSON numbers).
Time is `i * interval` from record start, or `(i - triggerPosition) * interval`
when both comparison records are trigger-aligned.

Samples are calibrated, probe-scaled floating-point values, not ADC bytes. JSON
double precision is retained. No spectrum, rendered geometry, or live statistics
are stored: measurements are recomputed on load. UI settings in `metadata` describe
the state at GUI receipt; they are explicitly **not** a synchronized capture-settings
snapshot. Calibration coefficients are not embedded. Do not use this format to
reconstruct raw ADC readings, change calibration, or prove continuous acquisition.

Limits: 32 MiB file, one million samples across all channels, at most three channels.
Loading rejects invalid/non-finite values, unsupported units/schema, invalid time
intervals and trigger positions outside non-empty channels. Saving is atomic using
QSaveFile. No executable content, external URLs, extraction, or settings application.

This format is intentionally separate from the roadmap's future `.ohcap` ZIP/NumPy
format. It can be read with Python's standard `json` module; no plugin is needed.
