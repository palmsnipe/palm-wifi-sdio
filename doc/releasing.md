# Releasing Palm Wi-Fi SDIO

GitHub Actions builds the public patch kit from `main` using the same private, pinned SDK image as PalmTLS. The repository needs the `PALM_TOOLCHAIN_REGISTRY_TOKEN` Actions secret with read access to that image. Check mode runs the Python packaging tests without that secret or the SDK.

The Actions build uses `make test preview`. It includes the patch shims, the status app, and the installer base. The kit's Python patcher uses the original palmOne installer locally to produce the combined PRC. The original installer, its extracted resources, and the combined PRC stay off GitHub.

## Build and test a candidate

1. Update [the release notes](release-notes.md) and push the release commit to `main`.
2. Open **Actions → Palm Wi-Fi release → Run workflow** on `main`. Choose **candidate** and enter an unused version tag such as `v0.8`.
3. Download the candidate artifact from the successful run. Verify the ZIP with `shasum -a 256 -c PalmOne-E2-WPA2-v0.8.zip.sha256` from its containing directory.
4. Generate the installer with your local stock PRC and test install, WPA2 and DHCP after reset, and Restore Stock on the intended E2. Record the results and the candidate run in `doc/releases/<version>.md` and push it to `main`.

## Publish those same files

Run **Palm Wi-Fi release** again on `main` in **publish** mode. Enter the candidate run ID. The workflow checks the run and commit, creates a draft, and compares the uploaded assets against the candidate before publishing. The release tag points to the candidate's source commit. The release uses the version-specific notes from `main` when present, or the notes from the candidate commit otherwise.

The workflow stops if the version tag or release already exists. If an upload is interrupted, check the draft before retrying to avoid replacing an existing release or its assets.
