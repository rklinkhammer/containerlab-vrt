# Transition discard classification

Before implementation/execution: preserve the existing packet acceptance decisions, cumulative legacy malformed total and health policy. Add a bounded exhaustive reason partition. Valid signal envelopes/sample payloads arriving before valid context must count as waiting_context; malformed envelopes/data/context must remain separately rejected. Invalid data without context must not be mislabeled as waiting for context. No payload or per-packet logging.

Independent fixtures: valid signal before context produces no spectrum and one waiting_context; invalid data without context produces invalid_data; bad envelope and size/listener limits produce their own reasons; malformed context produces invalid_context; empty samples produce invalid_samples; valid context then two correct signal halves yield the unchanged expected tone and no added discards; fresh processor counters start at zero. Sum of reasons equals legacy total; JSON counters expose all fields.

Runtime: repeat the existing bounded processor replacement in a NEW dedicated ARM64 VM. Both data/control recovery must still pass. Record actual reason counts (do not expect exactly 130), compare early and late sustained snapshots for growth, preserve logs and hashes. Do not retroactively assign reasons to the old 130 counts. Destroy the exact lab and stop the new VM.

Fixture correction before runtime: the native encoder refuses an empty signal, so the first local test stopped during fixture construction. Preserve that failed run; exercise invalid_samples with a natively encoded 2049-pair signal exceeding the 2048 FFT limit instead. Also cover timestamp underflow from an independently constructed gap at UTC zero. Expected rejection remains unchanged.
