# Introduction

This is the project for the fall detection using the Thingy:91. The Thingy:91 is a global modem that is built on a Cortex M33 device and includes a host of other devices.

# Reference

1. https://github.com/nrfconnect/sdk-nrf/tree/main/samples

# Block Diagram

# BOM

1. Thingy:91

Since the Thingy:91 has all the necessary sensors, no additional tools are required.

# Issues

1. Can't program with a jlink because the Thingy:91 provides an IO of 1.8V
2. For MCUboot you need to add a prj.conf option and flash the app_signed.hex

# Approach

## Edge Impulse

1. Create a project and test the classification system with the Thingy:91
2. Export the data to use as a standalone library - https://docs.edgeimpulse.com/docs/running-your-impulse-locally-zephyr

## nRF Connect SDK

1. Install nRF Connect for Visual Studio Code
2. Implement RTT debug
3. Send data to cloud

# Video

## Impressions

## TODO

1. Generate HTTPS certs - https://medium.com/@nitinpatel_20236/how-to-create-an-https-server-on-localhost-using-express-366435d61f28
