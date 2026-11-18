# YurLaps Decoder Native

Independent decoder implementation for YurLaps.

The current implementation accepts only the two protocols that YurLaps
supports:

- `OPN` (YurLaps/OpenStint transponder frames)
- `AMB` (legacy RC3 frames)

RC4, RC4 Hybrid, Vostok, and learning messages are intentionally rejected.
The implementation is written independently from the reference decoder and
includes an OPN/AMB baseband detector for 2.5 MS/s IQ captures. RTL-SDR and
ZeroMQ are optional build features enabled by the GitHub Actions workflow.

## Build tanpa hardware

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Untuk build Windows dengan RTL-SDR dan ZeroMQ, jalankan workflow
`.github/workflows/build.yml`. Artifact menghasilkan `YurLapsDecoder.exe`
beserta DLL yang dibutuhkan.

Mode capture IQ:

```sh
YurLapsDecoder.exe --iq passing.iq
```

Mode RTL-SDR:

```sh
YurLapsDecoder.exe --rtl YD0001 28
```

Output passing dikirim ke stdout dan, bila dibangun dengan ZeroMQ, ke
`tcp://127.0.0.1:5556`.
