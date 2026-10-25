# Seasonal artwork

Four themes are bundled: halloween, newyear, easter, and kupala. Each has wide,
compact, and narrow compositions. Transparent PNG atlases store animation
frames; manifest.json records frame geometry and timing.

Run `python script/build_seasonal_assets.py` with Pillow to regenerate the
atlases from script/seasonal_sources and script/seasonal_hd/sources. Builds
use the committed PNGs and do not require Python or Pillow.

Auto selects a theme using the local calendar date. Manual and disabled
choices are preserved after the one-time Auto migration.
