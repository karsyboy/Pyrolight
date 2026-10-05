# Pyrolight artwork

`logo-no-text.png` is the transparent source mark; `icon.png` is the padded
square version used by the README. Regenerate all shipped platform assets with
`python3 scripts/generate-branding.py` (Pillow required). Existing Moonlight
asset filenames are retained for easier upstream merges; their artwork is
Pyrolight's flame-and-crescent logo.

The mark was generated with the built-in imagegen tool, using Pyroshine's
`assets/logo-no-text.png` as the style reference and transparent output enabled.

## Generation prompt

> Use case: logo-brand. Create a new sibling logo for Pyrolight, the game-streaming client paired with Pyroshine. Input image is a style/reference image, not an edit target. Match the reference's polished sculptural flame ribbons, saturated red and orange at the base, luminous gold/yellow highlights, crisp swooping curves, and small golden streaming orbit trails. For Pyrolight make the main flame silhouette somewhat simpler and balanced for a desktop application icon, with a luminous golden crescent moon in the central negative space instead of the reference's four-point star. Keep the crescent clear, recognizable, open to the right, inside the flame. One centered isolated emblem, generous transparent margin, square canvas, truly transparent background. No text, letters, wordmark, dark background, frame, mockup, watermark, or tiny decorative clutter. Professional coherent sibling branding, clean edge alpha, readable at small sizes.
