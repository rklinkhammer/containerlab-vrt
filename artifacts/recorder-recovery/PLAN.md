# Recorder replacement and capture preservation

Written before execution. Scope: pinned ARM64 images in a new dedicated VM; native filtered recorder destruction and full-topology deployment. No other existing VM access.

Independent expectations:
- Existing capture bytes must never be overwritten, including symlink targets. Invalid limits must not create files.
- Each bounded capture owns a unique session directory, exact recorder container identity, PCAP, metrics and manifest. Completed, failed and interrupted attempts remain distinct. Files copied to the guest host survive container replacement; container writable state is not persistent storage.
- A completed capture parses as PCAP 2.4 Ethernet: each record length within snaplen/original length, timestamps valid, no partial tail, total bytes within 64 MiB, record counts/bytes match final metrics. Flush is not fsync/durable commitment.
- Initial traffic includes all configured tones. Recorder replacement changes only recorder container/boot, restores eth1 UP/MTU and advancing mirrored-frame counts within 90 seconds, followed by 30 seconds sustained traffic. Other seven identities remain unchanged.
- Capture before and after replacement; preserve first capture hash. Explicitly record the unrecorded interval, without claiming packet-loss counts or continuity.
- Interrupt an active capture by native recorder removal: non-success attempt retained, no false completed claim, prior completed files unchanged. Reconcile and verify again.
- Unit tests on macOS and Linux; actual capture integrity and replacement on Linux. No ordinary test creates VMs. Preserve failed attempts, clean exact lab, stop new VM.
