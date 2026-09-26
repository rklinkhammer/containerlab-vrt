# Recovery contract choice

## Recommended: preserve stream IDs with an explicit native extension

The pinned public runtime exposes no command-ID resumption operation. Its lower-level controller relationship accepts a next_mid argument, but the public runtime registers it with 1. Its recover_stream operation rejects reuse of any historical stream ID. A socket reconnect is not a new wire association.

Proposed bounded change, pending recovery-contract selection:

1. Add a narrowly reviewed upstream patch exposing the radio's authoritative admitted-command watermark and a controller operation that can only advance its next command ID. Never clear replay retention or decrease high-water marks. Reject exhaustion at UINT32_MAX and invalid handles.
2. Add a versioned status capability for resumption, bound to radio boot identity, stream ID and exclusive control-session generation. Missing/invalid capability fails explicitly; never guess a watermark or silently fall back to ID 1.
3. Serialize the watermark and session snapshot with runtime progress. The status service currently runs on separate threads, so adding an unsynchronized native getter there would be incorrect. A stale snapshot must not become a valid resume acknowledgement.
4. Acquire the control connection, validate the bound snapshot, advance the controller ID floor, then reconcile native VRT status/configuration/start. Connection or boot changes invalidate that snapshot. Do not replay old pending start commands.
5. Independently test fresh-controller recovery after many commands, ordinary reconnect without extra starts, stale-ID rejection, boot/session mismatch, malformed capability, ID exhaustion and concurrency. Keep the original failing fixture unchanged as a regression expectation.
6. Pin and hash the extension separately from existing patches, verify application/unit suites, then repeat actual processor replacement in a newly created dedicated VM. No runtime pass until both data and controller checks succeed.

## Alternative: native recovery with new stream IDs

Use the existing documented recover_stream lifecycle, including quiescence and confirmed state, allocate new wire stream IDs, and update processor/detector/recorder mapping contracts. This changes externally visible workflow identity and is substantially broader than processor-only replacement. It cannot be called a same-stream-ID recovery fix.

The user has been asked to select the contract because these alternatives change observable behavior and the third-party API boundary. No production or third-party changes have been made while that choice is pending.

## Decision and implementation

The user selected the stream-ID-preserving extension. Implemented behavior, including serialized status snapshots and native stop-before-configure, is documented in ../../docs/COMMAND_RESUMPTION.md. Local/Linux tests and fresh runtime qualification are recorded in ../processor-recovery-fix. The pending-choice text above is retained as proposal history.
