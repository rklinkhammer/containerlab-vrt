# Processor replacement qualification

Written before execution. Question: can a new processor/controller attach to the four existing streaming radios and recover useful spectra without replacing other nodes?

Independent acceptance:
1. Establish all configured tones within half an FFT bin and 30 seconds of advancing detector traffic. Save processor telemetry identity and all radio status boot identities, settings and connection generations.
2. Native filtered destroy of processor, preserve management network, then deploy unchanged full topology. Confirm processor absent between operations, other seven container identities/start times unchanged, all radio boot identities unchanged. Query radios while processor is absent to distinguish continued streaming from later restart.
3. New processor container/telemetry identity, restored eth1 UP/address/MTU, fresh all-stream valid detections within 90 seconds, then another 30 seconds of advancing traffic.
4. New controller must configure four sessions, submit/admit four starts at a later coordinated epoch, with zero observed radio boot changes. Existing radios must report active connections and streaming with configured settings after recovery. Source indicates fresh controller state schedules a new epoch rather than adopting the prior epoch; record this as disruptive re-coordination, not transparent continuation.
5. Record counters, retries, sequence gaps, safe failure details and unchanged input hashes. No standalone reliance on the uninstrumented stale_starts_replayed counter.
6. Gracefully stop processor after traffic checks to collect metrics, destroy exact lab, audit remaining resources, stop the new VM.

Scope: synthetic pinned ARM64 eight-node fixture, not lossless recovery, simultaneous faults, other kinds or AMD64. No sibling modifications or pre-existing VM access.
