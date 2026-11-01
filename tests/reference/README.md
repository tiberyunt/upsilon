# Test reference implementation

The Python `upsilon` package here is retained as an independent encoding and
execution oracle for regression tests. Production tools are C binaries.
`make test` sets PYTHONPATH to this directory. To run tests manually from the
repository root, use `PYTHONPATH=tests/reference python3 -m unittest discover -s tests`.
