# palmOne SDIO WPA2 for the Tungsten E2

Version 0.7 adds WPA2-Personal to the Tungsten E2 with the palmOne SDIO card with NIC ID `801D`, primary firmware `1.1.2`, and station firmware `1.8.0`. It uses the selected network's SSID and takes the BSSID and channel from the Prism scan. Use a broadcast 2.4 GHz WPA2-Personal network with CCMP and 802.11b rates enabled. The AP should allow stations to join without protected management frames.

The stock scan reports whether a network is protected. It does not include the RSN information needed to distinguish WPA2 from other protected modes. The Setup panel asks which security path to use. The driver verifies WPA2-Personal and CCMP during the handshake. [Prism scan result layout](https://raw.githubusercontent.com/torvalds/linux/v5.15/drivers/net/wireless/intersil/hostap/hostap_wlan.h)

## Install from a single PRC

The installer requires the original palmOne v1.2 Wi-Fi software to be installed on the Tungsten E2. It replaces the two Wi-Fi databases and adds the **E2 WPA2 Test** status app.

Download **E2WPA2Patcher.prc** from the v0.8 release assets. It checks and patches the Wi-Fi software already installed on the E2. If the app asks for the original v1.2 software, reinstall it on the E2 before applying the patch. [v0.8 release assets](https://github.com/palmsnipe/palm-wifi-sdio/releases/tag/v0.8)

The release ZIP can also generate a combined PRC on a computer using Python 3 and the original installer. The v0.7 kit uses the individual PRCs described below.

```sh
python3 apply.py --single /path/to/WiFi_Installer1.2.prc palm-wpa2-prcs
```

This writes `palm-wpa2-prcs/E2WPA2Installer.prc`. If you are working from source, `make installer` writes the same file under `build/`. [Build guide](maintenance.md)

```sh
make installer
```

1. Back up the E2. Exit Wi-Fi Setup, turn off Bluetooth, eject the Wi-Fi card, and soft-reset.
2. Transfer `E2WPA2Patcher.prc` to internal memory, or use the `E2WPA2Installer.prc` you generated from the ZIP. Open **E2 WPA2 Installer** and tap **Install WPA2**.
3. After the installer reports success, leave the card out. Power off for five seconds, turn the E2 back on, then soft-reset.
4. Insert the card and continue with [Connect over WPA2](#connect-over-wpa2).

To return to the original version, repeat the first step, open **E2 WPA2 Installer**, and tap **Restore Stock**. Follow the same power and soft-reset sequence before reinserting the card. The installer leaves the separate status app installed. Keep the desktop-generated `E2WPA2Installer.prc` private because it embeds resources from your local palmOne software.

## Generate the individual PRCs

Obtain the original `WiFi_Installer1.2.prc` and unpack this ZIP. From the extracted folder, run the command below with Python 3. The output folder must be new. The patcher verifies the installer and its driver and panel resources before writing the installation and rollback PRCs. [Original on-device installer](https://palmdb.net/app/palmone-wifi-card-driver)

```sh
python3 apply.py /path/to/WiFi_Installer1.2.prc palm-wpa2-prcs
```

## Install the individual PRCs

1. Exit Wi-Fi Setup, turn off Bluetooth, and eject the card. Soft-reset. Install `E2WPA2Test.prc`, `SP3WLAN-E2-WPA2-Preview.prc`, and `WiFiPanel-E2-WPA2-Preview.prc` into internal memory.
2. Turn the E2 off for five seconds, turn it on, and soft-reset with the card still ejected.

## Connect over WPA2

1. Insert the card, open **Wi-Fi Setup**, and select the network. At **Wi-Fi Security**, choose **WPA2** and enter its passphrase. When upgrading from v0.6, selecting WPA2 for the v0.6 test network can import its saved key. Choose **Stock Wi-Fi** for the original security flow when prompted for a different protected network.
2. After Setup, open **E2 WPA2 Test**. Expect **Stage 22 / Detail 0000**. Check the BSSID, channel, and **Entered key saved** or **Used saved key**. If Setup leaves Wi-Fi off, set it to **On** in **Preferences → Wi-Fi** before resetting.

The driver chooses the strongest protected infrastructure BSSID it sees for the selected SSID. With an empty scan cache after reboot it uses the last successful BSSID and channel. Rescan through **Wi-Fi Setup** if the AP moved or changed channel.

## Forget and restore stock Wi-Fi

- To erase a saved key, select the network in **Wi-Fi Setup** and tap **Forget** in **Saved WPA2 Key**. The WPA2 network setting remains, so the next connection requires a new passphrase. To verify this across reboot, turn the Palm off for five seconds, turn it on, soft-reset, and reselect the network in **Wi-Fi Setup**.
- To restore stock Wi-Fi, eject the card and soft-reset. Install only `SP3WLAN-Stock.prc` and `WiFiPanel-Stock.prc` from the output folder, then repeat the power and soft-reset sequence. The original palmOne v1.2 installer can also restore both components.

## Physical results

### v0.8 downloadable patcher

The device owner tested the released **E2WPA2Patcher.prc** on a physical Tungsten E2. **E2 WPA2 Test** reported **Stage 22 / Detail 0000**, Wi-Fi obtained an IP address, and **Restore Stock** worked after reset. [Tested release asset](https://github.com/palmsnipe/palm-wifi-sdio/releases/download/v0.8/E2WPA2Patcher.prc)

### v0.8 installer generated from the ZIP

The device owner tested the installer generated from the v0.8 GitHub Actions candidate on a physical Tungsten E2. **E2 WPA2 Test** reported **Stage 22 / Detail 0000**, Wi-Fi Info showed an IP address, and **Restore Stock** worked after reset. [Tested candidate build](https://github.com/palmsnipe/palm-wifi-sdio/actions/runs/44447940929)

### v0.7 setup and rollback

The device owner reported a successful v0.7 connection from the physical Tungsten E2 to their local network. They also confirmed that **Forget** brought back the empty passphrase prompt after reboot and reinstalling both rollback PRCs restored a stock connection to an open Wi-Fi network.

### Pi bench result

On a physical Tungsten E2, the driver obtained a DHCP lease from a 2.4 GHz WPA2-Personal bench AP. The Palm replied to all three pings before rotation and after each of two group key rotations on the same association. Hostapd logged both handshakes, and a monitor capture confirmed that protected broadcasts used group key IDs 2, 1 and 2 in order.

## Emulator and packaging checks

The emulated E2 completed WPA2, DHCP and encrypted ping using the Pi profile on channel 6 and a different SSID and BSSID on channel 11. The channel 11 fixture advertised optional protected management frames. The channel 11 profile also reconnected with its saved key after a soft reset. Forget returned to an empty passphrase field, and the prompt returned again after the power cycle and soft reset. The Pi fixture completed two group key rotations and authenticated new-key broadcast ARP and protected ping after each rotation. With the preview installed, the stock open-network setup completed on a separate fixture. The generated stock rollback PRCs restored the same open-network setup.

The extracted archive generated five PRCs from the original installer and rejected a mismatched input without creating an output folder. Rebuilding from the archive reproduced the same PRC resource contents.

### Single-PRC installer

On an emulated Tungsten E2 with stock v1.2 Wi-Fi installed, the installer refused to modify Wi-Fi while the card was inserted. After ejecting it, **Install WPA2** installed both patches and the status app. The three databases survived a power cycle and soft reset, and their exported resources matched the individual PRCs. The Palm authenticated to simulated APs on channels 6 and 11 and exchanged protected DHCP traffic. **E2 WPA2 Test** showed **WPA2 link active**, **Stage 22 / Detail 0000**.

**Restore Stock** reinstated the original driver and panel resources, which survived another reset. A deliberately incomplete payload left the original databases intact.

### Downloadable patcher

CloudpilotEmu installed `E2WPA2Patcher.prc` over the original v1.2 software. The patched resources matched the physically tested v0.8 candidate, and the emulated E2 completed WPA2 and protected DHCP after reset. **Restore Stock** recovered the original resource bytes across another reset. An altered stock driver was rejected with the Wi-Fi databases left intact.

## Performance

v0.8 used BearSSL's 32-bit constant-time AES with a generic ARMv4T Thumb build. The XScale CCMP build (`74970c6`) targets XScale with `-march=armv5te -mtune=xscale`. It compiles BearSSL's bitsliced AES encryption rounds in ARM state and uses Thumb for the rest of the shim. CCMP now prepares the AES key schedule on pairwise or group key installation and clears it with the key. BearSSL still expands the stored schedule inside its CTR/CBC-MAC routines. The change adds 1,248 bytes to the driver's link allocation. The total patched ARM code resource goes from 65,488 to 65,360 bytes against a 65,530-byte limit.

The table counts instructions in the compiled shim on Unicorn's PXA270 model, using GCC 16.1.0 for both builds. It measures CCMP on aligned buffers with a key already installed. The benchmark checks ciphertext and tags against an independent AES-CCM implementation and covers unaligned buffers, replay rejection, invalid tags, and group key replacement. [Repeat the benchmark](maintenance.md#ccmp-instruction-benchmark)

| CCMP payload | Seal, v0.8 → performance build | Open, v0.8 → performance build |
| ---: | ---: | ---: |
| 32 bytes | 60,026 → 42,105 (29.9% fewer) | 67,325 → 48,006 (28.7% fewer) |
| 128 bytes | 105,398 → 79,077 (25.0% fewer) | 112,403 → 84,690 (24.7% fewer) |
| 512 bytes | 286,886 → 226,965 (20.9% fewer) | 292,715 → 231,426 (20.9% fewer) |
| 1,510 bytes | 772,320 → 622,789 (19.4% fewer) | 775,112 → 624,275 (19.5% fewer) |

Key installation rises from 121 to 9,494 instructions as schedule preparation moves out of the packet path. With the XScale flags and ARM-state rounds, full-size sealing uses 18.1% fewer instructions than v0.8 before adding the schedule cache. Caching has the greatest effect on short packets.

With the performance patcher installed, CloudpilotEmu completed the four-way handshake and protected DHCP on channels 6 and 11. It authenticated broadcast ARP and pairwise ICMP echo replies before and after both group rotations. The status app showed Stage 22 / Detail 0000 and two verified rekeys. After reset on channel 11, Wi-Fi Setup connected with the saved key and obtained a DHCP ACK. Forget brought back the empty passphrase prompt immediately and after another reset. Restore Stock recovered the original driver and Wi-Fi Setup resources across a reset. Wi-Fi Setup completed on an open AP with both the performance patch and the restored stock software.

Physical measurements to date cover small pings. The v0.7 Pi run averaged 26.6 ms round-trip before rekey and 23.3 ms and 22.0 ms after the two rotations, with all nine pings succeeding. The performance build still needs a [physical A/B run against v0.8](maintenance.md#physical-ab-test). The driver still writes each protected TX frame to the Prism buffer two bytes at a time. A 1,500-byte IPv4 packet uses 792 data-register writes. Time that rewrite on the physical E2 to decide how much a bulk SDIO write could save.

### Passphrase derivation

The follow-up builds BearSSL's SHA-1 compression loop in ARM state so the compiler can combine rotates with arithmetic and logic. The existing PBKDF2 algorithm and its 4,096 iterations are preserved. The CCMP nonce now writes the six packet-number bytes at fixed shifts, recovering 40 bytes of code in the combined build. The patched ARM resource is 65,496 bytes, with 34 bytes of headroom.

On the same Unicorn PXA270 model, the SHA-1 compression test went from 2,495 to 1,945 instructions. One complete WPA2 passphrase derivation went from 64,896,085 to 55,932,943 instructions, a reduction of 13.8% from the XScale CCMP build. The derived 32-byte key matched Python's PBKDF2-HMAC-SHA1 implementation. This measures the cost of entering or changing a passphrase. Reconnects with a saved key already reuse the derived key.

The follow-up patcher passed fresh passphrase entry, protected DHCP, and encrypted ping after both group rotations in CloudpilotEmu. Restore Stock recovered the original driver and panel resources after reset. The GitHub Actions toolchain image also built and audited the vendor-free patcher and ZIP locally.

An experiment following PalmTLS's word-wide copy approach used 2,295 instructions instead of 9,065 for a 1,510-byte copy between aligned buffers. The maximum saving for one such copy is about 1% of the CCMP work for a packet of that size. The ARM-state hash loop gives a larger gain for the passphrase path.

## Build

The archive includes the payloads' source, BearSSL source and notices, and the patching tools. To rebuild from the current source checkout or a bundle with a top-level Makefile, use the compatible Palm OS toolchain and the included BearSSL tree. For the original v0.7 archive, follow the build instructions included in that archive.

```sh
make test preview PALM_TOOLCHAIN_ROOT=/path/to/palm-toolchain
```
