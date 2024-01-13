# "Reflections on Trusting Trust" in TinyCC

This repository implements the [Reflections on Trusting Trust][1] (RoTT) attack
in [TinyCC][2]. It has two trojans that it can inject into other programs,
namely:

  * It will cause the `login.c` file in the `tests_rott` directory to
    unconditionally allow a user identifying as `"ken"` to login.
  * It will cause the `su` toy in (my fork of) [Toybox][3] to unconditionally
    succeed when using the password `"ken"`.

Furthermore, this fork of TinyCC can be used to compile a "clean" copy of TinyCC
(commit `6120656`), and it will propagate the two trojans to the newly built
compiler.

The source for the trojans is in `tccpp_rott-pre.inc`, which is processed by
`tccpp_rott-gen.py` to be included in `tccpp.c`. The trojans match based on a
trigger string expected to be in the file, along with the name of the file
itself. Note that filenames are matched based on their entire path given on the
command line. So, for example, compiling `login.c` could potentially cause its
trojan to activate, but compiling `./login.c` won't.

[1]: https://www.cs.cmu.edu/~rdriley/487/papers/Thompson_1984_ReflectionsonTrustingTrust.pdf
[2]: https://bellard.org/tcc/
[3]: https://github.com/ammrat13/toybox-rott
