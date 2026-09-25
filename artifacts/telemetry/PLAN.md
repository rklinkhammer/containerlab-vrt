# Telemetry acceptance before implementation

Roles: radio, processor (with existing controller), detector, passive recorder. Keep legacy status/protocol and summary interfaces; add versioned stdout records. No third-party changes.

Expectations: all telemetry JSON lines <=4096 bytes; role/instance/boot ID/UTC/time/sequence present; counters cumulative per boot. Default heartbeat5s, env configurable100..60000ms,0 disables new telemetry. Ready means local initialization only. Idle after inactivity; healthy means recent measured successful local activity, never end-to-end health. Malformed/send/write failures degrade reporting; fatal errors emit failed. Unknown measurements are null, including inferred loss, queue depth where unavailable, and durable recorder bytes (ofstream flush is not fsync).

Scenarios: idle and interrupted input still emit heartbeat; malformed input increases malformed counter; sequence discontinuity never labelled proven loss; failed UDP consumer can yield send errors but successful send cannot prove delivery; recorder write failure raises observed error; restart changes boot ID/resets counters. Detection events bounded10/s with suppression count. Concurrency must keep each JSON line intact; shutdown emits a final heartbeat. Compare identical finite workload with telemetry disabled/enabled; report wall time and variance, no assumed overhead target.

Use existing native tests plus new deterministic telemetry and process scenarios. Baseline retention/coordinated-start failures tracked separately. macOS cannot qualify AF_PACKET; Linux tests/builds only in a new dedicated VM, never a pre-existing instance. Exact task VM cleanup on success/failure. No workspace/investigation edits, no serial ports, brokers or databases.
