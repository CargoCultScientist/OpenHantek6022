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

Channel objects contain `name` (up to 256 characters), `unit` (0 = dimensionless, 1 = V, 7 = W,
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

## Library bundles v1

A library is an ordinary folder of immediate subfolders named
`capture-<lowercase UUID without braces>`. Each contains:

```text
capture-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx/
  capture.ohl.json  # unchanged capture v1 format above
  entry.json        # description, summary and checksum
```

The UTF-8 manifest has `format: "OpenHantekLabLibraryEntry"`, integer `version: 1`,
`id` matching the folder name, `name`, `notes`, `tags` (string array), `capturedAtMs`
and `savedAtMs` (non-negative decimal millisecond strings), `tag` (unsigned 32-bit
integer), `channels` (one to three channel-name strings), `samples` (total across
channels, 1–1,000,000) and `sha256` (64 lowercase hex characters over the exact bytes
of `capture.ohl.json`). Description limits are 120 characters for a nonempty
single-line name, 4096 for notes, 16 single-line/comma-free tags of 32 characters.
Whitespace surrounding names/tags is trimmed; duplicate tags ignore case. The hash
does not authenticate a source: someone who edits both files can recompute it.

Names and tags are labels, never paths. Only the fixed filenames are opened.
Entry directories, manifests and payloads cannot be symlinks. Metadata reads are
bounded to 64 KiB; payload reads to 32 MiB. Payload bytes are verified and decoded
from the same bounded buffer, then compared against the tag, timestamp, channel
names and sample count in the manifest. Unknown manifest versions are skipped,
reported and never rewritten. Unknown fields on supported manifests are preserved
when editing descriptions. The independent capture JSON format is not changed.

Writers cooperate through `.library.lock`. New entries are written to an owned
`.pending-*` directory inside the library, validated, then published using a
same-parent directory rename to a fresh UUID. Failed saves clean up only their own
temporary directory. A process crash may leave that hidden directory; scanning
ignores it. Metadata edits atomically replace `entry.json` and compare its previous
SHA-256 revision first to reject stale edits. No waveform rewrite or automatic
settings restoration occurs. These checks assume trusted local storage, not an
adversarial process racing filesystem operations or cross-host distributed locking.
