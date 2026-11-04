#ifndef OK_VERSION_H
#define OK_VERSION_H

// the version stamp, P1's single source of truth. `ok --version` prints
// it, the capi harness composes it (drift between the numbers and the
// string fails the gate), the bench workflow reads it.
//
// all five defines move together at a version bump: the three numbers,
// the lane, and the composed string. the lane is the pre-release channel
// while a wave is in flight ("rc.1" below, per the roadmap's stamp
// ruling); release day sets it to the empty string and the stamp collapses
// to the bare version.
//
// the abi is NOT here - it lives in ok_module.h (OK_ABI_MAJOR.MINOR, the
// patch-52 semver encoding) because it belongs to the native-module
// contract, not the version. the stamp prints both; the truth stays
// where it lives.

#define OK_VERSION_MAJOR 0u
#define OK_VERSION_MINOR 9u
#define OK_VERSION_PATCH 0u

// mid-wave: the Reconciliation rc. "" on release day.
#define OK_VERSION_LANE "rc.1"

// hand-composed from the four above; tests/capi re-composes and compares.
#define OK_VERSION_STRING "0.9.0-rc.1"

#endif // OK_VERSION_H
