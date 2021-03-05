# Linux Installation Guide
Installation instructions for the AWS Cloud Digital Interface (CDI) SDK on Linux instances.

---

- [Install EFA driver](#install-efa-driver)
- [Install AWS CDI SDK](#install-aws-cdi-sdk)
- [Install AWS CloudWatch and AWS CLI](#install-aws-cloudwatch-and-aws-cli)
  - [Install AWS CLI](#install-aws-cli)
  - [Install Package Dependencies](#install-package-dependencies)
  - [Install CMake](#install-cmake)
  - [Download AWS SDK](#download-aws-sdk)
- [Build CDI libraries and test applications](#build-cdi-libraries-and-test-applications)
  - [Disabling the display of performance metrics to your Amazon CloudWatch account](#disabling-the-display-of-performance-metrics-to-your-amazon-cloudwatch-account)
- [Enable huge pages](#enable-huge-pages)
- [Validate the EFA environment](#validate-the-efa-environment)
- [Build the HTML documentation](#build-the-html-documentation)
- [Creating additional instances](#creating-additional-instances)

---

# Install EFA driver

For Linux installations, follow step 3 in [launch an Elasatic Fabric Adapter (EFA)-capable instance](http://docs.aws.amazon.com/AWSEC2/latest/UserGuide/efa-start.html), with the following additions to **step 3, part 1**, **step 3, part 5**, and **step 3, part 7**:

1. During **step 3, part 1**, once your instance has booted, you can find the public IP you requested earlier by clicking on the instance and looking for “IPv4 Public IP” in the pane below your instance list. Use that IP address to SSH to your new instance.
    - If you cannot connect (connection times out), you may have forgotten to add an SSH rule to your security group, or you may need to set up an internet gateway for your Virtual Private Cloud (VPC) and add a route to your subnet. You can find more information about setting up SSH access and connecting to the instance at [accessing Linux instances](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/AccessingInstancesLinux.html).
    - The default user name for Amazon Linux 2 instances is ```ec2-user```, on CentOS it’s ```centos```, and on Ubuntu, it’s ```ubuntu```.
1. During **step 3, part 5**, install the minimum version of the EFA software (the command with the ```--minimal``` option). This will not install libfabric; the AWS CDI SDK tar file has its own packaged version.
1. During **step 3, part 7**, note that the ```fi_info``` command does not work when installing the minimum version of EFA software. You will perform this check later after installing the AWS CDI SDK.

---

# Install AWS CDI SDK

1. Download AWS CDI SDK from GitHub.

   **Note**: Instructions to install git can be found at [Getting Started Installing Git](https://git-scm.com/book/en/v2/Getting-Started-Installing-Git).

   **Caution:** Do not install a new version of the AWS CDI SDK over an old one.

    ```bash
    mkdir <install_dir>
    cd <install_dir>
    git clone https://github.com/aws/aws-cdi-sdk
    ```

1. Install libfabric.

    ```bash
    git clone --single-branch --branch v1.9.x-cdi https://github.com/aws/libfabric
    ```

1. The <install_dir> now contains the following folders **libfabric** and **aws-cdi-sdk**.
    - **libfabric** is a customized version of the open-source libfabric project.
    - **aws-cdi-sdk** is the directory that contains the source code for the AWS CDI SDK and its test application. The contents of the AWS CDI SDK include a Makefile and the following directories: **doc**, **include**, **src**, and **proj**.
        - The AWS CDI SDK contains an overall Makefile that builds libfabric, the AWS CDI SDK, the test application, and the Doxygen-generated HTML documentation. The build of libfabric and the AWS CDI SDK produce shared libraries, ```libfabric.so.1``` and ```libcdisdk.so.2.0```, along with the test application, ```cdi_test```.
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
    > aws-cli/1.16.300 Python/2.7.18 Linux/4.14.173-137.228.amzn2.x86_64 botocore/1.13.36
    ```

1. If AWS CLI is not installed, perform the steps in [install AWS CLI (version 2)](https://docs.aws.amazon.com/cli/latest/userguide/install-cliv2.html).
1. Create an IAM User with CloudWatch and performance metrics permissions.
    - Navigate to the [AWS console IAM Policies](https://console.aws.amazon.com/iam/home#/policies)
        - Select **Create policy** and then select **JSON**.
        - An example policy is below:

        ```JSON
        {
            "Version": "2012-10-17",
            "Statement": [
                {
                    "Effect": "Allow",
                    "Action": "mediaconnect:*",
                    "Resource": "*"
                }
            ]
        }
        ```

    - To create an IAM User click on **Users** under **Access management**.
        - Select **Add user** and provide a name and select **Programmatic access**.
        - Select **Next: Permissions** and then select **Create group** to create a new user group.
        - Put in a **Group name** for the new group and select the policies for the group.
            - Select the policy that was made in the step above for mediaconnect access.
            - Select **CloudWatchAgentServerPolicy** to provide cloudwatch access.
            - Select **Create group**
                - Select **Next:Tags** the select **Next:Review**.
                - Select **Create user**
        - Save your **Access Key ID** and **Secret Access Key** from this IAM User creation for use in step 5.

1. Next, configure AWS CLI:

    ```bash
    aws configure
    ```

1. When prompted for the **Access Key** and **Secret Access Key**, enter these keys from the IAM role you created in step 3.
1. If successful, two files are created in the  ```~/.aws/ directory```: ```config``` and ```credentials```.
1. Verify the creation of the ```config``` and ```credentials``` file:

    ```bash
    ls ~/.aws
    ```

## Install Package Dependencies
Installation of dependent packages is required before building the AWS CDI SDK and CMake:

- CentOS 7 and Amazon Linux 2:

    ```bash
    sudo yum -y install gcc-c++ make libnl3-devel autoconf automake libtool doxygen ncurses-devel
    ```

- Ubuntu:

    ```bash
    sudo apt-get install –y build-essential libncurses-dev autoconf automake libtool libnl-3-dev cmake git doxygen libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
    ```

## Install CMake
CMake is required to build AWS SDK C++.

1. Verify that CMake version 3.2 or higher is installed. If it’s not installed, here are a few examples of how to install it through a package manager or by building from its source:

    - Ubuntu versions 16.04LTS and later have suitable packages that can be installed with:

    ```bash
    sudo apt-get install -y cmake
    ```

    - CentOS 8 use:

    ```bash
    sudo yum install -y cmake
    ```

    - The following commands represent an Amazon Linux 2 and Linux CentOS 7 installation of CMake version 3.15 by building from its source:

    ```bash
    cd
    wget https://github.com/Kitware/CMake/releases/download/v3.15.3/cmake-3.15.3.tar.gz
    tar -zxvf cmake-3.15.3.tar.gz
    cd cmake-3.15.3
    ./bootstrap --prefix=/usr/local
    make
    sudo make install
    ```

## Download AWS SDK
AWS SDK C++ will be compiled during the build process of AWS CDI SDK, so it is only necessary to download it.

**Note**: AWS CDI SDK has been tested with version 1.8.46 of AWS SDK C++.

**Note**: The AWS SDK for C++ is essential for metrics gathering functions of AWS CDI SDK to operate properly.  Although not recommended, see [these instructions](./README.md#customer-option-to-disable-the-collection-of-performance-metrics-by-the-aws-cdi-sdk) to learn how to optionally disable metrics gathering.

1. Verify that the necessary [libraries are installed for AWS SDK for C++](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/setup.html).
1. Download AWS SDK for C++ source code.
    - **Note**: This procedure replaces these instructions: ["Setting Up AWS SDK for C++"](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/setup.html).
    - Commands to clone AWS SDK for C++ from git for Amazon Linux 2 and Linux CentOS 7 are listed below:

    ```bash
    cd
    git clone -b 1.8.46 https://github.com/aws/aws-sdk-cpp.git
    ```

---

# Build CDI libraries and test applications

1. Run the Makefile in aws-cdi-sdk to build the static libraries and test application in debug mode. This step also automatically builds the necessary shared library files from AWS SDK C++ and links them to AWS CDI SDK, as well as generates the HTML documentation. You should run the debug build for ALL initial development as it will catch many problems (i.e. asserts). However, performance testing should always be done with the release build, which can be built without the DEBUG=y Make option.

    **Note**: You need to specify the location of AWS SDK C++ when building AWS CDI SDK through the value of the ```AWS_SDK``` make variable.

    The following commands build the DEBUG variety of the SDK:

    ```bash
    cd aws-cdi-sdk/
    make DEBUG=y AWS_SDK=<path to AWS SDK C++>
    ```

    **Note**: A trailing ```/``` may be required on the path given in <path to AWS SDK C++> above. For example:
    ```make DEBUG=y AWS_SDK=../aws_sdk_cpp/```

1. After a successful compile, the locations for the results are at:
    - Test application: ```cdi_test``` is placed at ```aws-cdi-sdk/build/debug/bin```
    - Minimal test applications: ```cdi_test_min_tx``` and ```cdi_test_min_rx``` are placed at ```aws-cdi-sdk/build/debug/bin```
    - AWS CDI SDK and libfabric shared libraries ```libcdisdk.so.2.0``` and ```libfabric.so.1``` are placed at ```aws-cdi-sdk/build/debug/lib.```
    - HTML documentation can be found at ```aws-cdi-sdk/build/documentation```

## Disabling the display of performance metrics to your Amazon CloudWatch account

To disable the display of performance metrics to your Amazon CloudWatch account:

- In the file ```src/cdi/configuration.h```, comment out ```#define CLOUDWATCH_METRICS_ENABLED```.

---

# Enable huge pages

Applications that use AWS CDI SDK see a performance benefit when using huge pages. To enable huge pages:

1. Edit ```/etc/sysctl.conf``` (you will likely need to use sudo with your edit command). Add the line ```vm.nr_hugepages = 1024```
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

---

# Build the HTML documentation

The normal build process will create the documentation; however it is possible to just build the documentation alone. To do this, use the following command:

```bash
make docs docs_api
```

After this completes, use a web browser to navigate to ```build/documentation/index.html```.

---

# Creating additional instances

To use AWS CDI SDK in an application, two instances are needed.
To create a new instance:

  1. Select the previously created instance in the EC2 console, and then in the **Action** menu, select **Launch More Like This**.
  1. Perform step 3 of the instructions for creating an [EFA-capable instance](http://docs.aws.amazon.com/AWSEC2/latest/UserGuide/efa-start.html). This installs and configures the EFA software.
  1. To install AWS CDI SDK software and test using the memory loopback test, repeat these instructions:
     - [Install AWS CDI SDK](#install-aws-cdi-sdk)
     - [Install AWS CloudWatch and AWS CLI](#install-aws-cloudwatch-and-aws-cli)
     - [Build CDI Libraries and Test Applications](#build-cdi-libraries-and-test-applications)
     - [Enable Huge Pages](#enable-huge-pages)
     - [Validate the EFA Environment](#validate-the-efa-environment)
  1. This instance is now considered the receiver.

---

[[Return to README](README.md)]
