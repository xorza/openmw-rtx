# Simplification

A simplification moves no picture, except by a named rounding.

- A formula in two places moves into one function: `lib/*.glsl`, or `components/rtx/shaders/*.h` when the host computes it too.
- A literal that repeats a named constant uses the name; a chain of seeds or bindings is derived, not written out.
- A value the frame already carries is read, not rebuilt.
- Dead code: a function no module reaches, a field no shader reads (check the host writes too), a parameter every caller passes alike, a `normalize` of a vector unit by construction.
- A branch a constant never takes goes only where the runtime test stays in front of it.
- Cancel before computing; multiply-then-divide by one quantity goes.
- A value packed at one function's end and unpacked at the next function's start stays unpacked.
- A fitted constant gives way to a closed form, or a host test checks the fit.
- Early returns that repeat a tail become one tail.
