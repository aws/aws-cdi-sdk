# NDI/CDI Test Application User Guide

This is the usage guide for the example NDI/CDI Test application ```ndi_test```.

**In addition to filing CDI-SDK [bugs/issues](https://github.com/aws/aws-cdi-sdk/issues), please use the [discussion pages](https://github.com/aws/aws-cdi-sdk/discussions) for Q&A, Ideas, Show and Tell or other General topics so the whole community can benefit.**

---

- [NDI/CDI Test Application User Guide](#ndicdi-test-application-user-guide)
- [Minimal requirements](#minimal-requirements)
- [Building the test application](#building-the-test-application)
  - [Building on Linux](#building-on-linux)
  - [Building on Windows](#building-on-windows)
- [Running the Test Application](#running-the-test-application)
  - [Test application help](#test-application-help)
  - [Show NDI sources](#show-ndi-sources)
  - [Convert NDI to CDI](#convert-ndi-to-cdi)
  - [Convert CDI to NDI](#convert-cdi-to-ndi)
- [Test application notes](#test-application-notes)
- [NDI usage notes](#ndi-usage-notes)
  - [NDI-SDK](#ndi-sdk)
  - [NDI Tools (Windows)](#ndi-tools-windows)
  - [Recommended first steps using the NDI-SDK](#recommended-first-steps-using-the-ndi-sdk)
- [NDI source configuration](#ndi-source-configuration)

# Minimal requirements

For basic, minimal workings, it is only required to have one CDI/NDI-enabled EC2 instance. The instance can run NDI source generation and receiver applications from the NDI-SDK examples, run this test application and run the CDI minimal transmitter and receiver applications for basic testing and understanding purposes.

# Building the test application

## Building on Linux

Follow the steps to install and build the CDI-SDK using the [INSTALL_GUIDE_LINUX](INSTALL_GUIDE_LINUX.md), except as noted below:

Download and install the NDI-SDK in a folder at the same level as the aws-cdi-sdk. It should be called "NDI-SDK".

When building, use the ```NDI_SDK``` option on the make command line to specify the location of the NDI-SDK. This option **MUST** be used in order to build the ```ndi_test``` application. An example is shown below that builds a debug variant of the CDI-SDK, including ndi_test:

```
cd aws-cdi-sdk
make test DEBUG=y NO_MONITORING=y NDI_SDK=../../NDI-SDK
```

Note the usage of ```NO_MONITORING=y```. This removes several dependencies and disables performance metric collection by the AWS CDI SDK.

For the Linux variant, only Amazon Linux 2 has been tested.

## Building on Windows

Follow the steps to install and build the CDI-SDK using the [INSTALL_GUIDE_WINDOWS](INSTALL_GUIDE_WINDOWS.md), except as noted below:

Download and install the NDI-SDK to the default folder ```C:\Program Files\NDI\NDI 5 SDK```. If it is installed in another location, then the Visual Studio project ```ndi_test``` must be modified. Settings for ```C/C++->General->Additional Include Directories```, ```Linker->General->Additional Library Directories``` and ```Build Events->Post-Build Event``` require changes.

To build, select either the ```Debug_DLL_NDI``` or ```Release_DLL_NDI``` Visual Studio solution configuration. Note that these configurations contain the preprocessor definition ```CDI_NO_MONITORING```. This removes several dependencies and disables performance metric collection by the AWS CDI SDK.

The application binaries will be generated in the folders shown below:

**Debug_DLL_NDI configuration**
```
aws-cdi-sdk/proj/x64/Debug_DLL_NDI - ndi_test application
aws-cdi-sdk/proj/x64/Debug_DLL - Other CDI test applications
```

**Release_DLL_NDI configuration**
```
aws-cdi-sdk/proj/x64/Release_DLL_NDI - ndi_test application
aws-cdi-sdk/proj/x64/Release_DLL - Other CDI test applications
```

# Running the Test Application

The NDI Test application provides a starting point to help understand how to convert between NDI and CDI transports. Use the commands listed in this section from the ```aws-cdi-sdk``` folder.

**Note**: All of the following example test commands are Linux-centric, but the basic command syntax is the same for Windows. The only difference is that for Linux the executable is at ```build/debug|release/bin/ndi_test``` and for Windows the executable is at ```proj\x64\Debug|Release\ndi_test.exe```.

## Test application help

Running the ```--help``` command will display all of the command-line options for the application.

```bash
./build/debug/bin/ndi_test --help
```
## Show NDI sources

Example command used to show all available NDI sources:

```
./build/debug/bin/ndi_test --show_ndi_sources
```

## Convert NDI to CDI

Example command used to receive a NDI source called ```MY_MACHINE (My Test Program)```, convert to CDI and send to remote CDI host at IP address 1.2.3.4 on port 5000:

```
./build/debug/bin/ndi_test --local_ip 127.0.0.1 --ndi_rx --remote_ip 1.2.3.4 --dest_port 5000 --ndi_source_name "MY_MACHINE (My Test Program)"
```
## Convert CDI to NDI

Example command used to receive a CDI stream on port 5000 and convert to an NDI source called ```My Test Pattern```:

```
./build/debug/bin/ndi_test --local_ip 127.0.0.1 --bind_ip 127.0.0.1 --dest_port 5000 --ndi_tx --ndi_source_name "My Test Program"
```

# Test application notes

In the scenario where a static pattern is used as the source, NDI will reduce the number of frames sent down to as low as one frame per second. In order to support CDI full-frame rate output, the test application handles this by replicating the NDI source frames.

# NDI usage notes

## NDI-SDK

The NDI-SDK has a number of NDI source generation and receiver applications. A NDI source application will send video, audio, and/or metadata over the network and establish itself as a named NDI source on the network. Any number of NDI receivers can then connect to the content. A NDI receive application will find NDI sources on the network and receive video, audio, and metadata over the network.

## NDI Tools (Windows)

NDI Tools contains several useful tools and can be installed on a Windows EC2 instance. In order the use the **Studio Monitor** application the instance must contain a GPU and have the correct driver installed. For example, if using **Windows Server 2016-2019** on a **g4dn.8xlarge** instance type, the driver can be downloaded from:

```
https://s3.amazonaws.com/ec2-windows-nvidia-drivers/g4/latest/462.96_grid_win10_winserver2016_winserver2019_64bit_aws_swl.exe
```

To see a list of other available drivers, use:

```
https://s3.amazonaws.com/ec2-windows-nvidia-drivers
```

- **Studio Monitor** - View and hear NDI sources. If multiple sources are present, there is an option to select which source to view.
- **Access Manager** - Configure the IP addresses of NDI sources or an NDI discovery server. See more details in the next section.
- **Test Patterns** - Generated NDI sources using static test patterns and audio tones.

## Recommended first steps using the NDI-SDK

- Use NDI Tools Studio Monitor to view and hear NDI source content
  - Example: NDI Test Patterns seen on NDI Studio Monitor
- Run a NDI sender and a NDI receiver program on the same instance
  - Example: NDIlib_Send_10bit and NDIlib_Recv
- Run a NDI sender and a NDI receiver program on separate instances
  - Example: NDIlib_Send_10bit and NDIlib_Recv

# NDI source configuration

To add, remove, or alter any NDI configuration you must create or modify ```ndi-config.v1.json```. For Linux the file is located at  ```$HOME/.ndi/ndi-config.v1.json```. For Windows you can either edit it directly at ```C:\ProgramData\NDI\ndi-config.v1.json``` or use the NDI Tools Access Manager.

Basic configuration example is shown below. Note: You can either define the IP addresses of NDI sources or use one or more discovery servers.

```json
    {
        "ndi": {
            "machinename": "ABC",
            "networks": {
                ## Statically define NDI source IP addresses.
                "ips": "x.x.x.x,x.x.x.x", ## Use a comma to separate IPs
                ## or ##
                ## Designate one or more NDI discovery servers.
                "ips": "",
                "discovery": "x.x.x.x,x.x.x.x" # Use a comma to separate IPs
            },
            "rudp": {
                "send": {
                    "enable": true
                },
                "recv": {
                    "enable": true
                }
            },
            "multicast": {
                "send": {
                    "enable": false
                },
                "recv": {
                    "enable": false
                }
            },
            "tcp": {
                "send": {
                    "enable": false
                },
                "recv": {
                    "enable": false
                }
            },
            "unicast": {
                "send": {
                        "enable": true
                },
                "recv": {
                    "enable": true
                }
            }
        }
    }
```
