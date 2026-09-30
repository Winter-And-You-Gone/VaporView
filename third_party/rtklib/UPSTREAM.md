# RTKLIB source provenance

The bundled sources are RTKLIB 2.4.3 b34 from the official repository:
https://github.com/tomojitakasu/RTKLIB/tree/180043ee24b6d2b168f98b64be15f69d50046b1a

The PPK integration adds the unmodified upstream `ephemeris.c`, `lambda.c`,
`ionex.c`, `options.c`, `pntpos.c`, `postpos.c`, `ppp.c`, `ppp_ar.c`, `rinex.c`, `rtkpos.c`,
and `tides.c`. The existing `rtklib.h` retains VaporView's stream position expiry
fields. See LICENSE.txt for the BSD license.

RTKLIB compile definitions describing observation struct sizes (`NFREQ=5`,
`NEXOBS=3`) and enabled satellite systems must be shared by all consumers.
