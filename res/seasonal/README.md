# Approved seasonal artwork

Ten themes, each with wide, compact and narrow compositions. PNG atlases contain transparent frames; manifest.json records timing and geometry. The application loads only the current composition and pauses drawing when hidden. PNG resources avoid platform GIF plugins and dark baked backgrounds.

Assets were baked from user-approved renderers in script/seasonal_sources. Run python script/build_seasonal_assets.py with Pillow to regenerate. The May renderer uses Windows Consolas for calendar labels. The archived original Qt drawing supplies exact New Year geometry. Builds consume committed PNGs and do not require Python/fonts to generate artwork.

Auto selection uses local dates; exact-date events override the season. Memorial themes cannot be selected manually. The first 1.3 startup selects Auto once for new and existing installations. A persisted migration marker preserves any subsequent manual or disabled selection across restarts and future upgrades.
