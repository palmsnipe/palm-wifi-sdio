# palmOne SDIO WPA2 v0.7 for the Tungsten E2

Version 0.7 adds WPA2-Personal to the Tungsten E2 with the palmOne SDIO card with NIC ID `801D`, primary firmware `1.1.2`, and station firmware `1.8.0`. It uses the selected network's SSID and takes the BSSID and channel from the Prism scan. Use a broadcast 2.4 GHz WPA2-Personal network with CCMP and 802.11b rates enabled. The AP should allow stations to join without protected management frames.

The stock scan reports whether a network is protected. It does not include the RSN information needed to distinguish WPA2 from other protected modes. The Setup panel asks which security path to use. The driver verifies WPA2-Personal and CCMP during the handshake. [Prism scan result layout](https://raw.githubusercontent.com/torvalds/linux/v5.15/drivers/net/wireless/intersil/hostap/hostap_wlan.h)

## Generate the PRCs

Obtain the original `WiFi_Installer1.2.prc` and unpack this ZIP. From the extracted folder, run the command below with Python 3. The output folder must be new. The patcher verifies the installer and its driver and panel resources before writing the installation and rollback PRCs. [Original on-device installer](https://palmdb.net/app/palmone-wifi-card-driver)

```sh
python3 apply.py /path/to/WiFi_Installer1.2.prc palm-wpa2-prcs
```

## Install and connect

1. Exit Wi-Fi Setup, turn off Bluetooth, and eject the card. Soft-reset. Install `E2WPA2Test.prc`, `SP3WLAN-E2-WPA2-Preview.prc`, and `WiFiPanel-E2-WPA2-Preview.prc` into internal memory.
2. Turn the E2 off for five seconds, turn it on, and soft-reset with the card still ejected.
3. Insert the card, open **Wi-Fi Setup**, and select the network. At **Wi-Fi Security**, choose **WPA2** and enter its passphrase. When upgrading from v0.6, selecting WPA2 for the v0.6 test network can import its saved key. Choose **Stock Wi-Fi** for the original security flow when prompted for a different protected network.
4. After Setup, open **E2 WPA2 Test**. Expect **Stage 22 / Detail 0000**. Check the BSSID, channel, and **Entered key saved** or **Used saved key**. If Setup leaves Wi-Fi off, set it to **On** in **Preferences → Wi-Fi** before resetting.

The driver chooses the strongest protected infrastructure BSSID it sees for the selected SSID. With an empty scan cache after reboot it uses the last successful BSSID and channel. Rescan through **Wi-Fi Setup** if the AP moved or changed channel.

## Forget and restore stock Wi-Fi

- To erase a saved key, select the network in **Wi-Fi Setup** and tap **Forget** in **Saved WPA2 Key**. The WPA2 network setting remains, so the next connection requires a new passphrase. To verify this across reboot, turn the Palm off for five seconds, turn it on, soft-reset, and reselect the network in **Wi-Fi Setup**.
- To restore stock Wi-Fi, eject the card and soft-reset. Install only `SP3WLAN-Stock.prc` and `WiFiPanel-Stock.prc` from the output folder, then repeat the power and soft-reset sequence. The original palmOne v1.2 installer can also restore both components.

## Physical results

The device owner reported a successful v0.7 connection from the physical Tungsten E2 to their local network. They also confirmed that **Forget** brought back the empty passphrase prompt after reboot and reinstalling both rollback PRCs restored a stock connection to an open Wi-Fi network.

### Pi bench result

On a physical Tungsten E2, the driver obtained a DHCP lease from a 2.4 GHz WPA2-Personal bench AP. The Palm replied to all three pings before rotation and after each of two group key rotations on the same association. Hostapd logged both handshakes, and a monitor capture confirmed that protected broadcasts used group key IDs 2, 1 and 2 in order.

## Emulator and packaging checks

The emulated E2 completed WPA2, DHCP and encrypted ping using the Pi profile on channel 6 and a different SSID and BSSID on channel 11. The channel 11 fixture advertised optional protected management frames. The channel 11 profile also reconnected with its saved key after a soft reset. Forget returned to an empty passphrase field, and the prompt returned again after the power cycle and soft reset. The Pi fixture completed two group key rotations and authenticated new-key broadcast ARP and protected ping after each rotation. With the preview installed, the stock open-network setup completed on a separate fixture. The generated stock rollback PRCs restored the same open-network setup.

The extracted archive generated five PRCs from the original installer and rejected a mismatched input without creating an output folder. Rebuilding from the archive reproduced the same PRC resource contents.

## Performance

The ARM shim uses Thumb code and `-Os` to fit in the stock driver's resource. The tested ARM code resource is 65,488 bytes against a 65,530-byte limit. The physical Pi bench's clean v0.7 run averaged 26.6 ms round-trip before rekey and 23.3 ms and 22.0 ms after the two rotations. All nine pings succeeded. These measurements cover small packets. Peak throughput and CPU load on the E2 remain unmeasured.

The transmit path reads the stock frame from the Prism TX buffer, applies software CCMP and writes the protected frame back two bytes at a time. A 1,500-byte IPv4 packet with its headers uses 792 data-register writes in that rewrite. A bulk SDIO write is the first improvement to benchmark. The cipher currently initializes the AES key schedule for each packet, which also gives us a second target to measure.

## Build

The archive includes the payloads' source, BearSSL source and notices, and the patching tools. To rebuild, use the compatible Palm OS toolchain and the included BearSSL tree.

```sh
make -C src/uarm/test/wpa2_hardware test preview \
  PALM_TOOLCHAIN_ROOT=/path/to/palm-toolchain \
  BEARSSL_SRC=/absolute/path/to/bearssl-0.6 \
  PREVIEW_ZIP=build/PalmOne-E2-WPA2-v0.7.zip
```
