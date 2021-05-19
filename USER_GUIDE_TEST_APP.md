# Test Application User Guide

This is the usage guide for the example CDI Test applications ```cdi_test```, ```cdi_test_min_tx```, and ```cdi_test_min_rx```.

- [Test Application User Guide](#test-application-user-guide)
- [Running the minimal test applications](#running-the-minimal-test-applications)
  - [Minimal test application help](#minimal-test-application-help)
  - [EFA test](#efa-test)
- [Running the full-featured test application](#running-the-full-featured-test-application)
  - [Test application help](#test-application-help)
  - [EFA test with separate transmitter and receiver](#efa-test-with-separate-transmitter-and-receiver)
  - [EFA test with audio, video, and metadata options](#efa-test-with-audio-video-and-metadata-options)
    - [Video test with pattern](#video-test-with-pattern)
    - [Video test with input file](#video-test-with-input-file)
  - [Verify output files:](#verify-output-files)
    - [Audio test](#audio-test)
    - [Metadata](#metadata)
    - [Variable sized ancillary payloads with RIFF files](#variable-sized-ancillary-payloads-with-riff-files)
    - [Multi-stream audio/video/metadata](#multi-stream-audiovideometadata)
    - [Mux/Demux streams](#muxdemux-streams)
  - [Using file-based command-line argument insertion](#using-file-based-command-line-argument-insertion)
    - [Rules for file-based command-line insertion](#rules-for-file-based-command-line-insertion)
    - [Examples](#examples)
  - [Multiple connections](#multiple-connections)
    - [Multiple connection example](#multiple-connection-example)
  - [Connection names, logging, and display options](#connection-names-logging-and-display-options)
    - [Naming a connection](#naming-a-connection)
    - [Logging](#logging)
      - [Directing output to files](#directing-output-to-files)
      - [Directing output to stderr](#directing-output-to-stderr)
      - [Setting the log level](#setting-the-log-level)
      - [Setting log components](#setting-log-components)
      - [Putting it all together](#putting-it-all-together)
    - [Output display options](#output-display-options)
  - [Using AWS CloudWatch with CDI](#using-aws-cloudwatch-with-cdi)
    - [Using AWS CDI SDK with AWS CloudWatch](#using-aws-cdi-sdk-with-aws-cloudwatch)
    - [View results on the AWS CloudWatch dashboard](#view-results-on-the-aws-cloudwatch-dashboard)
  - [Example test content](#example-test-content)

# Running the minimal test applications

Independent transmitter ```cdi_test_min_tx``` and receiver ```cdi_test_min_rx``` test applications provide a minimal starting point to help understand how to use the SDK from an application perspective. They provide a minimal set of features, focused exclusively on the EFA adapter. Use the commands listed in this section from the aws-cdi-sdk folder.

**Note**: All of the following example test commands are Linux-centric, but the basic command syntax is the same for Windows. The only difference is that for Linux, executables are found at ```build/debug|release/bin/cdi_test_min_tx``` and ```build/debug|release/bin/cdi_test_min_rx``` and for Windows executables are found at ```proj\x64\Debug|Release\cdi_test_min_tx.exe``` and ```proj\x64\Debug|Release\cdi_test_min_rx.exe```.

## Minimal test application help

Running the ```--help``` command will display all of the command-line options for the applications.

```bash
./build/debug/bin/cdi_test_min_tx --help
./build/debug/bin/cdi_test_min_rx --help
```

Additionally, there are several build configuration options for the AWS CDI SDK library that aid in debugging and development. For details, refer to: ```aws-cdi-sdk/src/cdi/configuration.h```

## EFA test

Two EFA-capable EC2 instances are required for this test. This test runs using EFA with the receiver running on a different EC2 instance from the transmitter.

Before running the test commands, **discover the IP addresses** of the transmitter and receiver instances as follows:

1. Retrieve the receiver’s IP address by checking the AWS console or by running the following command on the receiver:

    ```bash
    ifconfig
    ```

1. The output should look like this:

    ```bash
    eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 9001
    inet 203.0.113.222  netmask 255.255.255.0  broadcast 203.0.113.255
    inet6 2001:DB8::4321:5678:AAAA  prefixlen 64  scopeid 0x20<link>
    ```

1. Run the ```ifconfig``` command on the transmitter to retrieve its IP address. For this example, assume the transmitter is 203.0.113.111.

    Now run the test applications:

1. After the EC2 instances are configured, run the following command on the receiver (replace 203.0.113.222 with your local IP address):

    ```bash
    ./build/debug/bin/cdi_test_min_rx --local_ip 203.0.113.222 --rx RAW --dest_port 2000 --num_transactions 100 --payload_size 5184000
    ```

1. The AWS CDI SDK prints registration and log messages that show it is waiting for a connection from the transmitter:

    ```bash
    [18:15:54] [main:232] Initializing test.
    ...
    [18:15:54] [main:300] Waiting to establish connection with remote target...
    ```

1. Switch to the transmitter EC2 instance and run the following command, replacing 203.0.113.111 with  your local IP and 203.0.113.222 with the IP address used above:

    ```bash
    ./build/debug/bin/cdi_test_min_tx --local_ip 203.0.113.111 --tx RAW --remote_ip 203.0.113.222 --dest_port 2000 --rate 60 --num_transactions 100 --payload_size 5184000
    ```

1. The output on the transmitter console should look like this:

    ```bash
    [18:17:00] [main:331] Initializing test.
    ...
    [18:17:00] [main:404] Connected. Sending payloads...
    ```

1. After running the transmit command, the resulting console output should indicate the number of payloads transferred.

Transmitter console output:

```bash
[18:17:02] [main:497] All done. Sent [100] payloads. Shutting down...
```

Receiver console output:

```bash
[18:17:02] [main:343] All done. Received [100] payloads. Shutting down...
```

----

# Running the full-featured test application

The test application provides a means to quickly validate that your environment is set up correctly to use the AWS CDI SDK. Use the commands listed in this section from the aws-cdi-sdk folder.

**Note**: All of the following example test commands are Linux-centric, but the basic command syntax is the same for Windows. The only difference is that for Linux, the executable is found at ```build/debug|release/bin/cdi_test```  and for Windows the executable is found at ```proj\x64\Debug|Release\cdi_test.exe```.

## Test application help

Running the ```--help``` command displays all of the command-line options for the ```cdi_test``` application.

```bash
./build/debug/bin/cdi_test --help
```

Additionally, there are several build configuration options for the AWS CDI SDK library that aid in debugging and development. For details refer to: aws-cdi-sdk/src/cdi/configuration.h

## EFA test with separate transmitter and receiver

A second EFA-capable EC2 instance is required for this test. This test runs using EFA with the receiver running on a different EC2 instance from the transmitter. It is important that the instances are in the same subnet and VPC and are using the same security group. Before the AWS CDI SDK sends data across the network using SRD, a negotiation between the two instances occurs using sockets. This is used to verify AWS CDI SDK compatibility between the two instances and to verify that the SRD connection is functional.
If two instances are not already prepared, follow the instructions for [Linux](INSTALL_GUIDE_LINUX.md#creating-additional-instances) or [Windows](INSTALL_GUIDE_WINDOWS.md#creating-additional-instances) before continuing.

For this test, the IP addresses of the transmit and receive instances must be known. Follow the instructions in [Running the Minimal Test Applications - EFA Test](#efa-test) to get the values before continuing.

1. Next, run the following command on the receiver:

    ```bash
    ./build/debug/bin/cdi_test --adapter EFA --local_ip 203.0.113.222 -X --rx RAW --dest_port 2000 --rate 60 --num_transactions 100 -S --payload_size 5184000 --pattern INC
    ```

1. Implicit options not listed:

    ```bash
    --pat_start 0
    --buffer_type SGL
    --tx_timeout 16666
    ```

1. ```cdi_test``` prints registration and log messages that show it is waiting for a connection from the transmitter:

    ```bash
    [18:56:17] [main:325] -- Running CDI Test  -- 18:56:17 02/26/2020

    [18:56:17] [RunTestGeneric:231] Registering an adapter.
    [18:56:17] [TestRxCreateThread:633] Setting up Rx connection. Protocol[RAW] Destination Port[2000] Name[Rx_0]
    [18:56:17] [RxCreateInternal:259] Creating Rx connection. Protocol[RAW] Destination Port[2000] Name[Rx_0]
    [18:56:17] [LibFabricEndpointOpen:205] Using [fe80::c3:bcff:fe5f:de8c] for local EFA device GID
    [18:56:23] [ProbeRxControlProcessProbeMode:196] Probe Rx mode[SendReset].
    ```

1. Switch to the transmitter EC2 instance and run the following command:

    ```bash
    ./build/debug/bin/cdi_test --adapter EFA --local_ip 203.0.113.111 -X --tx RAW --remote_ip 203.0.113.222 --dest_port 2000 --rate 60 --num_transactions 100 -S --payload_size 5184000
    ```

1. Implied options not listed:

    ```bash
    --pattern INC  (--pattern defaults to INC for Tx connections)
    --pat_start 0
    --buffer_type SGL
    --tx_timeout 16666
    ```

1. The output on the transmitter console should look similar to this:

    ```bash
    Running CDI Test App -- 20:26:33 02/01/2020

        [20:26:33] [RunTestGeneric:136] Registering an adapter.
    ```

1. After running the transmit command, the resulting console output should indicate the number of successful payloads.

   Transmitter console output:

   ```bash
       [22:23:53] [TestTxSendAllPayloads:556] Connection[] TX Stats:
                  Number of payloads transferred[100]
                  Number of payloads dropped    [0]
                  Number of payloads late       [0]
                  Number of payloads delayed    [0]
        [22:23:53] [main:381] ** Tests PASSED **
   ```

   Receiver console output:

   ```bash
       [22:23:53] [TestRxVerify:195] Connection[] RX Stats:
                  Number of payloads transferred[100]
                  Number of payloads dropped    [0]
                  Number of payloads late       [0]
       [22:23:53] [main:381] ** Tests PASSED **
   ```

## EFA test with audio, video, and metadata options

To test the AVM API, use the commands in this section.
To display video configuration options:

```bash
./build/debug/bin/cdi_test --help_video
```

To display audio configuration options:

```bash
./build/debug/bin/cdi_test --help_audio
```

**Note**: The ```--id``` option is required for all AVM connection streams, and must be unique for each stream within a connection.

### Video test with pattern

The following test uses the AVM protocol in the AWS CDI SDK to mimic sending video frames. This specific test sends 100 frames of 1080p60 video across two EC2 instances. The transmitter creates an incrementing data pattern and transports this data to the receiver. The receiver accumulates the data and indicate that all data was successfully received.

Receiver EC2:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
```

Transmitter EC2:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -X --tx AVM --remote_ip <rx-ipv4> --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
```

### Video test with input file

The following test uses the AVM protocol in the AWS CDI SDK to mimic sending video frames. This specific test sends 100 frames of 1080p60 video across two EC2 instances. The transmitter reads ```video_out.file```, and transports the contents to the receiver. The receiver accumulates the data and writes it to ```video_in.file```. In this example, the receiver does not check incoming data, as the ```--pattern option``` is set to NONE and the ```--file_read``` option is not used. The received data are written to ```video_in.file``` via the ```--file_write``` option, allowing users to check the file contents manually by running a SHA operation on both files, as shown below. If the receiver has access to a copy of ```video_in.file```, the ```--file_read video_in.file``` option can be used to instruct the receiver to verify the incoming data matches the contents of ```video_in.file```.

**Note**: See the [Example test content](#example-test-content) section for example files that can be used with the --file_read option in place of ```video_in.file```.

Receiver EC2:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 5184000 --pattern NONE --file_write video_in.file --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
```

Transmitter EC2:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -X --tx AVM --remote_ip <rx-ipv4> --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 5184000 --file_read video_out.file --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
```

## Verify output files:
Peform ```sha256sum``` on ```video_in.file``` and ```video_out.file``` and the two should match.

Receiver:

```bash
sha256sum video_in.file
```

Transmitter:

```bash
sha256sum video_out.file
```

### Audio test

The following test uses the AVM protocol in the AWS CDI SDK to mimic sending audio datagrams. This test sends 100 datagrams of an incrementing data pattern across two EC2 instances.
To display audio configuration options:

```bash
./build/debug/bin/cdi_test --help_audio
```

Receiver:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 6216 --pattern INC --avm_audio 51 48KHz none
```

Transmitter:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -X --tx AVM --remote_ip <rx-ipv4> --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 6216 --pattern INC --avm_audio 51 48KHz none
```

### Metadata

The AWS CDI SDK specifies metadata (ancillary) payloads as a collection of all ancillary packets for a given frame grouped together and wrapped as an IETF RFC 8331 payload.

The following test uses the AVM protocol in the AWS CDI SDK to mimic sending ancillary datagrams. This test sends 100 datagrams of an incrementing pattern across two EC2 instances:

Receiver:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 50 --pattern SHL --pat_start 1 --avm_anc
```

Transmitter:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -X --tx AVM --remote_ip <rx-ipv4> --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 50 --pattern SHL --pat_start 1 --avm_anc
```

### Variable sized ancillary payloads with RIFF files

In most real world applications there is not a fixed amount of ancillary data for every frame. The number of ancillary data packets that must be sent for each frame varies so the payload size varies. To support variable size payloads, the test application supports a simple RIFF file format in which the payload size is declared in a header for every payload. RIFF files are supported for all payload types but ancillary and audio payloads are the most common use cases.

Here is an example of a RIFF file format:

BYTE POSITION | DATA TYPE | DESCRIPTION
--------------|-----------|------------
1 to 4        | FOURCC    | "RIFF"
5 to 8        | uint32    | File size in bytes minus the size of the RIFF chunk header (8 bytes)
9 to 12       | FOURCC    | "CDI " - This is the Form Type field
13 to 16      | FOURCC    | "ANC " - Frame #1 ancillary data
17 to 21      | uint32    | Frame #1 chunk size in bytes
chunk_size number of bytes starting at 22 | BYTE[chunk_size] | Frame #1 binary section of ancillary RTP packets

Other than the limits provided by your operating system, the RIFF file has no maxiumum number of "ANC" chunks in it. Each ANC chunk is sent as a payload of chunk_size bytes. When using a RIFF file, the ```--payload_size``` option indicates the max chunk_size for buffer allocation purposes instead of the actual size of the payload that is sent.
If using looping content for testing, the RIFF file continues looping until ```--num_transactions``` are sent. As long as the RIFF file ends on a completed chunk, it loops back to the beginning of the file.

The following test sends variably sized payloads of ancillary data sourced from a RIFF file and writes back the received data back as a RIFF file. These commands create an ```ancillary_data_out.cdi``` file identical to the test content ```ancillary_data.cdi``` file:

Receiver:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 2000 --rate 60 --num_transactions 30 -S --id 1 --payload_size 1024 --riff --file_write ancillary_data_out.cdi --avm_anc
```

Transmitter:

**Note**: The use of RIFF files as *transmit sources* requires ```--buffer_type LINEAR``` to be added to the transmitter command string.

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -X --tx AVM --remote_ip <rx-ipv4> --dest_port 2000 --rate 60 --num_transactions 30 --buffer_type LINEAR -S --id 1 --payload_size 1024 --riff --file_read <Test Content Directory>/ancillary_data.cdi --avm_anc
```

### Multi-stream audio/video/metadata

With the multi-stream option, the test application can send multiple AVM data types across the same connection. The following test uses the AVM protocol in the AWS CDI SDK to mimic sending audio, video, and ancillary datagrams across one connection. Note the use of a unique stream ID (```--id```) to differentiate streams in the connection.

Receiver:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0 -S --id 2 --payload_size 6216 --pattern INC --avm_audio 51 48KHz EN -S --id 3 --payload_size 50 --pattern SHL --pat_start 1 --avm_anc
```

Transmitter:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -X --tx AVM --remote_ip <rx-ipv4> --dest_port 2000 --rate 60 --num_transactions 100 -S --id 1 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0 -S --id 2 --payload_size 6216 --pattern INC --avm_audio 51 48KHz EN -S --id 3 --payload_size 50 --pattern SHL --pat_start 1 --avm_anc
```

### Mux/Demux streams

The AWS CDI SDK supports muxing and demuxing of multiple streams using a single connection. A diagram that shows this is located at: doc/multi_endpoint_flow.jpg

With the multi-endpoint option (-XS or -new_conns), the test application can send multiple AVM data types to different endpoints. The following test uses the AVM protocol in the AWS CDI SDK to mimic sending video datagrams from a test application and audio datagrams from another test application to unique endpoints within a single connection. This results in both streams being muxed by the receiver into a single connection. Note the use of a unique stream ID (```--id```) to differentiate streams in the connection.

Receiver:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 3100 --rate 60 --num_transactions 100 -S --id 1 --payload_size 5184000 --pattern IGNORE --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0 -S --id 2 --payload_size 2944 --pattern IGNORE --avm_audio "ST" 48kHz none
```

Transmitter #1 (video datagrams):

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -XS --tx AVM --rate 60 --num_transactions 100 -S --id 1 --remote_ip <rx-ipv4> --dest_port 3100 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
```

Transmitter #2 (audio datagrams):

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -XS --tx AVM --rate 60 --num_transactions 100 -S --id 2 --remote_ip <rx-ipv4> --dest_port 3100 --payload_size 2944 --pattern INC --avm_audio "ST" 48kHz none
```

The following test uses the AVM protocol in the AWS CDI SDK to mimic sending video and audio datagrams from a single connection within a test application to unique endpoints. This results in the streams being demuxed by the transmitter and sent to different endpoints. Note the use of a unique stream ID (```--id```) to differentiate streams in the connection.

Receiver #1 (video datagrams):

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 3100 --rate 60 --num_transactions 100 -S --id 1 --payload_size 5184000 --pattern IGNORE --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
```

Receiver #2 (audio datagrams):

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --rx AVM --dest_port 3200 --rate 60 --num_transactions 100 -S --id 2 --payload_size 2944 --pattern IGNORE --avm_audio "ST" 48kHz none
```

Transmitter:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <tx-ipv4> -XS --tx AVM --rate 60 --num_transactions 100 -S --id 1 --remote_ip <rx-ipv4> --dest_port 3100 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0 -S --id 2 --remote_ip <rx-ipv4> --dest_port 3200 --payload_size 2944 --pattern INC --avm_audio "ST" 48kHz none
```


## Using file-based command-line argument insertion

In addition to parsing command line options directly, the ```cdi_test``` application can read commands from a file. To use file-based command-line arguments, use the following format in place of usual arguments:

```bash
./build/debug/bin/cdi_test @<cmd-filename>
```

### Rules for file-based command-line insertion

* The ‘#’ character is a comment delimiter. Anything after this on the line is ignored.
* Commands can be placed across multiple lines.
* In Windows, if invoking the application on Powershell, the ‘@’ character must be escaped with a backwards tick mark: ‘`’.

### Examples

An example of the contents of a file for a typical local loopback test:

```bash
# Raw loopback example
# Adapter
--adapter EFA --local_ip 127.0.0.1
# Tx Connection
-X --tx RAW --rate 60 --remote_ip 127.0.0.1 --dest_port 2000 --num_transactions 100
-S --payload_size 5184000 --pattern INC
# Rx Connection
-X --rx RAW --rate 60 --dest_port 2000 --num_transactions 100
-S --payload_size 5184000 --pattern INC
```

An example of the contents of a file with a RAW transmitter profile and multiple comments:

```bash
# Adapter
--adapter EFA                      # adapter type is EFA
--local_ip <tx-ipv4>               # local ip

# Tx Connection
-X
--tx RAW
--remote_ip <rx-ipv4>              # remote ip
--dest_port 2000                   # destination port
--num_transactions 100 --rate 60   # number of transactions and rate
# stream criteria
-S
--payload_size 5184000
```

## Multiple connections

The AWS CDI SDK is designed to allow the creation of multiple connections between EC2 instances. This is advantageous because the EC2 instance can now send data of multiple types, raw data, video, audio, and ancillary data simultaneously.

The ```--new_conn``` or ```-X``` option creates a new connection for which subsequent options are attached until a new connection is specified or until the command-line options are finished. See [Multiple Connection Example](#multiple-connection-example) for an example of how to use multiple connections for a transmitting EC2 instance and a receiving EC2 instance.

### Multiple connection example

The following example uses two EC2 instances. One is a dedicated transmitter with two connections: one sending video and the other sending audio. The other EC2 instance is a dedicated receiver with two connections: one receiving video and the other receiving audio. The transmitter and receiver have separate command files that are invoked by using the method shown in the [file-based command-line](#using-file-based-command-line-argument-insertion) section.

Transmitter command file:

```bash
# adapter
--adapter EFA --local_ip <tx-ipv4>

# connection 1: video
-X --tx AVM
--connection_name my_video_tx_1
--remote_ip <rx-ipv4>
--dest_port 2000
--rate 60000/1001 --num_transactions 100
-S --id 1
--payload_size 5184000
--pattern INC
--avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0

# connection 2: audio
-X --tx AVM
--connection_name my_audio_tx_2
--remote_ip <rx-ipv4>
--dest_port 2001
--rate 60 --num_transactions 100
-S --id 1
--payload_size 6216
--pattern INC
--avm_audio 51 48KHz EN
```

Receiver command file:

```bash
# adapter
--adapter EFA --local_ip <rx-ipv4>

# connection 1: video
-X --rx AVM
--connection_name my_video_rx_1
--dest_port 2000
--rate 60000/1001 --num_transactions 100
-S --id 1
--payload_size 5184000
--pattern INC
--avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0

# connection 2: audio
-X --rx AVM
--connection_name my_audio_rx_2
--dest_port 2001
--rate 60 --num_transactions 100
-S --id 1
--payload_size 6216
--pattern INC
--avm_audio 51 48KHz EN
```

## Connection names, logging, and display options

### Naming a connection

By default, the ```cdi_test``` application labels connection names with numbers representing the order in which connections were created on the command line. Use the option ```--connection_name``` (or ```-name```) to name connections using more meaningful names.

For example:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> -X --connection_name big_xfer --rx RAW --dest_port 2000 --rate 60 --num_transactions 100 -S --payload_size 5184000 --pattern INC -X --connection_name small_xfer --rx RAW --dest_port 2001 --rate 60 --num_transactions 100 -S --payload_size 1000 --pattern INC
```

### Logging

#### Directing output to files

The ```cdi_test``` application outputs to the console by default, but users can direct output to user-defined log files if desired. There are two options for directing log file output, ```--log <file path/name>``` and ```--logs <file path/name>```. The former directs log output to a pair of log files for test application output and SDK output, respectively. The latter option creates the same two files as the ```--log``` option, but any connection specific messages are output to a file that is unique to that connection.

For example, ```--log my.log``` would create a file called ```my.log``` for ```cdi_test``` application output, and a file called ```SDK_my.log``` for SDK output.

The ```--logs my.log``` command would create the files listed above, but neither would contain any connection-specific messages. Instead, connection-specific messages would be output to ```my.log_0.log```, ```my.log_1.log```, etc., for ```cdi_test``` application messages and to ```SDK_my.log_0.log``` and ```SDK_my.log_1.log```, etc., for SDK messages.

When logging options are used in conjunction with the ```--connection_name``` option described above, the connection numbers are replaced with the names provided by the user.

For example:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> --logs my.log -X --connection_name big_xfer --rx RAW --dest_port 2000 --rate 60 --num_transactions 100 -S --payload_size 5184000 --pattern INC -X --connection_name small_xfer --rx RAW --dest_port 2001 --rate 60 --num_transactions 100 -S --payload_size 1000 --pattern INC
```

This command produces several files, which are shown below:

* ```my.log``` – ```cdi_test``` messages that are not associated with any particular connection
* ```my.log_big_xfer.log``` – ```cdi_test``` messages that are associated with connection “big_xfer”
* ```my.log_small_xfer.log``` – ```cdi_test``` messages that are associated with connection “small_xfer”
* ```SDK_my.log``` – SDK messages that are not associated with any particular connection
* ```SDK_my.log_big_xfer.log``` – SDK messages that are associated with connection “big_xfer”
* ```SDK_my.log_small_xfer.log``` – SDK messages that are associated with connection “small_xfer”

#### Directing output to stderr

In addition to sending messages to user log files, messages can be directed to stderr using the ```--stderr``` option.

#### Setting the log level

The logging level can be set with the ```--log_level``` option. By default the DEBUG log level is enabled, which is the most verbose. See the ```cdi_test``` usage message for all log levels.

#### Setting log components

Some log messages are disabled by default to keep log messages from overwhelming the SDK output. However, there are several groups of log messages, called “components,” that can be enabled using the ```--log_component``` command line option. Multiple components can be enabled by putting them in quotes, separated by spaces. See the ```cdi_test``` usage message for all log components.

#### Putting it all together

Here is an example of a command that sets the output to both a log file as well as ```stderr```, and also sets the logging level to ```VERBOSE``` while enabling the ```PROBE```, ```ENDPOINT_MANAGER```, and ```PERFORMANCE_METRICS``` log components:

```bash
--log log_tx.log --log_component "PROBE ENDPOINT_MANAGER PERFORMANCE_METRICS"
--log_level DEBUG --stderr
```

### Output display options

By default, the ```cdi_test``` application outputs to the console. This can be cumbersome to watch when users are interested in tracking then number of payloads sent as well as other statistics about the test run (such as payload latencies, number of errors, etc). To simplify this kind of monitoring, the ```cdi_test``` application includes an option called ```--multiwindow```, which uses the curses library to provide a stable display pane that shows statistics and a scrolling pane that shows other ```cdi_test``` console output.

Here is an example of the output at the top of the console when using the ```--multiwindow``` option:

```bash
| Elapsed Time: 00:00:40  |                         Payload Latency (us)                  |      | Connection | Control |
|      Payload Counts     |    Overall    |                 Most Recent Series            |      |            | Command |
| Success | Errors | Late |  Min  |  Max  |  Min  |  P50  |  P90  |  P99  |  Max  | Count | CPU% | Drop Count | Retries |
|     500 |      0 |    0 |  1186 |  1532 |  1195 |  1230 |  1249 |  1280 |  1280 |    27 |    8 |       0    |     0   |
-------------------------------------------------------------------------------------------------------------------------
```

Below that, regular console messages scroll without interrupting the statistics panel at the top.

## Using AWS CloudWatch with CDI

The AWS CloudWatch feature is available to use through the AWS CDI SDK. AWS CloudWatch allows you to monitor the health of the application through specific metrics such as CPU Utilization, Network Traffic In/Out, etc. For a high-level overview of AWS CloudWatch, visit [Amazon CloudWatch: Observability of your AWS resources and applications on AWS and on-premises](https://aws.amazon.com/cloudwatch/) and the [AWS CloudWatch Users Guide](https://docs.aws.amazon.com/AmazonCloudWatch/latest/monitoring/WhatIsCloudWatch.html).

AWS CloudWatch is a service that has both free and paid tiers. For more information on pricing, visit the [AWS CloudWatch pricing page](https://aws.amazon.com/cloudwatch/pricing/).

For the AWS CDI SDK, the statistics generated by the AWS CDI SDK can be published to AWS CloudWatch. To use this functionality, the AWS CLI and AWS SDK C++ must be downloaded and installed using these instructions for [Linux](./INSTALL_GUIDE_LINUX.md#installing-cloudwatch-features) or [Windows](./INSTALL_GUIDE_WINDOWS.md#build-cdi-with-aws-cloudwatch-enabled) before continuing.

### Using AWS CDI SDK with AWS CloudWatch

With the AWS CloudWatch libraries installed, the AWS CDI SDK can publish metrics to AWS CloudWatch. There are dedicated AWS CloudWatch options exposed to the user in the AWS CDI SDK: namespace, dimension domain, and region. Visit [Amazon CloudWatch Concepts](https://docs.aws.amazon.com/AmazonCloudWatch/latest/monitoring/cloudwatch_concepts.html) for more information concerning metrics terminology.

**Note**: Instances that publish metrics data to CloudWatch must have an IAM role attached that allows them to publish CloudWatch data.

* The namespace controls the naming of the overall metrics container. This value defaults to ```CloudDigitalInterface``` if AWS CloudWatch is enabled and the namespace is unspecified.
* The region specifies the location to send the metrics, eg. ```us-west-2```. If this value is unspecified, it defaults to the region in which the SDK is running.
* The dimension domain provides a unique name/value pair for the metric. The domain defaults to the connection name.

Run the following option in the ```cdi_test``` application to view AWS CloudWatch usage and statistics:

```bash
./build/debug/bin/cdi_test --help_stats
```

Here is an example of using AWS CDI SDK command-line arguments to publish AWS CloudWatch metrics:

```bash
./build/debug/bin/cdi_test --adapter EFA --local_ip <rx-ipv4> --stats_cloudwatch CDIStats us-west-2 Stream1 -X --connection_name small_xfer --rx RAW --dest_port 2000 --rate 60 --num_transactions 100 -S --payload_size 1000 --pattern INC --stats_period 1
```

### View results on the AWS CloudWatch dashboard

1. To view the resulting AWS CDI SDK metrics on the AWS CloudWatch dashboard, log on to the [AWS console](https://aws.amazon.com/console/).
1. From the AWS Management Console, locate  **Management & Governance** and then select **CloudWatch**.
1. From the primary AWS CloudWatch dashboard, select **Metrics** in the navigation pane.
1. If the AWS CDI SDK successfully posted data to AWS CloudWatch, locate the Namespace that was given to AWS CDI SDK under **Custom Namespaces**.
1. To interact with the graphing feature, see the [AWS CloudWatch User Guide](https://docs.aws.amazon.com/AmazonCloudWatch/latest/monitoring/WhatIsCloudWatch.html).

## Example test content

Test files for use with the cdi_test application's --file_read option may be downloaded from [cdi.elemental.com](https://cdi.elemental.com/test_content)

----


[[Return to README](README.md)]
