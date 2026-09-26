# Processor discard diagnostics

Processor metric and heartbeat records add `discard_schema: "processor.discards/1"`, `discarded`, and a fixed `discard_reasons` object. These are cumulative counts for the process boot; a restart creates a new boot identity and zero counters.

| Reason | Meaning |
| --- | --- |
| limits | Listener stream outside the configured application range, or datagram beyond the receive limit |
| invalid_envelope | Native envelope decoding failed, or packet SID differs from its listener |
| invalid_context | Context structure, field values or application profile constraints were rejected |
| invalid_data | Signal profile, envelope, timestamp representation or trailer rejected |
| invalid_samples | Native sample view invalid/empty, too many pairs for the FFT, or sample access failed |
| waiting_context | Envelope/trailer and sample view passed their checks, but this processor has no valid context for the stream yet |
| timestamp_range | Gap reconstruction would subtract samples outside the supported timestamp range |

The sum equals `discarded`, which equals the existing processor `malformed` aggregate. The legacy name is retained for compatibility; it has always included missing-context discards and must not be interpreted as a count of corrupt wire packets. The detector's existing malformed counter is a separate measurement and is unchanged.

Validation of signal shape precedes the missing-context classification, so a malformed signal without context is not counted as waiting_context. Acceptance criteria and emitted spectra are unchanged: pre-context signals remain discarded, never buffered or silently repaired. No packet payloads or per-packet events are logged. The seven counters appear at existing bounded reporting intervals.

Counters are historical, not proof of current health. The [local-activity/2 policy](HEALTH_POLICY.md) now uses error deltas and recent successful activity, allowing recovery while preserving historical counters. The original classification experiment used the earlier cumulative-error policy; its recorded results remain historical.

The earlier 130 aggregate counts cannot be retroactively partitioned because that run did not emit reasons. New qualification in artifacts/discard-classification measures its own counts and preserves that distinction.
