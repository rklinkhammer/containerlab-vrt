# Qualified recovery for the four-radio test lab

Linux ARM64 qualification uses Containerlab 0.79.0 (`5ae50094a`) and the pinned, patched application source. See [evidence and limitations](../artifacts/recovery-qualification/RESULTS.md).

## Detector replacement

In the dedicated Linux guest, from the existing lab's original repository, preserve required logs/captures first. This deliberately removes the detector container and its writable state. Keep the original generated topology and companion files unchanged.

```sh
python3 scripts/generate_config.py --check
sudo containerlab destroy --topo generated/four-radio.clab.yml \
  --node-filter detector --keep-mgmt-net --graceful
sudo containerlab deploy --topo generated/four-radio.clab.yml --format json
```

Do not add `--cleanup` to filtered destruction. Deploy the **complete topology**, without a node filter: this pinned release rejects filtered reconciliation against an existing lab. The ordinary scripts/deploy.sh intentionally refuses an existing lab and is for initial deployment, so recovery uses the explicit native commands above.

Verify recovery rather than relying on exit status:

```sh
sudo docker exec clab-four-radio-sdr-detector ip -j addr show dev eth1
sudo docker logs --tail 40 --follow clab-four-radio-sdr-detector
```

For this fixture, eth1 must be up with MTU 9000 and IPv4 10.79.0.15/24. Verify a new boot_id, sequence/counter reset, advancing datagrams, and valid detections for every configured stream. The experiment observed convergence within its 90-second bound and then 30 seconds of continuing traffic. Other seven container IDs and StartedAt values remained unchanged. Replacement entails an outage; zero later gap counters do not prove no data was lost during replacement.

If deployment fails after destruction, retain the exact failure and inspect the remaining lab. Do not substitute manual veth/address repairs or repeatedly restart the container and claim recovery.

## Radio replacement

[One-radio ARM64 qualification](../artifacts/radio-recovery-qualification/RESULTS.md) passed using the same native sequence, selecting `radio1` instead of `detector`:

```sh
sudo containerlab destroy --topo generated/four-radio.clab.yml \
  --node-filter radio1 --keep-mgmt-net --graceful
sudo containerlab deploy --topo generated/four-radio.clab.yml --format json
```

Verify the new radio container and boot identities, restored eth1/address/MTU, actual streaming, a later controller start epoch, and fresh detections for all configured streams. Other node identities must remain unchanged. The trial recovered within 90 seconds and then sustained traffic for 30 seconds. It observed controller retries/protocol failures and sequence discontinuities: replacement is not lossless. The zero stale-start counter alone is insufficient; see the evidence limitations. Recorder replacement and completed capture preservation are now qualified below; switch replacement remains unqualified. [Discard diagnostics](DISCARD_DIAGNOSTICS.md) distinguish valid signal data awaiting context from invalid input; the fresh classified trial observed only waiting_context during startup.

## Processor replacement

[The stream-ID-preserving fix](../artifacts/processor-recovery-fix/RESULTS.md) qualifies processor replacement on the updated pinned images. The [original failure](../artifacts/processor-recovery-qualification/RESULTS.md) remains evidence of why data traffic alone is insufficient. Upgrade radios and processor together; the new controller requires the command-resumption capability described in [COMMAND_RESUMPTION.md](COMMAND_RESUMPTION.md).

```sh
sudo containerlab destroy --topo generated/four-radio.clab.yml \
  --node-filter processor --keep-mgmt-net --graceful
sudo containerlab deploy --topo generated/four-radio.clab.yml --format json
```

The new controller resumes native command numbering, stops the surviving streams, reconfigures them and admits a later coordinated start. Verify new processor identity, unchanged radio boot/stream identities, four configurations/starts, and fresh valid detections. This is disruptive re-coordination. The qualified run had zero controller failures but transition data-plane discard/gap counters; it is not a lossless-recovery claim. Recorder replacement and completed capture preservation are now qualified below; switch replacement remains unqualified.

## Recorder replacement and capture preservation

[Fresh ARM64 qualification](../artifacts/recorder-recovery/RESULTS.md) passed. The main recorder counts mirrored frames; finite diagnostic captures run separately. Finish/copy required captures before removing the recorder:

```sh
sudo bash scripts/capture.sh
# Inspect the printed session's manifest/metrics; preserve its files.
sudo containerlab destroy --topo generated/four-radio.clab.yml \
  --node-filter recorder --keep-mgmt-net --graceful
sudo containerlab deploy --topo generated/four-radio.clab.yml --format json
sudo docker logs --tail 40 clab-four-radio-sdr-recorder
sudo bash scripts/capture.sh
```

Each capture owns a distinct directory under artifacts/runtime/captures, preserving earlier files and hashes. Verify new recorder identity, eth1 UP/MTU, and increasing rx_frames; native command success alone is insufficient. The trial preserved the other seven node identities and restored mirrored traffic. Replacement during an active capture produced exit137 and an incomplete manifest with a retained partial PCAP. Completed captures copied to the guest host survived removal and verified after transfer to the Mac. No append/resume or in-container persistence is implied.

Both completed PCAPs were structurally valid but reached the byte cap and reported kernel drops; they are not complete/lossless traffic records. See [capture contract](CAPTURE_PRESERVATION.md) for interruption, retention, flush and durability limits. The final full-lab teardown emitted a recorder APPLICATION_ERROR while still removing all resources; graceful application shutdown under that teardown is not established. Switch replacement remains unqualified.

## Disruptive fallback

Native full redeployment was also qualified:

```sh
sudo containerlab redeploy --topo generated/four-radio.clab.yml \
  --cleanup --graceful --format json
```

This removes and recreates **all eight nodes** and their writable state. Collect required evidence first. Recheck the complete workflow; previous runtime identities and log subscriptions are invalid.

## Unsupported claims

- Docker stop/start alone removed the detector's eth1 and did not recover traffic.
- Subsequent native deploy/reconciliation restored the link, but did not replay this existing node's exec-based address/MTU setup. It returned success with the wrong MTU and missing IPv4 address.
- These bounded trials do not qualify switch, arbitrary node kinds, AMD64, lossless failover or arbitrary persistent-state recovery. Those need their own acceptance cases.

A future generic GUI integration should bind operations to the current enrolled container identity, treat replacement as a new runtime instance, cancel old log streams, and separate native command completion from observed traffic recovery. The four-radio node names and measurements belong to this test fixture, not generic GUI or validation logic.
