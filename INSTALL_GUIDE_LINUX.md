# Linux Installation Guide
Installation instructions for the AWS Cloud Digital Interface (CDI) SDK on Linux instances.

**In addition to filing AWS CDI SDK [bugs/issues](https://github.com/aws/aws-cdi-sdk/issues), please use the [discussion pages](https://github.com/aws/aws-cdi-sdk/discussions) for Q&A, Ideas, Show and Tell or other General topics so the whole community can benefit.**

---

- [Linux Installation Guide](#linux-installation-guide)
- [Upgrading from previous releases](#upgrading-from-previous-releases)
- [Create an EFA enabled instance](#create-an-efa-enabled-instance)
- [Install EFA driver](#install-efa-driver)
- [Install Package Dependencies](#install-package-dependencies)
- [Install AWS CDI SDK](#install-aws-cdi-sdk)
- [Install AWS CloudWatch and AWS CLI](#install-aws-cloudwatch-and-aws-cli)
  - [Install AWS CLI](#install-aws-cli)
  - [Download AWS SDK](#download-aws-sdk)
- [Build CDI libraries and test applications](#build-cdi-libraries-and-test-applications)
  - [(Optional) Disable the display of performance metrics to your Amazon CloudWatch account](#optional-disable-the-display-of-performance-metrics-to-your-amazon-cloudwatch-account)
- [Enable huge pages](#enable-huge-pages)
- [Validate the EFA environment](#validate-the-efa-environment)
- [Build the HTML documentation](#build-the-html-documentation)
- [Creating additional instances](#creating-additional-instances)
- [Pinning AWS CDI SDK Poll Threads to Specific CPU Cores](#pinning-aws-cdi-sdk-poll-threads-to-specific-cpu-cores)
  - [Additional Notes/Commands when using cset](#additional-notescommands-when-using-cset)
    - [Display current cpusets](#display-current-cpusets)
    - [Disable Thread Pinning (stop the shield)](#disable-thread-pinning-stop-the-shield)
  - [Thread pinning applications running within Docker Containers](#thread-pinning-applications-running-within-docker-containers)
    - [Launching Docker Containers](#launching-docker-containers)

---
# Upgrading from previous releases

**Upgrading from CDI SDK 2.4 or earlier**

* Must [install](#install-efa-driver) the latest EFA driver and **REBOOT** the system.
* Must download and install a second version of **libfabric**, which requires **rdma-core-devel**. See steps in the libfabric section of [Install the AWS CDI SDK](#install-the-aws-cdi-sdk).

---

# Create an EFA enabled instance

Follow the steps in [create an EFA-enabled instance](README.md#create-an-efa-enabled-instance).

# Install EFA driver

For Linux installations, follow step 3 in [launch an Elastic Fabric Adapter (EFA)-capable instance](http://docs.aws.amazon.com/AWSEC2/latest/UserGuide/efa-start.html), with the following additions to the step **Install the EFA software**:

 - During **Connect to the instance you launched**, once your instance has booted, you can find the public IP you requested earlier by clicking on the instance and looking for “IPv4 Public IP” in the pane below your instance list. Use that IP address to SSH to your new instance.
    - If you cannot connect (connection times out), you may have forgotten to add an SSH rule to your security group, or you may need to set up an internet gateway for your Virtual Private Cloud (VPC) and add a route to your subnet. You can find more information about setting up SSH access and connecting to the instance at [accessing Linux instances](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/AccessingInstancesLinux.html).
    - The default user name for Amazon Linux 2 instances is ```ec2-user```, on CentOS it’s ```centos```, and on Ubuntu, it’s ```ubuntu```.
- During **Install the EFA software.**, install the minimum version of the EFA software using the command shown below. This will not install libfabric; the AWS CDI SDK uses its own versions.

    ```sudo ./efa_installer.sh -y --minimal```

1. During **Confirm that the EFA software components were successfully installed**, note that the ```fi_info``` command does not work when installing the minimum version of EFA software. You will perform this check later after installing the AWS CDI SDK.

---

# Install Package Dependencies
Installation of dependent packages is required before building the AWS CDI SDK:

- CentOS 7 and Amazon Linux 2:

    ```bash
    sudo yum update -y
    sudo yum -y install gcc-c++ make cmake3 curl-devel openssl-devel autoconf automake libtool doxygen ncurses-devel unzip git
    ```

- Rocky Linux 8:

  ```bash
  sudo dnf update -y
  sudo dnf config-manager --set-enabled powertools
  sudo dnf -y install gcc-c++ make cmake3 curl-devel openssl-devel autoconf automake libtool doxygen ncurses-devel unzip git
  ```

- Ubuntu:

    ```bash
    sudo apt update
    sudo apt-get upgrade -y
    sudo apt-get -y install build-essential libncurses-dev autoconf automake libtool cmake doxygen libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev unzip git
    ```

# Install AWS CDI SDK

1. Download AWS CDI SDK from GitHub.

   **Note**: Instructions to install git can be found at [Getting Started Installing Git](https://git-scm.com/book/en/v2/Getting-Started-Installing-Git).

   **Caution:** Do not install a new version of the AWS CDI SDK over an old one.

    ```bash
    mkdir <install_dir>
    cd <install_dir>
    git clone https://github.com/aws/aws-cdi-sdk
    ```

1. Install libfabric versions. The folder ```libfabric``` is used for libfabric v1.9, which is required to support AWS CDI SDK versions prior to 3.x.x. The folder ```libfabric_new``` is used for libfabric versions after v1.9, which is required to support AWS CDI SDK versions 3.x.x.

    ```bash
    git clone --single-branch --branch v1.9.x-cdi https://github.com/aws/libfabric libfabric
    git clone --single-branch --branch v1.15.2 https://github.com/ofiwg/libfabric libfabric_new
    ```

    libfabric_new also requires the development version of rdma-core v27 or later. You can either install the version for your OS using:
    ```
    sudo yum install rdma-core-devel
    ```

    Or using source to build the latest version. After built, the library files **libefa-rdmv??.so**, **libefa.so** and **libibverbs.so** will need to be copied from **<install path>/rdma-core/build/lib** to the same folder where the AWS CDI SDK library is generated.     To build rdma-core, see the [REAMDME file](https://github.com/linux-rdma/rdma-core/blob/master/README.md). Source code can be obtained using:
    ```
    git clone https://github.com/linux-rdma/rdma-core
    ```

The **<install_dir>** should now contain the folder hierarchy as shown below:

```
  <install_dir>/aws-cdi-sdk
  <install_dir>/libfabric
  <install_dir>/libfabric_new
```
- **libfabric** is a customized version of the open-source libfabric project based on libfabric v1.9.x.
- **libfabric_new** is a mainline version of the open-source libfabric project.
- **aws-cdi-sdk** is the directory that contains the source code for the AWS CDI SDK and its test application. The contents of the AWS CDI SDK include the following directories: **doc**, **include**, **src**, and **proj**.
  - The root folder contains an overall Makefile that builds libfabric, the AWS CDI SDK, the test applications, and the Doxygen-generated HTML documentation. The build of libfabric and the AWS CDI SDK produce shared libraries, ```libfabric.so.x``` and ```libcdisdk.so.x.x```, along with the test applications: ```cdi_test```, ```cdi_test_min_rx```, ```cdi_test_min_tx```, and ```cdi_test_unit```.
    - The **doc** folder contains Doxygen source files used to generate the AWS CDI SDK HTML documentation.
        - The documentation builds to this path: aws-cdi-sdk/build/documentation
    - The **include** directory exposes the API to the AWS CDI SDK in C header files.
    - The **src** directory contains the source code for the implementation of the AWS CDI SDK.
        - AWS CDI SDK: ```aws-cdi-sdk/src/cdi```
        - Common utilities: ```aws-cdi-sdk/src/common```
        - Test application: ```aws-cdi-sdk/src/test```
        - Minimal test applications: ```aws-cdi-sdk/src/test_minimal```
        - Common test application utilities: ```aws-cdi-sdk/src/test_common```
    - The **proj** directory contains the Microsoft Visual Studio project solution for Windows development.
    - The **build** directory is generated after a make of the project is performed. The **build** folder contains the generated libraries listed above along with the generated HTML documentation.

---

# Install AWS CloudWatch and AWS CLI

AWS CloudWatch is required to build the AWS CDI SDK, and is provided in [AWS SDK C++](https://aws.amazon.com/sdk-for-cpp/).

## Install AWS CLI
AWS CLI is required to setup configuration files for AWS CloudWatch.

1. Run the following command to determine if AWS CLI is installed:

    ```bash
    aws --version
    ```
    If AWS CLI is installed, you will see a response that looks something like this:

    ```bash
    aws-cli/2.8.9 Python/3.9.11 Linux/5.15.0-1019-aws exe/x86_64.ubuntu.22 prompt/off
    ```

1. If AWS CLI is not installed, perform the steps in [install AWS CLI (version 2)](https://docs.aws.amazon.com/cli/latest/userguide/install-cliv2.html).
1. Create an IAM User with CloudWatch and performance metrics permissions.
    - Navigate to the [AWS console IAM Policies](https://console.aws.amazon.com/iam/home#/policies)
        - Select **Create policy** and then select **JSON**.
        - The minimum security IAM policy is below:

        ```JSON
        {
            "Version": "2012-10-17",
            "Statement": [
                {
                    "Effect": "Allow",
                    "Action": [
                        "cloudwatch:PutMetricData",
                        "mediaconnect:PutMetricGroups"
                    ],
                    "Resource": "*"
                }
            ]
        }
        ```

    - To create an IAM User click on **Users** under **Access management**.
        - Select **Add user** and provide a name and select **Programmatic access**.
        - Select **Next: Permissions** and then select **Create group** to create a new user group.
        - Put in a **Group name** for the new group and select the policies for the group.
            - Select the policy that was made in the step above for CloudWatch access.
            - In order for the AWS CDI SDK to be able to connect to the performance metrics service, you must also add ```mediaconnect:PutMetricGroups``` permission as per the example policy above. Note: This may result in an IAM warning such as: ```IAM does not recognize one or more actions. The action name might include a typo or might be part of a previewed or custom service```, which can be safely ignored.
            - Select **Create group**
                - Select **Next:Tags** the select **Next:Review**.
                - Select **Create user**
        - Save your **Access Key ID** and **Secret Access Key** from this IAM User creation for use in step 5.

1. Next, configure AWS CLI:

    ```bash
    aws configure
    ```

1. When prompted for the **Access Key** and **Secret Access Key**, enter these keys from the IAM role you created in step 3.
1. If successful, two files are created in the  ```~/.aws/``` directory: ```config``` and ```credentials```. Verify they exist by using:

    ```bash
    ls ~/.aws
    ```

## Download AWS SDK
AWS SDK C++ will be compiled during the build process of AWS CDI SDK, so it is only necessary to download it.

**Note**: The AWS SDK for C++ is essential for metrics gathering functions of AWS CDI SDK to operate properly.  Although not recommended, see [these instructions](./README.md#customer-option-to-disable-the-collection-of-performance-metrics-by-the-aws-cdi-sdk) to learn how to optionally disable metrics gathering.

1. Verify that the necessary [requirements are met and libraries installed for AWS SDK for C++](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/setup-linux.html).
1. Download AWS SDK for C++ source code.
    - **Note**: This procedure replaces these instructions: ["Setting Up AWS SDK for C++"](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/setup.html).
    - Commands to clone AWS SDK for C++ from git for Amazon Linux 2 and Linux CentOS 7 are listed below:

       ```bash
       cd <install_dir>
       git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp
       ```

  The **<install_dir>** should now contain the folder hierarchy as shown below:

   ```
   <install_dir>/aws-cdi-sdk
   <install_dir>/aws-sdk-cpp
   <install_dir>/libfabric
   <install_dir>/libfabric_new
   ```

---

# Build CDI libraries and test applications

Run the Makefile in aws-cdi-sdk to build the static libraries and test application in debug mode. This step also automatically builds the necessary shared library files from AWS SDK C++ and links them to AWS CDI SDK, as well as generates the HTML documentation. You should run the debug build for ALL initial development as it will catch many problems (i.e. asserts). However, performance testing should always be done with the release build, which can be built without the DEBUG=y Make option.

**Note**: You need to specify the location of AWS SDK C++ when building AWS CDI SDK through the value of the ```AWS_SDK``` make variable.

The following commands build the DEBUG variety of the SDK:

```bash
cd aws-cdi-sdk/
make DEBUG=y AWS_SDK=<path to AWS SDK C++> RDMA_CORE_PATH=<path to rdma-core/build>
```

**Note**: A trailing ```/``` may be required on the path given in <path to AWS SDK C++> above. For example:

```bash
cd aws-cdi-sdk/
make DEBUG=y AWS_SDK=../aws-sdk-cpp/
```

**Note**: Pipe the StdOut/Err to a log file for future review/debug:

```bash
cd aws-cdi-sdk/
make DEBUG=y AWS_SDK=../aws-sdk-cpp/ 2>&1 | tee build.log
```

**Note**: RDMA_CORE_PATH does not have to specified if you installed the OS package version of rdma-core-devel.

**Note**: If you experience **library not found errors** during linking, you may have to change the **rpath** in the **Makefile** from using **$$ORIGIN** to using an absolute path that points to the AWS CDI SDK lib folder (ie. ```<install path>/build/debug/lib```).

After a successful compile, the locations for the results are at:
- Test application: ```cdi_test``` is placed at ```aws-cdi-sdk/build/debug/bin```
- Minimal test applications: ```cdi_test_min_tx``` and ```cdi_test_min_rx``` are placed at ```aws-cdi-sdk/build/debug/bin```
- AWS CDI SDK, libcdi_libfabric_api and libfabric shared libraries ```libcdisdk.so.x.x```, ```libfabric.so.x``` and ```libfabric_new.so.x``` are placed at ```aws-cdi-sdk/build/debug/lib.```
- HTML documentation can be found at ```aws-cdi-sdk/build/documentation```

## (Optional) Disable the display of performance metrics to your Amazon CloudWatch account

To disable the display of performance metrics to your Amazon CloudWatch account:

- In the file ```src/cdi/configuration.h```, comment out ```#define CLOUDWATCH_METRICS_ENABLED```.

**Note**: For the change to take effect, the CDI SDK library and related applications must be rebuilt.

---

# Enable huge pages

Applications that use AWS CDI SDK see a performance benefit when using huge pages. To enable huge pages:

1. Edit ```/etc/sysctl.conf``` (you will likely need to use sudo with your edit command). Add the line ```vm.nr_hugepages = 1024```
**Note**: If using more than 6 connections, you may have to use a larger value such as 2048.
1. Issue the command ```sudo sysctl -p```
1. Check that huge pages have updated by issuing the command ```cat /proc/meminfo | grep Huge```.

---

# Validate the EFA environment

This section helps you to verify that the EFA interface is operational. **Note**: This assumes that you followed the EFA installation guide, and that both the aws-efa-installer and the CDI version of libfabric are in the following locations:

- ```$HOME/aws-efa-installer```
- ```path/to/dir/libfabric```, where ```path/to/dir``` is the location where you have installed the libfabric directory.

Run the following commands to verify that the EFA interface is operational, replacing path/to/dir with your actual path:

```bash
PATH=path/to/dir/libfabric/build/debug/util:$PATH
fi_info -p efa -t FI_EP_RDM
```

This command should return information about the Libfabric EFA interface. The following example shows the command output:

```bash
provider: efa
    fabric: EFA-fe80::4dd:7eff:fe99:4620
    domain: rdmap0s6-rdm
    version: 2.0
    type: FI_EP_RDM
    protocol: FI_PROTO_EFA
```

If successful, proceed with the next command:

```bash
cd $HOME/aws-efa-installer
./efa_test.sh
```

If the EFA is working properly, the following output displays in the console:

```bash
Starting server...
Starting client...
bytes   #sent   #ack     total       time     MB/sec    usec/xfer   Mxfers/sec
64      10      =10      1.2k        0.03s      0.05    1362.35       0.00
256     10      =10      5k          0.00s     12.86      19.90       0.05
1k      10      =10      20k         0.00s     58.85      17.40       0.06
4k      10      =10      80k         0.00s    217.29      18.85       0.05
64k     10      =10      1.2m        0.00s    717.02      91.40       0.01
1m      10      =10      20m         0.01s   2359.00     444.50       0.00
```

If you get an error, please review these [Troubleshooting](README.md#troubleshooting) steps and check this issue: https://github.com/aws/aws-cdi-sdk/issues/48

**Note**: Although not required, the same test can be performed using the libfabric version located at ```libfabric_new```.

---

# Build the HTML documentation

The normal build process will create the documentation; however it is possible to just build the documentation alone. To do this, use the following command:

```bash
make docs docs_api
```

After this completes, use a web browser to navigate to ```build/documentation/index.html```.

---

# Creating additional instances

To create a new instance, create an Amazon Machine Image (AMI) of the existing instance and use it to launch additional instances as described below:

  1. To create an AMI, select the previously created instance in the EC2 console, and then in the **Action** menu, select **Image and Templates -> Create Image**. For details see [Create AMI](https://docs.aws.amazon.com/toolkit-for-visual-studio/latest/user-guide/tkv-create-ami-from-instance.html).
  1. In the EC2 console Under **Images**, select **AMIs**. Locate the AMI and wait until the **Status** shows **available**. It will may several minutes for the AMI to be created and become available for use. After it becomes available, select the AMI and use the **Action** menu to select **Launch**.
  1. Select the same instance type as the existing instance and select **Next: Configure Instance Details**.
  1. To **Configure Instance Details**, **Add Storage**, **Add Tags** and **Configure Security Group** follow the steps at [Create an EFA-enabled instance](#create-an-efa-enabled-instance).
  1. If access to this instance from outside the Amazon network is needed, enable **Auto-assign public IP.**
  1. Make sure to enable EFA by checking the **Elastic Fabric Adapter** checkbox here. **Note**: To enable the checkbox, you must select the subnet even if using the default subnet value.
  1. Amazon recommends putting EFA-enabled instances using AWS CDI SDK in a placement group, so select or create one under **Placement Group – Add instance to placement group.**  The **Placement Group Strategy** should be set to **cluster**.
  1. The new instance will contain the host name stored in the AMI and not the private IP name used by the new instance. Edit **/etc/hostname**. The private IP name can be obtained from the AWS Console or use **ifconfig** on the instance. For example, if **ifconfig** shows **1.2.3.4** then the name should look something like (region may be different):

     ```
     ip-1-2-3-4.us-west-2.compute.internal
     ```
     This change requires a reboot. Depending on OS variant, there are commands that can be used to avoid a reboot. For example:

     ```
     sudo hostname <new-name>
     ```

---

# Pinning AWS CDI SDK Poll Threads to Specific CPU Cores

On Linux, the transmit and receive poll threads should be pinned to specific CPU cores in order to prevent thread starvation resulting in poor packet transmission performance and other problems.

The CPU used for the poll thread is defined when creating a new CDI connection through the AWS CDI SDK API using a
configuration setting called **thread_core_num**. For transmit connections the value is in the **CdiTxConfigData**
structure when using the **CdiAvmTxCreate()**, **CdiRawTxCreate()** or **CdiAvmTxStreamEndpointCreate()** APIs. For
receive connections the value is in the **CdiRxConfigData** structure when using the **CdiAvmRxCreate()** or
**CdiRawRxCreate()** APIs.

In addition to defining which CPU core to use, the CDI enabled application must be launched using **cset**. If
**cset** is not already installed, the steps shown below are for Amazon Linux 2, but should be similar for other
distributions.

**Note**: **cset** cannot be used with Docker. See the next section for information on thread pinning with Docker.

1. Obtain the cpuset package and install it. NOTE: Can be built from source at:
   https://github.com/lpechacek/cpuset/archive/refs/tags/v1.6.tar.gz

1. Then run these steps:

     ```
    sudo yum install -y cpuset-1.6-1.noarch.rpm
    sudo yum install -y python-pip
    sudo pip install future configparser
     ```

Make sure the command shown below has been run first. This command will move kernel threads, so the CDI enabled
application can use a specific set of CPU cores. In the example shown below, the CDI-enabled application will use CPU
cores 1-24.

```
sudo cset shield -k on -c 1-24
```

On a system with 72 CPU cores (for example), output should look something like this:

```
cset: --> activating shielding:
cset: moving 1386 tasks from root into system cpuset...
[==================================================]%
cset: kthread shield activated, moving 45 tasks into system cpuset...
[==================================================]%
cset: **> 35 tasks are not movable, impossible to move
cset: "system" cpuset of CPUSPEC(0,25-71) with 1396 tasks running
cset: "user" cpuset of CPUSPEC(1-24) with 0 tasks running
```

To run the CDI enabled application, launch it using cset. An example command line is shown below:


```
sudo cset shield -e <application>
```

**NOTE**: The use of **sudo** requires root privileges and may not be desired for the application.

## Additional Notes/Commands when using cset

### Display current cpusets

To list cpusets, use this command:

```
cset set -l
```

**NOTE**: If docker shows up in the list you MUST remove it, otherwise trying to use any of the shield commands will fail.
Use this command to remove it:

```
sudo cset set --destroy docker
```

### Disable Thread Pinning (stop the shield)

This is required in order to use docker:

```
sudo cset shield --reset
```

## Thread pinning applications running within Docker Containers

Below are some general tips and results of experimentation when using thread-pinning in CDI enabled applications
within Docker containers.

* Don't use hyper-threading. Intermittent problems will occur when hyper-threading is enabled. The first half of
  the available cores are the real cores, and the second half are the hyper-threading cores. Only use the first half.
* Tried using **isolcpus** to do thread pinning at the kernel level, but were unsuccessful.
* Tried using **pthread_set_affinity** to pin all other threads away from the CDI cores, but caused application instability.
* Tried pinning CDI threads to a unique core, but this resulted in kernel lockups and CDI crashes.
* Tried multiple custom AffinityManager experiments, one was utilizing the file-based method of pinning non CDI threads.
   All efforts were unsuccessful.
* Any time we tried pinning CDI threads to less than three cores we ran into issues, so three is the minimum.

**What worked:** Once the AWS CDI SDK poll-threads were pinned to specific cores, we pinned all other application threads
away from those cores using the AWS CDI SDK API **CdiOsThreadCreatePinned()**.

### Launching Docker Containers

When launching a Docker container, we typically use command lines such as the one shown below:

```
docker run --rm --shm-size=8g --security-opt seccomp=unconfined --cap-add net_raw --cap-add NET_ADMIN --tty --name [my_container] --tmpfs=/var/your_generic_tmp --ulimit rtprio=100 --ulimit core=-1 --cpu-rt-runtime=30645 --cap-add SYS_NICE
```

**NOTE**: The docker command shown above was run with docker version 20.10.4.

If you have additional findings, please start a [Show and Tell Discussion](https://github.com/aws/aws-cdi-sdk/discussions/categories/show-and-tell) so others may also benefit.

[[Return to README](README.md)]
