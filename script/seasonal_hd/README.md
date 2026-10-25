# Desktop seasonal artwork

The bundled themes are halloween, newyear, easter, and kupala, each with wide,
compact, and narrow compositions at 20 fps. Internal identifiers preserve
saved choices across upgrades.

`mobile_safe.py` measures animation bounds and adds a transparent inset before
fitting each scene to its destination. `mobile-master-sha256.json` records the
source APNG hashes.

Run `python script/build_seasonal_assets.py` to regenerate the PNG atlases.
Qt uses the committed atlases and does not require Pillow at runtime. Each
atlas has its own QRC unit to bound Windows x86 compiler memory.
