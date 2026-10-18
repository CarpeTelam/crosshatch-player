# Display lab test images

Four 480 x 800 grayscale PNGs for the lab's gray viewer (story dlab-g3, decision D16). Copy them to
`/.crosshatch/display-lab/images/` on the SD card. They are generated, not photographed, so every device paints the
same pixels and photos stay comparable over time.

| File | Shows |
| --- | --- |
| `ink-sphere.png` | pen-and-ink line art: a cross-hatched sphere with a highlight, a hatched cube, stroke weights 1 to 8 px, a stipple ramp |
| `hatch-and-weights.png` | hatch pitches 2 to 12 px, cross-hatch at the same pitches, concentric ellipses at weights 1 to 12 px, a wave |
| `gradients.png` | a smooth vertical ramp, a 16-step ramp, a radial gradient, a halftone dot ramp |
| `text.png` | anti-aliased text at 12 to 60 px, mid-gray and light-gray text, white text on a dark block |

Line art and text are the viewer's plain-quantisation case (edges stay edges); the gradients are its dither case.
