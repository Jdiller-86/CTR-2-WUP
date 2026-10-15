# Checked audio streams and native playback

Scoped #115 under #25/M3 connects the checked CSTM/ADPCM reader to bounded
voices, fixed-rate stereo mixing and an original native listening diagnostic.
Implementation and independent validation are in progress on the focused branch.
The final contract and verified evidence will be recorded before merge.

This checkpoint uses finite, owned native clips. It does not implement the
3DS DSP firmware/IPC protocol or synchronized real-time game playback. Real
NDSP sends voice queues through shared DSP memory and needs a component setup;
a custom play opcode would not establish that compatibility. No firmware or
commercial audio is bundled.
