# YurLaps Decoder clean-room build

Upload the contents of this folder to the repository root. The workflow is in
`.github/workflows/build-yurlaps-decoder.yml`.

Run it from GitHub Actions. It builds `YurLapsDecoder.exe` with RTL-SDR and
ZeroMQ enabled. The decoder accepts OPN and legacy AMB/RC3 frames and groups
hits into one passing with a 250 ms quiet window. RC4, RC4 Hybrid, Vostok, and
learning messages are ignored.

The source in this folder is an independent implementation. It does not
contain OpenStint source files or an OpenStint attribution file. External
libraries used by the Windows build retain their own licenses.
