# Diagnostic repeat, written before execution

Attempt 1 preserved a traffic pass and controller failure, but stopped on an inactive-session assertion with an empty message. Attempt 2 keeps all original expectations and extends controller observation to its own 90-second deadline after traffic recovery (a stricter distinction, not weakening required controller outcomes). Adds actionable failure/traceback. Uses the same still-running task-owned guest with a new clean lab after verified first cleanup; no VM restart or pre-existing VM access.
