# Zephyr Bluetooth Sounding Sensor Testground

If you find this repo, don't look at it. It'll probably be messy, full of weird hacky workarounds and bad code while I work out the best way to implement distance based sensor and peripheral stuff using the nRF54L15. 

If that opening paragraph didn't put you off:

## What is this?

This is a test project designed to incorporate distance via bluetooth sounding into sensors. For now the idea is to have two BLE boards with sensors on them that only poll and send that data when you are within range. Useful? No, not really, but maybe in the future people will want smart home dashboards that automatically rearrange themselves to show near things - or have things that usually only report occasionally give you a fresh reading when you get near. I dunno maybe if you were super crazy about this stuff you could have a greeter in your hall that says personalized things when it detects you are near it.

## How do I run this?

It's as simple as I can make it for now. nRF54L15dk overlay is provided, building as cpuapp without ns, BME280 connected to i2c22 - though note that the i2c address is not standard, I just i2c scanned to find it. Bluetooth central is just nrf connect android app for now because at this point I've only just started adding all the ble stuff slowly so my brain doesn't turn to soup.