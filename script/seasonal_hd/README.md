# Desktop seasonal artwork

The 1.3.2 wide atlases use the final mobile 1.5.3 masters, with a transparent
safety inset. Their original APNG hashes are in `mobile-master-sha256.json`.
Compact and narrow scenes use the same guarded mobile drawing pipeline in
`mobile_safe.py`, which measures the union of all animation frames before
fitting a scene into its destination. Compact autumn has two pumpkins; its
small face is drawn directly rather than cropped from a padded canvas.

Only halloween (Autumn), newyear (Winter), easter (Spring), and kupala (Summer)
are bundled. Each has wide, compact, and narrow compositions at 20 fps.
The original internal identifiers preserve saved choices across upgrades.
Run `python script/build_seasonal_assets.py` to bake the procedural sources.
Qt uses the committed PNG atlases and does not require Pillow at runtime.
Each atlas has its own QRC unit to bound Windows x86 compiler memory.
