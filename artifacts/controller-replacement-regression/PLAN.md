# Controller replacement regression

Before execution: preserve radio runtimes and stream identities, establish coordinated streaming and more than 100 status commands, destroy only ProcessorController, instantiate a fresh controller, and require reconfiguration/admission within 5 seconds. Existing same-controller reconnect must continue to pass without extra starts. Do not disable native monotonic-ID/replay checks or guess new IDs. Confirm whether a supported association transition preserves the topology stream IDs before selecting any fix.
