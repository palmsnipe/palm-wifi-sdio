# Releasing Palm Wi-Fi SDIO

GitHub Actions builds the public patch kit from `main` using the same private, pinned SDK image as PalmTLS. The repository needs the `PALM_TOOLCHAIN_REGISTRY_TOKEN` Actions secret with read access to that image. Check mode runs the Python packaging tests without that secret or the SDK.

The Actions build uses `make test preview patch-prc` to create the ZIP and `E2WPA2Patcher.prc`. It verifies the contents and records a SHA-256 checksum for each file. The patcher uses the v1.2 software already on the E2. The release build needs no palmOne installer or extracted palmOne databases.

## Build and test a candidate

1. Update [the release notes](release-notes.md) and push the release commit to `main`.
2. Open **Actions → Palm Wi-Fi release → Run workflow** on `main`. Choose **candidate** and enter an unused version tag such as `v0.9`.
3. Download the artifact from the successful run. From the extracted directory, run `shasum -a 256 -c PalmOne-E2-WPA2-v0.9.zip.sha256 E2WPA2Patcher.prc.sha256`, substituting the version you built.
4. Test the candidate's `E2WPA2Patcher.prc` on the intended E2 with palmOne v1.2 Wi-Fi installed. Check install, WPA2 and DHCP after reset, and Restore Stock. Record the results and the candidate run in `doc/releases/<version>.md` and push it to `main`.

## Publish those same files

Run **Palm Wi-Fi release** again on `main` in **publish** mode. Enter the candidate run ID. The workflow checks the run and both checksums, uploads the ZIP and PRC with their checksum files to a draft, and downloads all four assets for a byte-for-byte comparison before publishing. The release tag points to the candidate's source commit. The release uses the version-specific notes from `main` when present, or the notes from the candidate commit otherwise.

The workflow stops if the version tag or release already exists. If an upload is interrupted, check the draft before retrying to avoid replacing an existing release or its assets.
