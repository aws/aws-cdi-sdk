# Windows Installation Guide
Installation instructions for the AWS Cloud Digital Interface (CDI) SDK on Windows instances.

**In addition to filing [bugs/issues](https://github.com/aws/aws-cdi-sdk/issues), please use the AWS CDI SDK [discussion pages](https://github.com/aws/aws-cdi-sdk/discussions) for Q&A, Ideas, Show and Tell or other General topics so the whole community can benefit.**

---

- [Windows Installation Guide](#windows-installation-guide)
- [Upgrading from previous releases](#upgrading-from-previous-releases)
- [Create an EFA enabled instance](#create-an-efa-enabled-instance)
- [Connecting to Windows and activating](#connecting-to-windows-and-activating)
- [Configure the EC2 Instance](#configure-the-ec2-instance)
  - [Create IAM user required by AWS CloudWatch](#create-iam-user-required-by-aws-cloudwatch)
  - [Add tools to the System Environment Variable Path](#add-tools-to-the-system-environment-variable-path)
- [Install and build the AWS CDI SDK](#install-and-build-the-aws-cdi-sdk)
- [Manually building CDI libraries and test applications](#manually-building-cdi-libraries-and-test-applications)
  - [Building the AWS CDI SDK with Microsoft Visual Studio IDE](#building-the-aws-cdi-sdk-with-microsoft-visual-studio-ide)
  - [(Optional) Disable the display of performance metrics to your Amazon CloudWatch account](#optional-disable-the-display-of-performance-metrics-to-your-amazon-cloudwatch-account)
- [Build the HTML documentation](#build-the-html-documentation)
- [Creating additional instances](#creating-additional-instances)
- [Running the Windows test application](#running-the-windows-test-application)
  - [Allow test applications in Windows firewall](#allow-test-applications-in-windows-firewall)
  - [Help](#help)
  - [Additional tests](#additional-tests)
  - [Windows specific test differences from Linux](#windows-specific-test-differences-from-linux)
    - [Disabling the SHM transfer error message](#disabling-the-shm-transfer-error-message)
    - [Using the libfabric socket adapter on instances without an EFA adapter](#using-the-libfabric-socket-adapter-on-instances-without-an-efa-adapter)

---

# Upgrading from previous releases

**Upgrading from CDI SDK 2.4 or earlier**

* Install in a clean folder. A PowerShell script is used to install the AWS CDI SDK and required components. See steps in [Install the AWS CDI SDK](#install-the-aws-cdi-sdk).

---
# Create an EFA enabled instance

Follow the steps in [create an EFA-enabled instance](README.md#create-an-efa-enabled-instance).

# Connecting to Windows and activating

Connect to your EC2 instance using Remote Desktop.
Refer to **step 2** in the [AWS Windows Guide](https://docs.aws.amazon.com/AWSEC2/latest/WindowsGuide/EC2_GetStarted.html).
If Windows activation fails, see [these instructions](https://aws.amazon.com/premiumsupport/knowledge-center/windows-activation-fails/).

---

# Configure the EC2 Instance

1. (Optional) To install a Windows SSH OpenSSH server:

    **Note**: You do not need to perform this step if you plan to use RDP to connect to the instance. See [Windows and OpenSSH](https://docs.microsoft.com/en-us/windows-server/administration/openssh/openssh_install_firstuse) for more information.

    - Connect to the instance using an RDP client.
    - Invoke Windows PowerShell and then run the following commands:

    ```powershell
    Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0
    Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
    Start-Service sshd
    Set-Service -Name sshd -StartupType 'Automatic'
    ```

1. (Optional) To ease tool download, install the **Chocolatey package manager** using this command:

    ```powershell
    Set-ExecutionPolicy Bypass -Scope Process -Force; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
    ```

1. (Optional) Install Git with Chocolatey from Powershell:

    ```powershell
    choco install git.install -y
    ```

    **Note**: After installing git, the PowerShell window needs to be closed and reopened before the ```git``` command will be recognized.

1. Install Doxygen

    ```powershell
    choco install doxygen.install -y
    ```

    **Note**: After installing Doxygen, the PowerShell window needs to be closed and reopened before the ```doxygen``` command will be recognized.

1. Install Microsoft Visual Studio 2019 and native desktop (C/C++) components with Chocolatey from Powershell:

    ```powershell
    choco install visualstudio2019community -y
    choco install visualstudio2019-workload-nativedesktop -y
    ```

    **Note**: These commands can take some time to complete and will not provide an indication of progress.

1. To install Microsoft Visual Studio using a web browser, navigate to the [Visual Studio download page](https://visualstudio.microsoft.com/downloads/) and then follow this procedure:

    **Note**: Microsoft security is locked down tightly on Internet Explorer, which Visual Studio invokes. To disable this enhanced security:

    - From the Windows start menu, search for and select **Server Manager**.
    - In the **Server Manager** settings, select **Local Server** and enter the name of the server you are currently using to disable IE Enhanced Security.
    - The **IE Enhanced Security Setting** defaults to **On**. Click the link to turn it off, thus disabling the enhanced security.
    - **Note**: Ensure that you disable the setting for both the **Administrator** and for all other users.
    - Select OK, and then exit the **Server Manager**.
    - Reboot for changes to take effect.

1. Verify that CMake version 3.2 or higher is installed. If CMake is not installed, [download and install version 3.18.5](https://cmake.org/download/).


## Create IAM user required by AWS CloudWatch

AWS CloudWatch is required to build the AWS CDI SDK, and is provided in [AWS SDK C++](https://aws.amazon.com/sdk-for-cpp/).

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
        - Save your **Access Key ID** and **Secret Access Key** from this IAM User creation.

## Add tools to the System Environment Variable Path

- Add installed tools to the System Environment Variable: **Path**.
    1. Select the **Windows Start button**.
    1. Type **Environment**, and it will match with **Edit the system environment variables**. Select this suggested option.
    1. In the resulting pop-up window, select **Environment Variables**.
    1. In the **System Variables** section, select the **Path** variable and click **Edit**, then click **New**.
    1. Add the following path into the new text area: ```C:\Program Files\CMake\bin```
    1. Click **New** again and add the following path into the text area: ```C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin```
    1. Click **New** again and add the following path into the text area: ```C:\Program Files\doxygen\bin```
    1. select **OK**.

---

# Install and build the AWS CDI SDK

**Caution**: Do not install a new version of the AWS CDI SDK over an old one.

**Note**: **Windows PowerShell** and [git for Windows](https://git-scm.com/download/win) are used to acquire source repositories.

1. From within a PowerShell in **Administrator mode**, run **install.ps1** from within the ```aws-cdi-sdk/proj``` folder as shown below. It will install the EFADriver required by Windows, the **AWS SDK C++**, two **libfabric** variants, **efawin** and **PDCurses**. After installing the components, it will use Microsoft Visual Studio's MSBuild.exe to build a debug configuration of the AWS CDI SDK.

    ```powershell
    .\install.ps1
    ```

1. This will prompt with something like the following:

    ```powershell
    Security warning
    Run only scripts that you trust. While scripts from the internet can be useful, this script can potentially harm your
    computer. If you trust this script, use the Unblock-File cmdlet to allow the script to run without this warning
    message. Do you want to run install.ps1?
    [D] Do not run  [R] Run once  [S] Suspend  [?] Help (default is "D"):
    ```

1. Chose 'Run once' to install. The **<install_dir>** should now contain the folder hierarchy as shown below:

   ```
   <install_dir>\aws-cdi-sdk
   <install_dir>\aws-sdk-cpp
   <install_dir>\efawin
   <install_dir>\libfabric
   <install_dir>\libfabric_new
   <install_dir>\PDCurses
   ```

1. Add the AWS SDK for C++ to the system **PATH**. The installation procedure creates a folder called ```aws-cpp-sdk-all``` in ```C:\Program Files (x86)```. This is the location the of the resulting .dlls and .libs along with the necessary include header files. Add ```C:\Program Files (x86)\aws-cpp-sdk-all\bin``` to the **Path** System Environment Variable using the steps outlined in the section: [Add tools to the System Environment Variable Path](#add-tools-to-the-system-environment-variable-path).


The **proj** directory contains the Visual Studio project solution for Windows development.

- The solution file builds the AWS CDI SDK library, ```cdi_sdk.lib``` or ```cdi_sdk.dll```, including its dependencies, ```libfabric.dll```, ```libfabric_new.dll```, ```cdi_libfabric_api.lib```, ```cdi_libfabric_new_api.lib```, and the test applications ```cdi_test.exe```, ```cdi_test_min_tx.exe``` and ```cdi_test_min_rx.exe```.
- For detailed folder descriptions, refer to the descriptions in the [Linux installation guide](./INSTALL_GUIDE_LINUX.md#install-aws-cdi-sdk).


**Note**: The AWS SDK for C++ installed at ```<install_dir>\aws-sdk-cpp``` is essential for metrics gathering functions of AWS CDI SDK to operate properly. Although not recommended, see [these instructions](./README.md#customer-option-to-disable-the-collection-of-performance-metrics-by-the-aws-cdi-sdk) to learn how to optionally disable metrics gathering.

---

# Manually building CDI libraries and test applications

The AWS CDI SDK solution, *cdi_proj.sln*, contains three test applications: *cdi_test*, *cdi_test_min_rx*, and *cdi_test_min_tx*. Each project can be built using either Debug or Release configurations.

## Building the AWS CDI SDK with Microsoft Visual Studio IDE

This procedure builds the entire AWS CDI SDK solution in a Debug configuration.

**Note**: It is recommended to use Microsoft Visual Studio 2019, but Visual Studio 2022 can be used if the optional component **MSVC v142 - VS 2019 C++ x64/x86 build tools** is installed. Please refer to Microsoft's documentation on how to install it.

1. Use Microsoft Visual Studio to open the *cdi_proj.sln* solution file found at ```<install directory path>/aws-cdi-sdk/proj/cdi_proj.sln```.
1. Choose a configuration. For this example, choose **Debug**. **Note**: To use a DLL configuration, the equivalent **Debug** or **Release** configuration must be built first.
1. When switching between debug and release configurations, clean the solution by selecting: **Build** > **Clean Solution**.
1. Build the solution by selecting: **Build** > **Build Solution**. This builds all libraries and applications. **Note**: The test applications are only configured to use static libraries, so must use **Debug** or **Release** configurations when building them.
1. Choose the application to run. By default, the *cdi_test* application runs. To select another application, right-click on the target application and choose **Set as Startup Project** for the application you want to run.

## (Optional) Disable the display of performance metrics to your Amazon CloudWatch account

To disable the display of performance metrics to your Amazon CloudWatch account:

- In the file ```src/cdi/configuration.h```, comment out ```#define CLOUDWATCH_METRICS_ENABLED```.

**Note**: For the change to take effect, the CDI SDK library and related applications must be rebuilt.

---

# Build the HTML documentation

To build the AWS CDI SDK documentation, install Doxygen using Chocolatey and configure Visual Studio to build it:

1. To build the documentation:
    - Open a ```powershell``` or ```cmd``` Window:
      - To open a Cmd window, press the Windows key or select the search icon in the Windows Start menu, and type ```cmd```.  To open a PowerShell window, type ```powershell``` instead.
    - Navigate to Visual Studio project directory in the aws-cdi-sdk repo (i.e. ```aws-cdi-sdk\proj```).
    - Run make_docs.bat to build the documentation.
        - To build the full documentation for the SDK and the test applications, run the following command:

            ```batch
            .\make_docs.bat all
            ```

            Documents will appear in the directory ```aws-cdi-sdk\build\documentation\all```.

        - To build only the API documentation, run the following command:

            ```batch
            .\make_docs.bat api
            ```

            Documents will appear at ```aws-cdi-sdk\build\documentation\api```.

1. Use a web browser to open aws-cdi-sdk\build\documentation\\**api|all**\html\index.html, choosing **api** or **all** depending on the build option you chose.

---

# Creating additional instances

Refer to the Linux Guide [Creating additional instances](./INSTALL_GUIDE_LINUX.md#creating-additional-instances) for steps and information. NOTE: The **hostname** does not have to be changed as described in the Linux Guide.

---

# Running the Windows test application

The test application provides a means to quickly validate that your environment is set up correctly to use the AWS CDI SDK. Use the commands listed in this section from the aws-cdi-sdk folder.

## Allow test applications in Windows firewall

Do the following to ensure the firewall does not block traffic to or from the test applications:

- Open Windows Defender Firewall and select **Allow an app or feature through Windows Defender Firewall**.

- Click **Allow another app...**

- Click **Browse...** and navigate to the test application executable that was built in [Build CDI libraries and test applications](#build-cdi-libraries-and-test-applications).

**Note**: Each executable must be individually allowed through the firewall. As an example cdi_test_minimal_rx.exe and cdi_test_minimal_tx.exe must be allowed individually. "Debug" and "Release" variants of executables must also be individually allowed.

## Help

Running the ```--help``` command displays all of the command-line options for the ```cdi_test.exe``` application.

```powershell
.\proj\x64\Debug\cdi_test.exe --help
```

For file-based command-line arguments, refer to the [USER_GUIDE_TEST_APP.md](./USER_GUIDE_TEST_APP.md#using-file-based-command-line-argument-insertion).
Additionally, there are several build configuration options for the AWS CDI SDK library that aid in debugging and development. For details refer to ```aws-cdi-sdk/src/cdi/configuration.h```.

## Additional tests

For more examples of tests, refer to [Running the Full-Featured Test Application](./USER_GUIDE_TEST_APP.md#running-the-full-featured-test-application) and [Running the Minimal Test Applications](./USER_GUIDE_TEST_APP.md#running-the-minimal-test-applications). All examples are Windows-compliant. The path to the test executables are at ```.\proj\x64\Debug\cdi_test.exe```, ```.\proj\x64\Debug\cdi_test_min_tx.exe``` and ```.\proj\x64\Debug\cdi_test_min_rx.exe```.

## Windows specific test differences from Linux
While the functionality of the AWS CDI SDK in Windows and Linux is the same, there are some slight differences.

1. Use **ipconfig.exe** in PowerShell to retrieve the local IP address.
1. Command-line insertion can also be entered in Microsoft Visual Studio for any configuration.
    1. Choose **Debug > Properties > Configuration** and  **Properties > Debugging > Command Arguments**
1. When using logs in Windows, verify that the targeted directory exists. The application will exit if the directory does not exist.

### Disabling the SHM transfer error message

When running applications built on the AWS CDI SDK in Windows, the following message may appear during startup:

```bash
SHM transfer will be disabled because of ptrace protection.
To enable SHM transfer, please refer to the man page fi_efa.7 for more information.
Also note that turning off ptrace protection has security implications. If you cannot
turn it off, you can suppress this message by setting FI_EFA_ENABLE_SHM_TRANSFER=0
```

This message can be safely ignored, or it can be eliminated by creating a Windows environment variable called ```FI_EFA_ENABLE_SHM_TRANSFER``` and setting it to ```0```.

---

### Using the libfabric socket adapter on instances without an EFA adapter

When using the [libfabric sockets adapter](USER_GUIDE_TEST_APP.md#testing-cdi-with-the-libfabric-sockets-adapter) on an instance that does not have an EFA adapter, remove the AWS CDI SDK built file **efawin.dll** to prevent problems from occurring during initialization of libfabric. It can be found in the build result folder located at **aws-cdi-sdk/proj/x64/\<configuration\>**.

[[Return to README](README.md)]
