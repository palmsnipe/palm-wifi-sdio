## Single-PRC installation

Download `E2WPA2Patcher.prc` from the release assets and transfer it to internal memory on a Tungsten E2 with palmOne v1.2 Wi-Fi software installed. The app applies both WPA2 patches and installs the **E2 WPA2 Test** status app. It also has a **Restore Stock** button.

The ZIP also supports generating a combined installer. Extract it and run this with your original palmOne v1.2 installer.

```sh
python3 apply.py --single /path/to/WiFi_Installer1.2.prc palm-wpa2-prcs
```

Transfer the generated `E2WPA2Installer.prc` to internal memory on the Tungsten E2. Follow the installation guide in the ZIP to prepare the device, run the installer, and reset before using the card. The one-file build contains resources from your local palmOne installer and should be kept private.

Install, reboot, WPA2 and DHCP, and stock restore passed on an emulated Tungsten E2. Physical results are recorded in the [installation and test guide](https://github.com/palmsnipe/palm-wifi-sdio/blob/main/doc/palmone-e2-wpa2.md#physical-results).
