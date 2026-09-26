> Historical pre-fix reproduction commands below. On the updated code, run `ctest --test-dir build/dev -R "processor_controller|command_resumption" --output-on-failure` for the maintained regression; the original fixture does not advertise the new capability.

# Reproduce the controller replacement regression

From the project root, after the documented macOS development setup:

```sh
cmake --build build/dev --target sdr_processor_controller_tests
clang++ -std=c++23 -g -Wall -Wextra -Wpedantic -Werror \
  -Iinclude -isystem third_party/vrt_framework/include -isystem /opt/homebrew/include \
  artifacts/controller-replacement-regression/controller_replacement.cpp \
  build/dev/libsdr_vrt_adapter.a build/dev/libsdr_core.a build/dev/libsdr_telemetry.a \
  /opt/homebrew/lib/libSoapySDR.0.8.1.dylib -pthread -Wl,-rpath,/opt/homebrew/lib \
  -o build/dev/controller_replacement_probe
build/dev/controller_replacement_probe
```

Exit 1 is the recorded current failure, not a passing test. The replacement must configure/start all four radios within the written bound to pass. No VM or Containerlab deployment is involved. The existing test is extended only in this standalone experimental source; normal tests are not changed. TRACE_CHANGE.md describes the bounded diagnostic-only instrumentation in a disposable header copy.
