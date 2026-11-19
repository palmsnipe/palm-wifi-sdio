## Single-PRC installation

The new installer applies the WPA2 driver and Wi-Fi Setup patches and installs the **E2 WPA2 Test** status app. It also has a **Restore Stock** button. Extract the ZIP and run this with your original palmOne v1.2 installer.

```sh
python3 apply.py --single /path/to/WiFi_Installer1.2.prc palm-wpa2-prcs
```

Transfer the generated `E2WPA2Installer.prc` to internal memory on the Tungsten E2. Follow the installation guide in the ZIP to prepare the device, run the installer, and reset before using the card. The one-file build contains resources from your local palmOne installer and should be kept private.

Install, reboot, WPA2 and DHCP, and stock restore passed on an emulated Tungsten E2. Physical results are recorded in the [installation and test guide](https://github.com/palmsnipe/palm-wifi-sdio/blob/main/doc/palmone-e2-wpa2.md#physical-results).
