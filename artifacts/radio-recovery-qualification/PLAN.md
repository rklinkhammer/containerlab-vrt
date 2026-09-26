# Radio replacement qualification

Question: Does native removal/recreation of one radio restore application traffic through the existing controller without restarting other nodes or replaying a stale start?

Independent expectations, written before execution:
1. Establish all four configured tones within half an FFT bin and advancing detector traffic.
2. Remove radio1 using Containerlab filtered destroy, keep the management network, then deploy the unchanged complete topology. Never repair links manually.
3. The target container and application boot identities must change; the other seven container IDs and start times must remain unchanged. The target eth1 must regain the configured address and MTU.
4. The controller must observe one boot change, reconfigure that radio, and admit a new start epoch later than the original coordinated start. Actual resumed radio streaming and post-replacement detections must corroborate admission. No stale starts replayed.
5. Recovery within 90 seconds, followed by 30 seconds of advancing traffic and fresh valid detections for every stream. Replacement is disruptive; do not claim lossless recovery.
6. Collect controller shutdown metrics, report protocol/status failures separately, destroy the exact lab, audit remaining containers, then stop the newly created VM.

Scope: one selected synthetic radio on Linux ARM64, Containerlab 0.79.0, pinned patched VRT source. No existing VM access, no GUI changes, no universal recovery claim. Preserve every failed attempt.
