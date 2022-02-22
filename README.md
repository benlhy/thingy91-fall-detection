# Introduction

This is the project for the fall detection using the Thingy:91. The Thingy:91 is a global modem that is built on a Cortex M33 device and includes a host of other devices.

# Reference

1. https://github.com/nrfconnect/sdk-nrf/tree/main/samples


# Process
1. Build the project
2. Load the file into nRF Connect Programmer
3. Set the board in reset mode by holding down the Multi button and turning the device on while connected to the computer
4. Flash the device using MCUBoot
# Block Diagram

# BOM

1. Thingy:91

Since the Thingy:91 has all the necessary sensors, no additional tools are required.

# Stumbling Blocks

1. Can't program with a jlink EDU mini because the Thingy:91 provides an IO of 1.8V
2. For MCUboot you need to add a `prj.conf` option and flash the file located in `build/zephyr/app_signed.hex`

# Approach

For this project, I started by downloading the nRF Connect for Desktop that contains all the tools needed to set up my workspace. I wanted to use VSCode as my main programming tool because I really like it, and very happily, nRF Connect for Desktop was able to set up the toolchain for VSCode without any hitches.

I explored the SDK, and in the process I discovered a little feature that turned out to be incredibly helpful. The Thingy:91 uses a nRF52832 as an interface between the nRF9160 IC and the PC. By enabling the BLE option by setting the `CONFIG_BRIDGE_BLE_ENABLE` option in the `Config.txt` file located in the USB Mass Storage device, it can stream the UART output from the nRF9160 IC over BLE as well, which really opened the next stage of testing.

I loaded the ADXL362 sample and tweaked it a little to send the accelerometer data in the format and frequency that I wanted it to, and then lunched into the next part of my project.

## Edge Impulse

I collected three different sets of movement for testing:

1. Walking
2. Resting
3. Falls

For this step I attached the Thingy:91 to myself by dangling the Thingy:91 from a lanyard with a hand-tied knot through the useful lightwell on the Thingy:91. From there, I used the multifunction button to trigger a timed recording of my movement, which would be captured on the nRF Connect App on my smartphone, logged, processed and then fed into Edge Impulse's data ingestion service.

From there, it was just a matter of tweaking the parameters to get a good model of what I wanted. I think this really speaks to the ease of use of Edge Impulse, because it provides a very fuss-free way to incoroporate machine learning elements into an embedded system. While I have explored Machine Learning before and understand some of the concepts, I definitely would have taken way longer to recreate what I can do in a few clicks in Edge Impulse. The great thing about Edge Impulse is that it also isn't shy about showing you all the details of your model, and if you really want to, you can look at the IPython notebook that is generating your model for further tweaking and optimization. Luckily, I never really had to do this because the defaults are good enough, but it was very illuminating to lift the veil as it were, and check out the code from time to time to learn a little but more.

The biggest challenge I faced was that Edge Impulse expects that all your sample rates have to be exactly the same, and it wasn't clear why my project was failing to run until much later on. This expectation is also not clearly labelled when I was uploading the data.

Edge Impulse allows you to export your project to wide variety, but I stuck with the C++ library because there was more that I wanted to do with the Thingy:91. Luckily for me, Nordic and Edge Impulse both have examples of how to integrate this library into

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
