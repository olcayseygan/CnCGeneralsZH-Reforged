# Native SDL Modern-water materials

`ModernWater.ini` retains its existing exact RGB and case-insensitive texture-basename
matching. Both lists must match and the first enabled rule wins. Omitted fields and
the shipped polluted-water rule use zero for every new control, so existing toxic,
clear, Classic and Direct3D water retain their appearance. Invalid edits remain
transactional and report their line rather than partially installing a rule.

The optional rule fields accept finite values from 0 to 1:

| Field | Effect |
| --- | --- |
| `Turbidity` | Adds neutral transmission extinction, `exp(-0.30 * value * path)` with path limited to 80 world units. |
| `SurfaceRoughness` | Raises optical roughness toward at least 0.85 and reduces reflected scene contrast toward the sky. |
| `RippleDamping` | Flattens optical wind normals toward 12% of their original horizontal slope; geometry stays unchanged. |
| `WindResistance` | Reduces incident physical wave amplitude by `(1-value)^2`; 0.5 gives 25%, 0.8 gives 4%, and 1 removes incident wind waves. |

Wind resistance uses the same gain in the analytic CPU mesh/cache and native shader,
and in native FFT displacement before limiting and vessel composition. The dynamic
FFT pixel sampling applies the gain to displacement and derivatives; unresolved slope
variance follows gain squared. The shared FFT clock, spectrum, direction and heading
field remain shared. The final nonlinear displacement limiter means large limited
crests need not have an exact final height ratio. Independently generated vessel waves
and bubbles remain active; `Bubbles` still controls bubbles separately. Changing the
effective matched map material invalidates cached analytic samples. Unsupported native
paths keep the previous behavior. The optional Clean FFT provider and its failure
fallback retain their prior ownership and bounds.

## Repeatable capture and causal checks

Use a fixed map, paused time, camera and water style in a native SDL runtime. Start
with `R:50 G:75 B:50` and `TSWater03.tga`, the existing supplied polluted-water match.
Capture the zero-valued rule first, then edit one field at a time. Restart the game
between rule edits because the file is loaded during engine initialization. Repeat with a white
clear-water material and with a mismatched texture: neither may inherit controls from
the previous map. Keep quality, reflection mode and Clean FFT/heading settings fixed
within each comparison, then repeat under their on/off combinations.

For turbidity compare the same shallow shelf, flat-bottom pool, coast and deep water;
for roughness compare reflected shore contrast; for ripple damping compare normals
against unchanged silhouettes. For wind resistance compare 0, 0.5, 0.8 and 1 at the
same time: inspect displaced silhouettes and moving surface samples, not just specular
contrast. Keep bubbles enabled for the calm case, then disable only `Bubbles` to verify
independence. Repeat Classic and Direct3D controls and compare to the zero-valued case.
No new preview executable, environment switch or path-tracing dependency is required.

`test_modern_water_config` checks parsing, neutral defaults, rejected edits, independent
matching and the CPU fallback's height/slope consistency. The retained
`check_gpu_material_wind_displacement` in `test_sdl_gpu_frame` feeds fixed nonzero
spectrum texels to the production vertex and pixel displacement helpers and compares
both against an independent CPU pre-limit gain and limiter at 0/0.5/0.8/1, including
the sampled pixel derivative. Removing the
physical gain must fail the nonzero-resistance cases. The adjacent GPU material fixture
checks the optical controls and independent bubble gate. Whole-map captures and runtime
cost remain integration checks; shader compilation alone does not establish them.
