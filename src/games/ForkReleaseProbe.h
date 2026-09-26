#pragma once

namespace ForkReleaseProbe {

// Requests the fork's releases/latest once more and returns true only when it
// answers 404, which GitHub does while the repository has no published release.
// The shared HTTP helper reports every non-200 status as a plain failure, so the
// update check calls this after a failed fetch to tell "no release yet" apart
// from a network or server error. Makes a network request; call only after a
// failed release fetch.
bool latestReleaseMissing();

}  // namespace ForkReleaseProbe
