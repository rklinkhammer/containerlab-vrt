# Fresh controller regression — reproduced and diagnosed

## Observed

The standalone regression uses real loopback VRT application runtimes on macOS ARM64. Four radio runtimes remain alive while only ProcessorController is destroyed and recreated. Existing coordinated streaming and same-controller reconnect checks pass before replacement.

- Unmodified-header probe exits 1: replacement configurations=0, starts=0, protocol_failures=9 (baseline.log).
- A diagnostic copy of the pinned headers logs at the native duplicate-ID branch without changing its return behavior. It also exits 1 with the same result (trace.log).
- Trace shows each new controller starts at message ID 1; surviving radio high-water marks are 188–189, association generation 1. Later IDs 2/3 remain below those marks. This confirms command-ID reuse as a cause of rejection in the focused regression.
- Prior Linux processor-replacement failures remain separate runtime evidence. This local trace strongly explains those failures, but their individual wire requests were not captured.

## Documented

Native ControllerRegistry relationships start next_mid at 1. Public VitaRuntime registers relationships with 1. SDR TransactionManager rejects/replays IDs at or below highwater_id. Public recover_stream requires a new SID and rejects any SID in its history. Normal ingress reconnect does not change the runtime association.

## Status

No production fix applied: the recovery-contract choice is pending (PROPOSED_FIX.md). Do not disable the duplicate guard, arbitrarily pick large IDs, or claim that continued detections prove controller recovery. No new VM was created; Linux requalification has not run. No third-party source was modified: diagnostic headers live only in ignored artifacts/runtime/controller-replacement/include.

## Subsequent implementation

The user selected stream-ID preservation. The explicit native extension and bounded status handshake are now implemented; see ../../docs/COMMAND_RESUMPTION.md and ../processor-recovery-fix. Original baseline and intermediate failed tests remain historical evidence. The production regression is now tests/processor_controller_tests.cpp; the standalone source here records the pre-fix experiment.
