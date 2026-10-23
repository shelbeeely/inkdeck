# Phone location over Bluetooth LE

InkDeck can record where each journal session was written by reading your
phone's location over Bluetooth LE. The phone runs a small **GATT server**; the
X4 connects to it when a session starts. This page is the contract for that
server.

Enable it with `location = phone` (flasher page, `/inkdeck.txt`, or the setup
page's settings). `location_every` (minutes, default 15) sets how often a fresh
location is taken: a session opened sooner reuses the previous one.

## What the X4 does

When today's journal entry opens (each `## HH:MM` session), and the last
location is older than `location_every` minutes:

1. Scans for about **4 seconds** for a connectable device advertising the
   InkDeck location **service UUID**. If several answer, it picks the strongest
   signal.
2. Connects, reads the **location characteristic** once, and disconnects.
3. Ignores fixes older than **30 minutes** (by the fix time you send).
4. Writes the result into the session's record in the entry's front matter at
   the next save:

   ```yaml
   location: {lat: 45.523100, lon: -122.676500, accuracy_m: 12, time: 2026-10-23T09:59:50-07:00, source: phone, place: "Portland"}
   ```

This runs in the background: typing is never blocked. The keyboard stays
connected (the X4 allows three connections). If no phone answers, the session
simply has no location.

## UUIDs

| | UUID |
|---|---|
| Service | `ff3f25e1-53df-4bf1-9794-eac66727467a` |
| Location characteristic | `ff3f0001-53df-4bf1-9794-eac66727467a` |

## Advertising

- Advertise as **connectable**, with the **service UUID in the advertisement**
  (or scan response). A 128-bit UUID takes 18 of the 31 legacy advertising
  bytes, so leave the device name out of the advertisement if space runs short.
- **Android**: `BluetoothLeAdvertiser` with `AdvertiseData.addServiceUuid(...)`
  works from a foreground service, including with the screen off.
- **iOS**: only advertise while the app is in the **foreground**. In the
  background iOS moves service UUIDs into an Apple-only "overflow" area that
  the X4 cannot see.

## Location characteristic

- Properties: **Read**. Notify is allowed but not used.
- Permissions: plain read, **no encryption required**. If the characteristic
  does require encryption, the X4 pairs once (Just Works) and keeps a bond,
  which uses one of its three bond slots (shared with keyboards).
- Value: UTF-8 text, at most 200 bytes, fields separated by `;`:

  ```
  1;<latitude>;<longitude>;<accuracy m>;<fix time, unix seconds>;<place>
  ```

  | Field | Notes |
  |---|---|
  | `1` | Format version. Anything else is rejected. |
  | latitude, longitude | Decimal degrees, WGS84, `.` as the decimal point. |
  | accuracy | Horizontal accuracy radius in metres (may be fractional). |
  | fix time | When the phone got this fix, unix seconds (UTC). |
  | place | Optional, may be empty, may contain `,` and spaces (everything after the 5th `;`). A reverse-geocoded name such as `Portland, Oregon`. |

  Examples:

  ```
  1;45.523100;-122.676500;12;1792778390;Portland, Oregon
  1;51.507400;-0.127800;35.5;1792778390;
  ```

- Keep the value current: update it from the phone's location updates (the X4
  reads whatever is there when it connects). Return the latest fix even if it's
  a few minutes old; the fix time tells the X4 how old it is.

## Testing without writing an app

On Android, **nRF Connect** can act as the server:

1. *Configure GATT server* → add a service with the service UUID above, and a
   characteristic with the location UUID, property **Read**, permission
   **Read**, initial value as text, e.g. `1;45.5231;-122.6765;10;<now>;Test`
   (current unix time from any epoch converter).
2. *Advertiser* → new advertising packet, *Connectable*, add the service UUID
   to the advertising data, start it.
3. On the X4, set `location = phone`, open *Today's journal*, type a line and
   save. The entry's front matter gets the location.

## Android sketch

```kotlin
val SERVICE = UUID.fromString("ff3f25e1-53df-4bf1-9794-eac66727467a")
val LOCATION = UUID.fromString("ff3f0001-53df-4bf1-9794-eac66727467a")

@Volatile var value = "".toByteArray()   // update from FusedLocationProviderClient

val server = bluetoothManager.openGattServer(context, object : BluetoothGattServerCallback() {
    override fun onCharacteristicReadRequest(
        device: BluetoothDevice, requestId: Int, offset: Int, c: BluetoothGattCharacteristic
    ) {
        val bytes = value.copyOfRange(minOf(offset, value.size), value.size)  // long reads
        server.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, bytes)
    }
})
server.addService(BluetoothGattService(SERVICE, BluetoothGattService.SERVICE_TYPE_PRIMARY).apply {
    addCharacteristic(BluetoothGattCharacteristic(LOCATION,
        BluetoothGattCharacteristic.PROPERTY_READ, BluetoothGattCharacteristic.PERMISSION_READ))
})

fun onLocation(l: Location, place: String) {
    value = "1;%.6f;%.6f;%.1f;%d;%s".format(Locale.US,
        l.latitude, l.longitude, l.accuracy, l.time / 1000, place).toByteArray()
}

bluetoothManager.adapter.bluetoothLeAdvertiser.startAdvertising(
    AdvertiseSettings.Builder().setConnectable(true)
        .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY).build(),
    AdvertiseData.Builder().addServiceUuid(ParcelUuid(SERVICE)).setIncludeDeviceName(false).build(),
    advertiseCallback)
```

Use `Locale.US` (or another `.` decimal locale) when formatting: a `,` decimal
separator would make the value unreadable.
