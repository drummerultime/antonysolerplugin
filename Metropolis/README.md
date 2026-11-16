# METROPOLIS

METROPOLIS is a real-time percussion and drum sound-design effect. It keeps the original hit available in parallel while generating a separately shaped layer, so a kick or snare can remain readable under the transformation.

## Engines

- **Impact** adds a tuned, decaying low-frequency body and attack click.
- **Resonator** excites a pitched resonant body from incoming percussion.
- **Reverse** replays the recent capture backwards with a shaped swell.
- **Grain** creates overlapping, pitch-shifted and time-stretched grains from a six-second capture.
- **Stutter** repeats a captured rhythmic slice, with tempo division and probability controls.

Every engine has a dry/wet mix, output control, tone, saturation, reverb, feedback, transient sensitivity and modulation. The resonator now has three tunable partials whose ratios shift from harmonic to metallic; Crush combines sample-rate reduction with bit-depth loss. The Grain engine continues to seed a cloud between hits; Freeze holds the captured input. Five factory scenes demonstrate the different engines and keep their settings independent.

The dynamic filter follows the strength of each incoming drum hit. Choose low-pass, band-pass or high-pass, then set envelope amount to open or close the cutoff on harder hits and Filter Decay to set how quickly it returns. It responds to the Drum Rack audio output, so it needs no MIDI note mapping.

The editor can be resized from its lower-right corner. Its Art Deco layout and control groups scale together while preserving their proportions.

## Build

Requires CMake 3.22+ and JUCE 8. Set `JUCE_DIR` to the JUCE source tree.

```sh
cmake -S . -B build -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

The project builds AU, VST3 and Standalone formats on macOS. The interface uses an original art-deco city motif, antique brass, oxidized teal, ivory and signal red, inspired by the futuristic city in Fritz Lang's 1927 film.
