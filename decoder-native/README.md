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
`.github/workflows/build-yurlaps-decoder.yml` dari root repository. Artifact menghasilkan `YurLapsDecoder.exe`
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

Format pesan kompatibel dengan output OpenStint:

```text
P <timestamp_ms> <OPN|AMB> <id> <rssi_dbfs> <hits> <duration_us> <mer_db>
S <timestamp_ms> <noise_dbfs> <dc_offset> <frames_received> <frames_processed> <jitter_p95_ms>
T <timestamp_ms> OPN <id> <transponder_timecode>
```

Passing baru dikirim setelah minimal dua frame untuk ID yang sama dan quiet
window 250 ms. Timestamp berasal dari sample awal preamble. `duration_us`
hanya diisi jika bentuk waveform memberikan crossing yang cukup jelas; jika
tidak, nilainya `0`. Pesan RC4, RC4 Hybrid, Vostok, dan learning tetap tidak
masuk ke output race.
