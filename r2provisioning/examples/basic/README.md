# basic

Minimal sketch where `r2provisioning` owns the WiFi setup.

```sh
pio run -e esp32dev -t upload && pio device monitor
```

- **Not provisioned:** the device starts an open hotspot `r2-XXXXXX`. Join it, then talk to `http://192.168.4.1`.
- **Provisioned:** the device joins the stored WiFi, retrying forever, and prints the stored custom data.
- Type `r` in the serial monitor to erase the provisioning record and restart into hotspot mode.

## Provisioning without the iOS app

```sh
curl http://192.168.4.1/ping
# {"name":"r2provisioning","version":"0.0.1","id":"A1B2C3","maxDataSize":512,"state":"idle"}

# Body: u8 ssidLen | ssid | u8 pwLen | password | u16 dataLen (LE) | data
# Example: SSID "home", password "secret123", data 00 01 02 FF
printf '\x04home\x09secret123\x04\x00\x00\x01\x02\xff' |
  curl -i -H 'Content-Type: application/octet-stream' \
       --data-binary @- http://192.168.4.1/provision
```

The device replies `202` and tries to join the network while keeping the hotspot up. Poll `/ping` for the result:

- `"state":"joined"`: the credentials work and are stored. The device restarts about one second after this ping.
- `"state":"failed","error":"..."`: nothing was stored and the hotspot stays up, so you can send again. The error is one of `ssid_not_found`, `connect_failed` (usually a wrong password), `timeout` or `storage_failed`.

Your machine may drop off the hotspot for a moment while the device joins, because the hotspot moves to the router's channel.

Other replies to the POST: `400` for a malformed payload, `409` while an attempt is already running, and `413` when the data is larger than `maxDataSize`.
