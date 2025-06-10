External dependencies
=====================

DeckLink
--------
**DeckLink SDK API** - a BSD-licensed wrapper library dynamically calling
actual library.

GPUJPEG
-------
Script `bootstrap_gpujpeg.sh` tries to bootstrap _GPUJPEG_ statically without
installing. However, using GPUJPEG as a standalone library is preferred if possible.

Zfec
----
Code of Reed-Solomon error correction. If submodule is initialized, UltraGrid
compiles the code automatically (the files `fec.h` and `fec.c` only need to be
present on predefined location).

RapidCheck
----------
Script `bootstrap_rapidcheck.sh` downloads and prepares _RapidCheck_ for
property-based testing without system installation. RapidCheck is used for
testing overlay and alpha blending functionality with randomly generated inputs.

