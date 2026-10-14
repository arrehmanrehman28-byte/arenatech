# Sample Project

`sample-project.aep` is the bundled beginner project (1080p30, one clip,
two markers, two subtitle track stubs). Open it from **File ▸ Open Sample
Project**: the bundle is copied into your data folder (never edited in place)
and opened, so it can be saved and relinked like any project.

The referenced media file `sample/clip01.mp4` (128 KB) **is committed** — the
same CC0 clip the test suite uses as a fixture (`tests/fixtures/clip01.mp4`)
— so the bundled project is fully self-contained in a source checkout.
License: CC0 (public domain).

If the clip is missing on disk (e.g. a trimmed-down install), the Media Bin
shows its "Missing" badge and the Relink action — the missing-media
workflow works as designed.
