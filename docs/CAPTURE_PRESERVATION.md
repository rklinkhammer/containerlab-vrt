# Diagnostic capture preservation

The generated recorder node continuously counts mirrored Ethernet frames. It does not continuously record PCAP files. `sudo bash scripts/capture.sh` starts a separate finite recorder process: at most 60 seconds, 64 MiB, all frames on eth1. No automatic resume or stitching is implied.

Each call creates an exclusive session directory under `artifacts/runtime/captures/` on the Linux guest host. It prints the directory immediately and writes:

- `manifest.json`: `recorder.capture/1`, original container ID, start/finish UTC timestamps, duration/byte limits, completed/incomplete status, copied file hashes and lengths.
- `capture.pcap`, `metrics.json` when copying is possible.
- `telemetry.jsonl`, `diagnostic.log` after normal command return, including unsuccessful return.

Capture, retrieval and scoped cleanup use the enrolled container ID, never a newly replaced same-name container. Each remote temporary directory is unique. Successful copies are retained on the host; completed remote files are removed. Failed attempts leave any available remote partials for diagnosis, with their execution still bounded by timeout. Container deletion can make those partials unrecoverable. A caller interruption can leave the bounded remote process running until its 65-second timeout; the copied partial is incomplete and not an immutable snapshot of ongoing remote writes.

`completed` requires successful capture exit, stopped metrics, zero PCAP I/O errors, copied PCAP size matching final metrics and the byte limit. It is not an independent structural PCAP validator. Qualification independently checks every record boundary. Kernel drops, receive truncations and size-limit termination must be assessed separately. Flush is not fsync; `durable_commit` is `not_measured`.

The C++ writer creates new files exclusively, refusing existing paths and symlinks. It does not truncate or append previous captures. Use a new destination for every invocation. Partial write errors can leave an incomplete file; never treat it as a finalized capture merely because a file exists.

Before replacing a recorder, finish/copy any required diagnostic capture. After replacement, explicitly start a new capture session. Retain both manifests and hashes, and report the interval between them as unrecorded. The interval is not a measured packet-loss count. There is no continuous recorder, automatic rotation or retention deletion policy in this slice; operators must budget host storage and remove only reviewed sessions. Copy evidence from the dedicated Linux VM to the Mac before deleting its disk; see MACOS.md.
