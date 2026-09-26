# Native recovery interfaces

**Documented:** read-only local Containerlab source at `5ae50094a3afd70e4e1674fe5385e64d8979da26` (`v0.79.0`, clean checkout) contains:

- `core/deploy.go`: Deploy dispatches existing managed labs to native reconciliation; checkReconcileDeployOptions rejects node-filter for a deployed lab.
- `core/apply.go` and its tests: native apply tracks restarted/recreated nodes and missing link endpoints.
- `cmd/destroy.go`: supports node-filter and explicitly rejects combining it with cleanup.
- `cmd/redeploy.go`: calls native destroy then native deploy, covering full-lab recovery.

**Observed:** the downloaded, checksum-verified release reports version 0.79.0 and commit 5ae50094a, matching the inspected source prefix (`build.log`). Installed help is also retained with the runtime evidence.

**Inferred before trial:** a full-topology native reconciliation may restore a detector link lost by Docker restart without replacing other nodes. This inference must be checked against identities, interface/address/MTU and actual recovered traffic. Full native redeployment is the bounded fallback and entails a lab-wide outage.
