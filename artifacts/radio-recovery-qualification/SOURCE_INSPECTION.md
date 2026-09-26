# Source inspection (Documented)

`src/processor_controller.cpp` reconcile detects changed status boot_id, clears configured/started, and increments boot_changes. configure waits for execution evidence; restart_after_boot_change submits UTC now + 5 seconds and waits for validation. Its radio_restarted event means admission, not observed streaming. apps/processor.cpp emits controller_metrics on graceful exit.

Containerlab 0.79.0 native filtered destruction plus full-topology deployment was observed for detector replacement in ../recovery-qualification; equivalent radio recovery remains unqualified until this trial. Existing node exec settings are not replayed by link-only reconciliation.

Limitation: stale_starts_replayed has no increment site in the current source; its zero value alone is not independent evidence. This trial additionally checks exactly five start submissions/admissions (four initial + one replacement), one later target restart event, a later epoch, and actual streaming. Packet-level command replay tracing remains outside this bounded trial.
