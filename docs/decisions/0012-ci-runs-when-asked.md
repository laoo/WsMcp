# 0012: CI builds three platforms when asked, and a tag makes a release

**Status:** accepted

## Context

PetMcp's workflow (its records 0013 and 0019) builds and tests on macOS, Linux and Windows, by
hand or on a version tag, and publishes a release from a tag.

## Decision

`.github/workflows/ci.yml` is PetMcp's, renamed: it runs by hand and on tags `v*`, checks the
formatting, builds with warnings as errors (`WS_WERROR`) and runs the suite; the tests that need
ROMs skip. A tag publishes each platform's package.

## Consequences

- The ROM-dependent tests and the regression suite are run where the ROMs are, before a release.
