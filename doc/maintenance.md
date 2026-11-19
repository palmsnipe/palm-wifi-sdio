# Maintaining the Tungsten E2 driver

Use this repository for driver, Wi-Fi Setup, and packaging changes. Build the release here, generate the patched PRCs with a local copy of the stock installer, and test those PRCs in the CloudpilotEmu fork and on the physical E2. The emulator keeps its own fixtures for the virtual SDIO card and simulated AP.

- [apply.py](../apply.py) is the patcher entry point in the checkout and the release ZIP.
- [driver/](../driver) contains the native ARM patch for the physical card.
- [panel/](../panel) contains the 68K Wi-Fi Setup patch and the credential layout shared with the driver.
- [wpa2/](../wpa2) contains the WPA2 handshake, key handling and CCMP code.
- [app/](../app) builds the E2 WPA2 Test status app.
- [installer/](../installer) builds the on-device installer and stock restore app.
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

4. Put the original installer at `local/WiFi_Installer1.2.prc` in the checkout. Verify the preview against that copy. The tests check the five output files, Palm OS database identities, and exact stock rollback hashes.

```sh
PALMONE_INSTALLER=local/WiFi_Installer1.2.prc \
PALMONE_RELEASE_ZIP=build/PalmOne-E2-WPA2-Preview.zip \
python3 -m unittest discover -s tests -v
```

Run `make package` to generate the patched and rollback PRCs in `build/`. Set `INSTALLER=/path/to/WiFi_Installer1.2.prc` if your installer is elsewhere. To write to a new folder of your choice, use the root patcher with the generated build files. The extracted release ZIP uses the same patcher with the path to your own copy of the installer.

```sh
python3 apply.py local/WiFi_Installer1.2.prc palm-wpa2-prcs
```

The build directory is generated. Git ignores `local/`, and the release ZIP excludes it. Keep the installer and generated stock and patched PRCs local. The release ZIP contains the patcher, its payloads and their source, and the seed used for that build.

The RSN source retains a few probe routines from the tested v0.7 build. The ARM linker discards them. Removing them from the source changed the compiler's output for the live AES key unwrap routine, so the layout change keeps that source intact to reproduce v0.7.

## Build the single-PRC installer

Run `make installer` to generate `build/E2WPA2Installer.prc` from the same five PRCs used for separate installation and rollback. The generated installer stays in the ignored build directory. The public patch kit includes the installer source, build script, and an installer base without embedded palmOne resources. Its `apply.py --single` option combines that base with resources extracted from the user's original installer.

The on-device app stages incoming databases under private names. It keeps the previous copies through the soft reset because Palm OS can still have the old Wi-Fi panel cached. The next installer action cleans up a completed transaction or rolls back a pending one before applying another change.

The [emulator results](palmone-e2-wpa2.md#single-prc-installer) cover install, reboot, WPA2 and DHCP, stock restore, and failure before activation. See the [release process](releasing.md) for building and publishing a candidate in GitHub Actions.

## Build the downloadable patcher

`make patch-prc` generates `build/E2WPA2Patcher.prc` from the local original installer and the compiled patches. Set `PATCH_PAYLOAD` to the payload directory from the verified release kit when building for an existing release. Use the same directory that supplied the shims and status app for its device test.

```sh
make patch-prc PATCH_PAYLOAD=dist/v0.8-candidate-44447940929/kit/payload
```

The generated PRC carries the compiled shims, the status app, and reversible edits with fingerprints of the three Wi-Fi resources it changes. It copies the original databases from the device and checks the changed resources before activation. Restore Stock reverses those edits using the installed copy. The public patcher contains no complete palmOne databases.

## Driver and protocol changes

Use the emulated E2 to check association, DHCP and traffic on two AP profiles. Verify reconnect from a saved passphrase, Forget after reboot, the stock open-network path, and both group rekeys. Repeat the affected flows on a physical E2 before tagging the next release. Compare performance changes against v0.7 with the same card and AP, recording sustained throughput and packet loss.
