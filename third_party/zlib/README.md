# zlib attribution

Save-file compression uses unmodified zlib by Jean-loup Gailly and Mark Adler.
Only its license is retained here; no library source is vendored.

Release CI checks out https://github.com/madler/zlib at v1.3.1 and builds the
`zlibstatic` target through `VYRUZ_ZLIB_SOURCE`. Local builds find installed zlib.
License source: https://github.com/madler/zlib/blob/v1.3.1/LICENSE .
