# Original PICA reference vectors

These adapters and deterministic vectors are original CTR2WUP test code. They
contain no commercial commands, shader programs, assets or proprietary firmware.

`libctru_builder.c` links the unmodified `libctru/source/gpu/gpu.c` from the clean,
audited external libctru pin `9b55eda44cf80b971503991e0c78f9bc8fe50425`. The
external code stays outside the repository and runtime. The upstream permission
is in its README; the existing [notice](../../docs/licenses/libctru.txt) is retained.

`tools/validate-pica.py` checks its actual little-endian output against independent
original layouts, then compares host and portable-C inspection. libctru uses
256-parameter chunks, so this oracle cannot establish larger raw-record count
semantics. Separate raw fixtures are labeled as such in the receipt.

Parsing or state-only acceptance does not establish PICA draws, shaders, GX2
translation, hardware timing or console behavior. Console testing remains pending.
