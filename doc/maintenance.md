# Maintaining the Tungsten E2 driver

Use this repository for driver, Wi-Fi Setup, and packaging changes. Build the release here, generate the patched PRCs with a local copy of the stock installer, and test those PRCs in the CloudpilotEmu fork and on the physical E2. The emulator keeps its own fixtures for the virtual SDIO card and simulated AP.

- [apply.py](../apply.py) is the patcher entry point in the checkout and the release ZIP.
- [driver/](../driver) contains the native ARM patch for the physical card.
- [panel/](../panel) contains the 68K Wi-Fi Setup patch and the credential layout shared with the driver.
- [wpa2/](../wpa2) contains the WPA2 handshake, key handling and CCMP code.
- [app/](../app) builds the E2 WPA2 Test status app.
- [tools/](../tools) contains the stock resource patcher and release bundle builder.
- [tests/test_release.py](../tests/test_release.py) checks how the release ZIP is assembled and applied.

## Build and check a change

1. Branch from `main` for the change. Keep the published tag and assets for each tested release.
2. Run the release tooling tests with Python 3. These use a synthetic bundle and an invalid installer.

```sh
python3 -m unittest discover -s tests -v
```

3. Build a preview with the Palm OS toolchain. The Makefile uses the included BearSSL tree by default.

```sh
make test preview \
  PALM_TOOLCHAIN_ROOT=/path/to/palm-toolchain
```

4. Verify the preview against the locally supplied palmOne installer. The tests check the five output files, Palm OS database identities, and exact stock rollback hashes.

```sh
PALMONE_INSTALLER=/path/to/WiFi_Installer1.2.prc \
PALMONE_RELEASE_ZIP=build/PalmOne-E2-WPA2-Preview.zip \
python3 -m unittest discover -s tests -v
```

Use the root patcher with the generated build files to create PRCs directly from the checkout, or run the same entry point after extracting the ZIP.

```sh
python3 apply.py /path/to/WiFi_Installer1.2.prc palm-wpa2-prcs
```

The build directory is generated. The release ZIP keeps the seed used for that build along with the patch payloads and their source. Keep the original installer and generated stock and patched PRCs local. The published archive contains the patcher and its payloads.

The RSN source retains a few probe routines from the tested v0.7 build. The ARM linker discards them. Removing them from the source changed the compiler's output for the live AES key unwrap routine, so the layout change keeps that source intact to reproduce v0.7.

## Driver and protocol changes

Use the emulated E2 to check association, DHCP and traffic on two AP profiles. Verify reconnect from a saved passphrase, Forget after reboot, the stock open-network path, and both group rekeys. Repeat the affected flows on a physical E2 before tagging the next release. Compare performance changes against v0.7 with the same card and AP, recording sustained throughput and packet loss.
