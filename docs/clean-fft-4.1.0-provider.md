# Clean FFT provider for the 4.1.0 port

`CleanFftWater = yes` in `Options.ini` opts into the native SDL Clean FFT provider.
The default is off. Classic Graphics and other renderers retain their existing water.
`MapAwareWaveHeading` independently enables the clean field's shore turning.

This port keeps one shared three-cascade FFT ocean. It uses the approved fixed
JONSWAP/Hasselmann/Horvath spectrum and secondary swell, wet-only effective depth,
per-band amplitude gains and their spatial derivatives, unresolved slope variance
for glints, crest colour, and horizontal-Jacobian whitecaps. The CPU analytic mesh
remains complete; the existing floor-clearance bound and exact wave cages remain
independent safety constraints.

The baseline binds six separate FFT maps. The heading atlas therefore stays at
pixel slot 15 and vertex slot 4, with four RGBA8 words per cell in clean mode:
travel/gradient, shelter/validity, shore direction/depth/confidence, and shoreline
distance. Ordinary heading retains its two-word transport. Default water declares
neither atlas. Clean water fits five vertex and sixteen pixel sampler bindings.
Missing or incomplete shoreline samples use the far-distance fallback, including
matching empty arrays during inland campaign startup.

Unlike the fork's optimized provider, this bounded port evaluates the hex-tile
field directly. It does not add the texture-array migration, separate floating
point depth/rotation caches, streamed tiles, one-read collapse optimization, or
live tuning and profiling switches. Numeric identities and shader compilation do
not establish equal GPU cost. Matching rendered comparisons and incremental GPU
timings are required before drawing a performance conclusion.

The renderer selects spectrum and shader identities together. Failed FFT mode
allocation preserves the previous resource, but a draw cannot advertise a mode
which that resource does not contain. An optional clean resource, shader, or
pipeline refusal uses the preserved analytic CPU picture and latches that
provider off for the device lifetime. Turning the option off restores the ordinary
water path; recreating the renderer permits a new clean-provider attempt.
