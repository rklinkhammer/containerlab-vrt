# Implement a standalone four-radio SDR lab with Containerlab and Nokia SR Linux

Build a complete, runnable Containerlab lab using:

- VRT framework: https://github.com/rklinkhammer/vrt_framework.git
- Behavioral reference: https://github.com/rklinkhammer/graphx-docker/tree/main/examples/four-radio-vita

Use Nokia SR Linux for switching, standalone application containers, and an **SDR profile renamed from the VRT framework’s GraphX profile**.

This is an implementation task. Inspect the sources, implement the required changes, build the project, and run applicable validation. Do not stop at an architecture proposal.

## 1. Mandatory architectural requirements

- Containerlab owns topology, nodes, links, addressing, and deployment.
- `vrt_framework` supplies VITA protocol processing and controller/controllee behavior.
- Rename the framework’s existing GraphX profile to the **SDR profile**.
- Use graphx-docker only as a behavioral reference.
- No node may contain GraphX infrastructure.
- Use Nokia SR Linux; do not substitute OVS, OVN, or a host bridge for the application switch.
- Use plain TCP control and UDP data, with no TLS or application authentication.
- Deploy four independent radio containers.
- Keep management connectivity separate from application traffic.
- A generalized GUI is outside scope. Provide structured state, metrics, and capture access suitable for a later viewer.

## 2. Inspect and pin the references

Inspect the four-radio example, application sources, configuration defaults, dependency declarations, and relevant tests in graphx-docker.

Inspect the VRT framework’s public APIs, GraphX profile, framing interfaces, runtime contracts, and regression tests.

Use these revisions as the starting reference baseline:

- GraphX: `7cad4da8646eda302a005070228495c1aa87d89a`
- VRT: `dbe85d37155145842da60367af1c4beef8801b0c`

Verify that both revisions are obtainable. Record dependency versions and licenses. Any revision change requires a documented reason and renewed compatibility checks.

The pinned GraphX topology specifies radio3 at **100.150 MHz**, with an initial phase of **90°**. Preserve these values.

Create a behavior-mapping document covering:

- Preserved reference behavior.
- Standalone replacements for GraphX infrastructure.
- The GraphX-to-SDR profile migration.
- Preserved and changed wire formats.
- Numerical defaults and their source locations.
- Conflicting evidence and its resolution.

Historical reports are reference material, not proof that this implementation works.

## 3. Protect the shared VRT checkout

Do not modify the shared sibling `vrt_framework` checkout, its branch, index, configuration, or working files.

Perform the SDR migration in a **project-owned clone/fork or isolated worktree**, or apply a reproducible patch series to a clean pinned source archive.

Record:

- Upstream revision.
- Patch identities and application order.
- Resulting source revision or content hash.
- Build configuration.

Do not leave essential changes only inside a temporary dependency cache. Do not publish upstream changes unless separately requested.

The final project must build reproducibly without relying on modifications to any shared checkout.

## 4. Mandatory GraphX-to-SDR profile migration

The GraphX VRT profile is a protocol definition supplied by `vrt_framework`, not GraphX deployment infrastructure. Its behavior is permitted, but its implementation must be renamed to **SDR**.

This must be a source/API migration, not a display-label change or wrapper alias.

Inspect and migrate applicable:

- Profile directories and header filenames.
- Namespaces, public APIs, types, constants, and functions.
- Internal profile-specific transaction state and budget identifiers.
- Build and installation references.
- Configuration selectors.
- Tests, fixtures, examples, and active documentation.
- Runtime messages.

Use `sdr` in identifiers and paths and “SDR profile” in prose.

The applications must consume the renamed APIs directly. An `sdr = graphx` namespace alias is insufficient.

Do not retain old executable profile aliases merely for convenience. Document any genuinely required compatibility exception.

Historical migration notes, upstream URLs, licenses, and attribution may retain the original name.

### Preserve protocol behavior

The rename must not change:

- Packet types, layouts, lengths, or byte ordering.
- Numeric identifiers, OUI values, or class identifiers.
- Context and control mappings.
- Framing.
- Timestamp and scheduled-start semantics.
- Acknowledgments, replay handling, or command correlation.

A symbolic rename does not authorize changing the underlying numeric value.

“SDR” is this project’s name for the existing application profile within VITA/VRT. Do not claim a new industry standard or independent-vendor interoperability.

### Independent equivalence evidence

Before applying the rename:

1. Build or run the pinned, unmodified VRT reference in isolation.
2. Inventory every existing P17 wire fixture and wire-producing test path.
3. Capture immutable pre-rename golden vectors using deterministic inputs.
4. Record fixture provenance, input parameters, toolchain, source revision, and SHA-256 hashes.
5. Preserve existing independently authored vectors and negative fixtures.

After the rename:

- Exercise the same fixture inventory.
- Compare every existing P17 wire fixture byte-for-byte.
- Use a comparison harness that does not call the patched codec to generate expected bytes.
- Fix clocks, identifiers, seeds, and other variable inputs so comparisons are reproducible.
- Do not normalize away protocol fields to obtain a match.
- Verify malformed-input outcomes, framing behavior, replay behavior, and timing semantics separately where byte comparison alone is insufficient.
- Report missing fixture coverage explicitly.

Do not regenerate expected outputs from the renamed implementation and call that equivalence evidence.

## 5. No GraphX infrastructure

Create an independent project and build/deployment workflow.

Do not install, copy, link, mount, or launch:

- GraphX runtime libraries.
- GraphX executables or wrappers.
- GraphX configuration loaders or catalogs.
- GraphX deployment/compiler services.
- GraphX lifecycle or ownership services.
- GraphX observation agents or browser infrastructure.
- GraphX credential services or base images.

Renaming GraphX infrastructure does not make it permissible.

Adapt domain algorithms only where licensing permits, remove infrastructure dependencies, and preserve attribution.

The project must build without the graphx-docker checkout. Include any adapted algorithms or reference fixtures explicitly with provenance.

Permitted dependencies include the renamed VRT framework profile, SoapySDR, an FFT implementation, and independent configuration or logging libraries.

## 6. Required topology

Deploy eight nodes:

| Node | Role |
| --- | --- |
| radio1–radio4 | Four independent virtual radios |
| processor | Radio controller, IQ receiver, FFT processor |
| detector | Spectrum consumer and feature detector |
| recorder | Passive mirrored-frame receiver |
| switch1 | Nokia SR Linux switch |

Use Linux containers for applications.

Configure `switch1` with:

- Kind: `nokia_srlinux`
- Type: `ixr-d2`
- Image repository: `ghcr.io/nokia/srlinux`
- A verified release and immutable digest compatible with the host architecture.

Verify syntax against the pinned Containerlab release. Do not invent tags or digests.

### Physical links

| Switch interface | Application endpoint |
| --- | --- |
| ethernet-1/1 | radio1:eth1 |
| ethernet-1/2 | radio2:eth1 |
| ethernet-1/3 | radio3:eth1 |
| ethernet-1/4 | radio4:eth1 |
| ethernet-1/5 | processor:eth1 |
| ethernet-1/6 | detector:eth1 |
| ethernet-1/7 | recorder:eth1 |

Configure ports 1–6 in one Layer 2 application broadcast domain using bridged subinterfaces and a MAC-VRF.

Configure port 7 as a dedicated local mirror destination outside ordinary forwarding. The recorder capture interface needs no application IP address.

### Default application addressing

| Node | Address | Listener |
| --- | --- | --- |
| radio1 | 10.79.0.10/24 | TCP 18401 |
| radio2 | 10.79.0.11/24 | TCP 18402 |
| radio3 | 10.79.0.12/24 | TCP 18403 |
| radio4 | 10.79.0.13/24 | TCP 18404 |
| processor | 10.79.0.14/24 | UDP 18501–18504 |
| detector | 10.79.0.15/24 | UDP 18600 |

The processor initiates four control connections. Radios send IQ to their assigned processor ports. The processor sends spectra to the detector.

Distinguish the nine application-network flows from the seven physical links.

The four additional lifetime/status flows defined below use the management network and must appear separately in flow inventories and diagrams.

## 7. One authoritative parameter source

Do not maintain independently editable copies of addresses, ports, interface names, MTUs, or radio parameters.

Use:

- Containerlab topology for nodes, links, and infrastructure addressing.
- One application-parameter source for values not represented by supported Containerlab fields.

Deterministically generate application configuration from the resolved topology and that parameter source. If environment substitution is used, resolve it once and use the same resolved values for deployment and configuration generation.

Use Containerlab/runtime inspection to resolve dynamically assigned management endpoints before launching dependent services. Do not maintain a separate manual management-address inventory.

Do not invent unsupported Containerlab fields or introduce a replacement topology DSL.

Address and port overrides must flow through this generation process. Manual changes to generated application files must not be required.

Record input hashes in generated artifacts and reject stale or inconsistent configuration before deployment.

Test that an address or port override updates every affected endpoint consistently and that conflicting assignments fail validation.

## 8. Switch feasibility gate

Before integrated acceptance, demonstrate on the actual pinned image and architecture:

- MAC-VRF forwarding among application ports.
- Application IP MTU 9000.
- Full-size forwarded and mirrored packets.
- Local mirroring to the recorder.
- Required counters and operational-state inspection.

Configure NOS MTUs according to its frame-size accounting.

Prefer ingress mirroring on all six application-facing ports. Verify IQ, spectra, bidirectional TCP control, ARP, and duplication behavior.

Promiscuous mode is not a substitute for mirroring.

Prove IQ, spectra, and VRT control traverse SR Linux rather than management networking. The explicitly defined management-status flows are exempt from this application-network requirement.

If the gate fails, continue independent application work but mark integrated acceptance blocked. Do not silently substitute OVS.

## 9. Standalone applications

### Radios

Build one implementation instantiated in four containers. Each owns an independent SoapySDR virtual device, signal state, sample index, control endpoint, and stream identity.

Use:

| Setting | Value |
| --- | --- |
| Signal frequencies | 100.050, 100.100, 100.150, 100.200 MHz |
| Initial phases | 0°, 45°, 90°, 135° |
| Receiver center | 100 MHz |
| Sample rate | 1,000,000 complex samples/s |
| Bandwidth | 800 kHz |
| Gain | 0 dB |
| Burst length | 262,144 samples |

The baseband tone is signal frequency minus receiver center frequency.

Control and acquisition must share the same device instance. Acquire samples through its streaming interface.

Implement the standalone SoapySDR device/backend integration required by the VRT framework. Do not assume the framework already supplies that adapter.

Use the renamed SDR profile for packet processing and control. Preserve initial configuration, scheduled start, context, timestamps, acknowledgments, and replay behavior.

No IQ may be emitted before successful configuration and the required start command.

### Processor and detector

The processor controls all radios, receives all four streams, maintains independent processing state, computes FFT power spectra, and forwards results.

Use the default 2048-point FFT, rectangular window, and no overlap.

Preserve gap and validity metadata. A short final burst packet alone must not imply missing samples.

The detector reports stream identity, observation time, detected RF frequency, and validity.

Replace GraphX-specific spectrum envelopes with a documented, versioned standalone format. Preserve necessary metadata and test both endpoints.

### Recorder

Receive and count mirrored Ethernet frames with bounded buffers and available drop statistics.

Remain passive. Recorder failure must not block forwarding.

Preserve receive/discard behavior. Saved PCAP/PCAPNG is a separate diagnostic function.

## 10. Plain TCP and explicit connection ownership

Use plain TCP control and UDP data. Do not implement TLS, certificates, passwords, tokens, authentication handshakes, or application login flows.

Custom status, metrics, and capture interfaces must also operate without authentication and bind to loopback or the isolated lab network.

This is a trusted, isolated test configuration. Logical identifiers and single-connection ownership are operational controls, not proof of peer identity. An unauthenticated peer may impersonate the configured controller.

Do not claim protection against malicious peers.

Do not alter vendor NOS internals to remove built-in security. Prefer host-side container execution where practical and disclose any unavoidable native management-authentication requirement.

### One active controller per radio

Implement these ownership rules:

1. Each radio accepts at most one active VRT control connection.
2. The first accepted connection acquires the control slot.
3. Additional connections are rejected or closed without dispatching commands or changing runtime state.
4. A new connection cannot take over while the existing connection remains active.
5. EOF, fatal socket error, framing failure, or the defined stale-connection timeout releases the slot after transport cleanup.
6. Surviving connections use the defined VRT liveness mechanism.
7. Reconnection uses the configured logical association, not an ephemeral source port.
8. Replay history, device state, and sample phase survive ordinary TCP reconnects within the same radio process lifetime.

A radio process restart creates a new runtime lifetime. Generate and expose a new boot identifier, clear old session state, and require controller reconciliation and explicit initialization/start.

Do not replay queued commands from a previous process lifetime.

### Transport adapter

Implement:

- Fragmented and coalesced reads.
- VRT packet-size framing without an extra prefix.
- Early length validation.
- Partial writes and correct buffer ownership.
- Exactly one terminal completion per transmission.
- Bounded queues and backpressure.
- Connection, partial-frame, and stalled-output deadlines.
- Disconnect cleanup and bounded reconnect backoff.
- Safe detachment before runtime destruction.

Removing authentication must not remove protocol validation, command correlation, or resource bounds.

## 11. Lifetime/status transport

Provide a dedicated, read-only status service in each radio application. It is separate from the VRT control connection.

### Endpoints

Bind the service to each radio’s Containerlab management address:

| Radio | Status transport | Port |
| --- | --- | --- |
| radio1 | Plain TCP | 18701 |
| radio2 | Plain TCP | 18702 |
| radio3 | Plain TCP | 18703 |
| radio4 | Plain TCP | 18704 |

Use no TLS or authentication.

The processor queries these services over the management network. Generate their endpoints through the authoritative configuration process.

Include all four flows in endpoint inventories, diagrams, validation, and deployment documentation.

### Framing and bounds

Use one request and one response per TCP connection:

- Four-byte unsigned, big-endian payload length.
- Exactly that many bytes of UTF-8 JSON.
- Maximum request payload: **1024 bytes**.
- Maximum response payload: **4096 bytes**.
- Maximum concurrent status connections per radio: **4**.
- Maximum complete request/response transaction time: **2 seconds**.

Validate lengths before allocating or admitting payloads. Handle fragmented reads and partial writes.

Reject malformed JSON, unsupported versions, oversized messages, and additional requests on the same connection.

Close the connection after the response. The service must not hold the radio control slot or block acquisition.

This framing applies only to the status service. The VRT control stream retains its unchanged packet-size framing with no JSON, lifetime preamble, or extra length prefix.

### Status contract

Use a versioned request such as:

`{"version":1,"operation":"get_status"}`

Return at least:

- Status-schema version.
- Configured radio identifier.
- Boot/lifetime identifier.
- Process uptime.
- Application readiness.
- Control-slot state.
- Active control-connection generation, or null.
- Radio streaming state.
- Observation timestamp and clock domain.

Generate a new boot identifier whenever the radio process starts. Keep it stable across TCP reconnects within that process lifetime.

Increment the connection generation whenever a new VRT control connection acquires the control slot.

The status service is read-only. It cannot configure, start, stop, reset, or take over a radio.

### Reconnection and restart handling

The controller maintains the last observed boot identifier and connection generation.

Before state-changing commands on a newly established control connection:

1. Read a status snapshot.
2. Establish the VRT control connection.
3. Read status again.
4. Confirm an unchanged boot identifier and the expected control-slot transition.
5. Complete a valid VRT status transaction on the new connection.

If observations are inconsistent or the connection fails during this sequence, reconcile again before issuing state-changing commands.

Do not assume status snapshots make the two connections atomic. On a control disconnect, invalidate readiness and pending-command decisions until reconnection is reconciled. Never transfer a partially sent command to a new connection without applying the documented replay and lifetime rules.

A changed boot identifier means a new radio lifetime:

- Discard queued commands and pending transactions from the previous lifetime.
- Reconcile actual state.
- Require explicit configuration and start before streaming.
- Never automatically replay an old scheduled-start command.

### Control liveness

Use valid VRT status requests and replies on the active control connection for liveness, at intervals no greater than **1 second**.

Successful requests to the separate status service must not keep a stalled VRT control connection alive.

Release stale control ownership within **5 seconds** after the last successful control-liveness observation, subject to shorter framing and socket deadlines.

## 12. Fixed acceptance floors

Check in an acceptance specification before integrated runs. These requirements cannot be weakened simply because a test fails.

### Nominal sustained run

After startup and stabilization:

- Run all four radios concurrently at the specified default rate for at least **60 continuous seconds**.
- Require zero unexpected IQ loss between radio send and processor receive at the application level.
- Require zero unexpected spectrum-message loss between processor send and detector receive.
- Require zero malformed packets, receive truncations, or unintended IP fragmentation.
- Require no unexplained queue overflows or application receive drops.

Use sequence/sample accounting and measurement barriers or reconciliation so in-flight packets at observation boundaries are not falsely classified as loss.

Test recorder and diagnostic-capture completeness separately with a bounded, known packet set. Do not infer forwarding loss from capture-tool drops.

### Detection

For clean, valid single-tone spectra at 1 MS/s and N=2048:

- Maximum frequency error: **244.140625 Hz**, half one FFT bin.

Define separate expected results for zero, gapped, malformed, and unavailable data.

### Scheduled start

The pinned profile admits scheduled starts with lead times from **20 ms through 10 seconds**. Preserve this contract.

After all four radios are configured, connected, and ready:

1. Select one common requested start epoch **5 seconds in the future**.
2. Send all four scheduled-start commands promptly.
3. Verify admission within the profile’s allowed interval.
4. Require all four successful admission acknowledgments at least **1 second before** the requested epoch.
5. If that deadline is missed, report a failed coordinated-start attempt. Use the profile’s supported cancellation/stop behavior for already admitted radios and record the outcome. Do not silently reschedule or claim synchronized success.

The 5-second lead is measured when the controller selects the common epoch. Each radio independently validates remaining lead when it receives the command.

Require:

- The common first-sample timestamp matches the requested sample epoch according to the profile’s exact representation.
- Measured device activation error is no greater than **20 ms** relative to the requested epoch.
- Activation is measured at the device/application boundary, not from packet arrival.
- Clock assumptions, offsets, and measurement uncertainty are recorded.
- Profile admission limits remain unchanged.
- Boundary rejection and delayed-command handling are tested separately.

### Deadlines and shutdown

- Connection, partial-frame, and stalled-output deadlines: no more than **2 seconds** each.
- Healthy control liveness interval: no more than **1 second**.
- Stale control ownership released within **5 seconds** after the last successful control-liveness observation.
- Graceful application shutdown completed within **5 seconds**.
- Diagnostic capture stopped and its file finalized within **5 seconds** of a stop request, excluding separately measured download time.
- Reconnect backoff bounded to a maximum interval of **2 seconds**.

### Resource bounds

Declare fixed queue capacities, application memory ceilings, log limits, and retry limits before testing. Enforce them and demonstrate no unbounded growth during nominal and fault runs.

Default diagnostic capture must be bounded by both:

- **60 seconds**, and
- **64 MiB total retained capture storage**.

If the environment cannot meet these floors, report failed or blocked acceptance and the measured cause. Do not claim completion by lowering workload or relaxing thresholds.

## 13. Required fault and transport tests

Run these separately from nominal acceptance:

- Stop one radio: healthy streams continue and missing-stream state appears within 5 seconds.
- Stop the recorder: ordinary forwarding continues.
- Stop the detector: processor buffering remains bounded and unavailable delivery is reported.
- Break one control connection: reconnect preserves replay history and does not repeat completed device operations.
- Attempt a second controller connection: no takeover or state mutation occurs.
- Stall a controller: ownership releases within the specified deadline.
- Restart a radio: a new lifetime is detected and explicit reconciliation occurs.
- Inject malformed and truncated control frames: fail closed without unbounded allocation.
- Stall TCP reads or writes: deadlines and buffer ownership remain correct.
- Disable an application link or switch forwarding: affected functions become unavailable without fabricated healthy metrics.
- Exercise zero, missing, and reordered data: validity/gap reporting remains correct.
- Stop capture and destroy the lab during activity: cleanup stays scoped.

Also verify:

- Status length-prefix fragmentation and partial writes.
- Oversized, malformed, and unsupported-version status requests.
- Status connection and transaction bounds.
- Boot identifier stability across control reconnects.
- Boot identifier change after radio restart.
- Control-connection generation changes.
- Restart between the two status snapshots.
- Restart after status verification but before a control transaction completes.
- Status-service overload without disruption to IQ or VRT control.
- A responsive status service does not conceal a stalled control connection.
- Failed coordinated-start admission does not produce a false success report.

Failed application containers must not restart automatically.

## 14. Observation, capture, isolation, and cleanup

Expose:

- Container state and available resource measurements.
- Application readiness.
- Switch interface state and counters.
- Per-stream counters and gaps.
- Detector results.
- Recorder receive statistics.

Include observation timestamps and stale/unavailable states. Separate application payload counters from interface counters. Do not assign a shared-interface total to each application flow.

Provide bounded capture with:

- Verified interface or flow selection.
- Validated filters.
- Start and stop.
- File retrieval.
- Interface/namespace provenance.
- Packet summaries and hashes.
- Cleanup after timeout, failure, and shutdown.

Control traffic is unencrypted and should be verifiable against the SDR profile’s VRT framing.

Use a dedicated isolated Linux environment. A new VM is acceptable but not mandatory if an existing dedicated environment is demonstrably isolated.

Record:

- Preflight resource inventory.
- Unique lab/resource prefix.
- Host architecture and pinned software/image versions.
- Resources created.
- Cleanup commands.
- Final residual-resource audit.

Never use global pruning or delete unrelated resources.

## 15. Deliverables

Deliver:

- `README.md` and correctly named `IMPLEMENTATION.md`.
- Containerlab topology and SR Linux startup configuration.
- Standalone C++23 applications and documented toolchain requirements.
- Deterministic configuration generation and validation.
- Reproducible image builds and dependency manifests.
- Project-owned SDR profile fork or patch series.
- Immutable pre-rename golden vectors and independent comparison harness.
- Spectrum-format documentation.
- Versioned lifetime/status schema and framing specification.
- Connection ownership, liveness, and restart documentation.
- Build, deploy, inspect, capture, and destroy commands.
- Acceptance specification, automated tests, and evidence.
- Physical and application-flow diagrams, including management-status flows.

Keep infrastructure configuration authoritative and generated application files reproducible.

Do not include revision commentary in `IMPLEMENTATION.md`; its first line must be the implementation brief’s title.

## 16. Completion and evidence

Verify GraphX-infrastructure absence through dependency, build/link, image, process, and mount inspection. A text search alone is insufficient.

Verify that the project builds without the graphx-docker checkout and without modifying the shared VRT checkout.

Report unavailable runtime facilities and unexecuted tests honestly. Do not substitute simulated output for runtime evidence.

Finish with exact runnable commands and:

Requirement | Implementation | Verification evidence | Status

Distinguish verified, implemented-but-unverified, failed, blocked, and deferred work.

Do not declare completion solely because code compiles, containers start, or a topology renders. Completion requires the fixed acceptance gates and documented evidence.