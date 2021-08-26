# Linux Installation Guide
Installation instructions for the AWS Cloud Digital Interface (CDI) SDK on Linux instances.

---

- [Linux Installation Guide](#linux-installation-guide)
- [Create an EFA enabled instance](#create-an-efa-enabled-instance)
- [Install EFA driver](#install-efa-driver)
- [Install AWS CDI SDK](#install-aws-cdi-sdk)
- [Install AWS CloudWatch and AWS CLI](#install-aws-cloudwatch-and-aws-cli)
  - [Install AWS CLI](#install-aws-cli)
  - [Install Package Dependencies](#install-package-dependencies)
  - [Download AWS SDK](#download-aws-sdk)
- [Build CDI libraries and test applications](#build-cdi-libraries-and-test-applications)
  - [(Optional) Disable the display of performance metrics to your Amazon CloudWatch account](#optional-disable-the-display-of-performance-metrics-to-your-amazon-cloudwatch-account)
- [Enable huge pages](#enable-huge-pages)
- [Validate the EFA environment](#validate-the-efa-environment)
- [Build the HTML documentation](#build-the-html-documentation)
- [Creating additional instances](#creating-additional-instances)

---

# Create an EFA enabled instance

Follow the steps in [create an EFA-enabled instance](README.md#create-an-efa-enabled-instance).

# Install EFA driver

For Linux installations, follow step 3 in [launch an Elastic Fabric Adapter (EFA)-capable instance](http://docs.aws.amazon.com/AWSEC2/latest/UserGuide/efa-start.html), with the following additions to the step **Install the EFA software**:

 - During **Connect to the instance you launched**, once your instance has booted, you can find the public IP you requested earlier by clicking on the instance and looking for “IPv4 Public IP” in the pane below your instance list. Use that IP address to SSH to your new instance.
    - If you cannot connect (connection times out), you may have forgotten to add an SSH rule to your security group, or you may need to set up an internet gateway for your Virtual Private Cloud (VPC) and add a route to your subnet. You can find more information about setting up SSH access and connecting to the instance at [accessing Linux instances](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/AccessingInstancesLinux.html).
    - The default user name for Amazon Linux 2 instances is ```ec2-user```, on CentOS it’s ```centos```, and on Ubuntu, it’s ```ubuntu```.
- During **Install the EFA software.**, install the minimum version of the EFA software using the command shown below. This will not install libfabric; the AWS CDI SDK tar file has its own packaged version.

    ```sudo ./efa_installer.sh -y --minimal```

1. During **Confirm that the EFA software components were successfully installed**, note that the ```fi_info``` command does not work when installing the minimum version of EFA software. You will perform this check later after installing the AWS CDI SDK.

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

The **<install_dir>** should now contain the folder hierarchy as shown below:

```
  <install_dir>/aws-cdi-sdk
  <install_dir>/libfabric
```
- **libfabric** is a customized version of the open-source libfabric project.
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
                    "Action": "cloudwatch:*",
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
            - Select **CloudWatchAgentServerPolicy** to provide CloudWatch access.
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

## Install Package Dependencies
Installation of dependent packages is required before building the AWS CDI SDK:

- CentOS 7 and Amazon Linux 2:

    ```bash
    sudo yum -y install gcc-c++ make cmake3 libnl3-devel autoconf automake libtool doxygen ncurses-devel
    ```

- Ubuntu:

    ```bash
    sudo apt-get install –y build-essential libncurses-dev autoconf automake libtool libnl-3-dev cmake git doxygen libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
    ```

## Download AWS SDK
AWS SDK C++ will be compiled during the build process of AWS CDI SDK, so it is only necessary to download it.

**Note**: AWS CDI SDK has been tested with version 1.8.46 of AWS SDK C++.

**Note**: The AWS SDK for C++ is essential for metrics gathering functions of AWS CDI SDK to operate properly.  Although not recommended, see [these instructions](./README.md#customer-option-to-disable-the-collection-of-performance-metrics-by-the-aws-cdi-sdk) to learn how to optionally disable metrics gathering.

1. Verify that the necessary [requirements are met and libraries installed for AWS SDK for C++](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/setup-linux.html).
1. Download AWS SDK for C++ source code.
    - **Note**: This procedure replaces these instructions: ["Setting Up AWS SDK for C++"](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/setup.html).
    - Commands to clone AWS SDK for C++ from git for Amazon Linux 2 and Linux CentOS 7 are listed below:

       ```bash
       cd <install_dir>
       git clone -b 1.8.46 https://github.com/aws/aws-sdk-cpp.git
       ```

  The **<install_dir>** should now contain the folder hierarchy as shown below:

   ```
   <install_dir>/aws-cdi-sdk
   <install_dir>/aws-sdk-cpp
   <install_dir>/libfabric
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

    ```
    make DEBUG=y AWS_SDK=../aws-sdk-cpp/
    ```

   **Note**: After a successful compile, the locations for the results are at:
    - Test application: ```cdi_test``` is placed at ```aws-cdi-sdk/build/debug/bin```
    - Minimal test applications: ```cdi_test_min_tx``` and ```cdi_test_min_rx``` are placed at ```aws-cdi-sdk/build/debug/bin```
    - AWS CDI SDK and libfabric shared libraries ```libcdisdk.so.x.x``` and ```libfabric.so.x``` are placed at ```aws-cdi-sdk/build/debug/lib.```
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

If you get an error, please check this issue: https://github.com/aws/aws-cdi-sdk/issues/48

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
  1. The new instance will contain the host name stored in the AMI and not the private IP name used by the new instance. Edit **/etc/hostname**. The private IP name can be obtained from the AWS Console or use **ifconfig** on the instance. For example, if **ifconfig** shows **1.2.3.4** then the name should look something like (region may be different):

     ```
     ip-1-2-3-4.us-west-2.compute.internal
     ```
     This change requires a reboot. Depending on OS variant, there are commands that can be used to avoid a reboot. For example:

     ```
     sudo hostname <new-name>
     ```

---

[[Return to README](README.md)]
