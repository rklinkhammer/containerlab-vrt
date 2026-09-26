# Documented source behavior

src/processor_controller.cpp starts each new controller with unconfigured/unstarted sessions and coordinated_ false. reconcile establishes a status/VRT session, configure waits for execution, coordinate_start submits UTC now +5 seconds and waits for validation. It does not adopt a prior controller epoch or infer a prior coordinated state from streaming. Hence test explicit re-coordination and actual downstream activity. apps/processor.cpp emits controller_metrics at graceful exit.

Prior detector/radio native replacement does not qualify processor replacement. stale_starts_replayed lacks an increment site; use counts, events, status and actual traffic together. Status and telemetry boot identities are different namespaces.

## Focused diagnosis (Documented source; inferred cause)

- include/vita/runtime/transaction/controller.hpp Relationship.next_mid defaults to 1; register_relationship also defaults to 1.
- include/vita/runtime/transaction/manager.hpp accept routes IDs <= highwater_id to replay_existing when monotonic_ids is enabled; reset_after_drain does not visibly reset that field.
- include/vita/runtime/public/runtime.hpp enables monotonic IDs for SDR radio managers.
- src/radio_transport.cpp fail_control disconnects ingress and releases the control generation; accept_controller reconnects ingress. src/processor_controller.cpp uses controller_id=1/controllee_id=2 for its fresh relationships.
- outcomes.hpp defines observation enum values 6=timeout and 7=late_response.

These facts motivate tracing association lifetime and message identity across controller process replacement. They do not prove which native admission branch handled the runtime requests.
