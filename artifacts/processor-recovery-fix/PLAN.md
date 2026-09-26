# Processor recovery fix qualification

Preserve original independent processor-replacement expectations from ../processor-recovery-qualification/PLAN.md. Require both traffic recovery and four configured/admitted controller starts, same radio stream/boot identities and unchanged other node identities. Native stop-before-configure is deliberate disruption, not transparent continuation. Use a fresh VM, preserve failures, clean lab and stop VM.

Additional acceptance: resumption advances IDs only, refuses exhaustion, binds capability to boot/SID/association/control-session generation, and reads status through serialized native snapshots. Missing/malformed capability must not enable commands. Existing same-controller reconnect must not submit additional starts.
