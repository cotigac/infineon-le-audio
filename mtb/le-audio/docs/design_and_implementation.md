[Click here](../README.md) to view the README.

## Design and implementation

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the `Cy_SysEnableCM55()` function and the CM55 core is subsequently put to DeepSleep mode.

This code example showcases the implementation of an Isochronous (ISOC) Peripheral interface using Bluetooth&reg; LE on the PSOC&trade; Edge E84 MCU with AIROC&trade; CYW55513 Wi-Fi & Bluetooth&reg; combo chip to establish a bidirectional data exchange with a central device via an isochronous channel, enabling both transmission and reception of data.

Bluetooth&reg; Low Energy isochronous channels are introduced in the Bluetooth&reg; Core Specification 5.2. Though highly touted as the pillar for new audio applications, isochronous channels can be used to send generic data and provides some advantages over traditional LE Asynchronous Connection-Less (ACL) connections. As LE ACL connections can only support a minimum of 7.5 ms and guarantees data delivery, it puts constraints on the amount of latency that can be supported. Given that LE isochronous channels are targeted towards time critical data delivery, it provides latency advantages. Therefore, it can open up opportunities for a broader range of applications.

When the peripheral board is reset; press and release BTN1 to start advertising with the name "IFX ISOC". Similarly, press and release BTN1 on the central board to initiate scanning for a peripheral. Once the peripheral is found, the central establishes a Low Energy (LE) connection, and the USER LED2 on both boards lights up. After the LE connection is established, the central sets up an ISOC channel, and the USER LED1 on both boards lights up. Two pulse width modulation (PWM) resources are used to control the USER LED 1 and USER LED2 states.

Press BTN1 on the peripheral or central board to send a burst of data to the other device. The data is received and the USER LED1 on the receiving device toggles to indicate successful reception. The burst count is set to 6 using the `ISOC_MAX_BURST_COUNT` macro, which can be adjusted between '1' and '6'. The data sent from the peripheral is a string named "IFX ISOC CIS PERIPHERAL", and from the central, it is "IFX ISOC CIS CENTRAL". The data length is set to 100 bytes (MAX_SDU_SIZE) to provide more stress on the data communication.

Here, the initial LE connection interval is set to 24 ms to balance the risk of collisions during ISOC channel setup with the time it takes to establish the ISOC connection. If the LE connection interval is too short, collisions may occur, while a longer interval increases the setup time.

<br>