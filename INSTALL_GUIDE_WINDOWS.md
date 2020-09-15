# Windows Installation Guide
Installation instructions for the AWS Cloud Digital Interface (CDI) SDK on Windows instances.

---

- [Connecting to Windows and activating](#connecting-to-windows-and-activating)
- [Install the Windows EFA driver](#install-the-windows-efa-driver)
- [Configure the EC2 Instance](#configure-the-ec2-instance)
- [Install the AWS CDI SDK](#install-the-aws-cdi-sdk)
- [Build the HTML documentation](#build-the-html-documentation)
- [Build CDI libraries and test applications](#build-cdi-libraries-and-test-applications)
  - [Install the AWS SDK for C++ on Windows](#install-the-aws-sdk-for-c-on-windows)
  - [Download and build the AWS SDK for C++](#download-and-build-the-aws-sdk-for-c)
  - [Build the AWS CDI SDK](#build-the-aws-cdi-sdk)
  - [Disabling the display of performance metrics to your Amazon CloudWatch account](#disabling-the-display-of-performance-metrics-to-your-amazon-cloudwatch-account)
- [Running the Windows test application](#running-the-windows-test-application)
  - [Allow test applications in Windows firewall](#allow-test-applications-in-windows-firewall)
  - [Help](#help)
  - [Additional tests](#additional-tests)
  - [Windows specific test differences from Linux](#windows-specific-test-differences-from-linux)
    - [Disabling the EFA transfer error message](#disabling-the-shm-transfer-error-message)

---

# Connecting to Windows and activating

After following the steps to [create an EFA-enabled instance](README.md#create-an-efa-enabled-instance), connect to your EC2 instance using Remote Destop.
Refer to **step 2** in the [AWS Windows Guide](https://docs.aws.amazon.com/AWSEC2/latest/WindowsGuide/EC2_GetStarted.html).
If Windows activation fails, see [these instructions](https://aws.amazon.com/premiumsupport/knowledge-center/windows-activation-fails/).

---

# Install the Windows EFA driver

To run on Windows, an EFA driver must be installed manually. Obtain the latest copy of the [Windows EFA driver ZIP file](https://ec2-windows-drivers-efa.s3-us-west-2.amazonaws.com/Latest/EFADriver.zip).

1. Unzip the file to a folder called **EFADriver**.

1. Run **install.ps1** from within the **EfaDriver** folder to install the certificate and the driver.

    ```powershell
    .\install.ps1
    ```

1. This will prompt the following:

    ```powershell
    Security warning
    Run only scripts that you trust. While scripts from the internet can be useful, this script can potentially harm your
    computer. If you trust this script, use the Unblock-File cmdlet to allow the script to run without this warning
    message. Do you want to run C:\Users\Administrator\Downloads\EFADriver\install.ps1?
    [D] Do not run  [R] Run once  [S] Suspend  [?] Help (default is "D"):
    ```

1. Chose 'Run once' to install.

If the installation is successful, the output will look similar to this:

```powershell
Installing efa kernel driver: "efa.inf"
AWS_DEV_DRV_INSTALLER: Device driver successfully installed!
Completed installation.
0
```

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

    **Note**: After installing git, the Powershell window needs to be closed and reopened before the ```git``` command will be recognized.

1. Install Doxygen

    ```powershell
    choco install doxygen.install -y
    ```

    **Note**: After installing Doxygen, the Powershell window needs to be closed and reopened before the ```doxygen``` command will be recognized.

1. Install Microsoft Visual Studio 2019 with Chocolatey from Powershell:

    ```powershell
    choco install visualstudio2019community -y
    ```

    **Note**: This command can take some time to complete and will not provide an indication of progress.

1. To install Microsoft Visual Studio using a web browser, navigate to the [Visual Studio download page](https://visualstudio.microsoft.com/downloads/) and then follow this procedure:

    **Note**: Microsoft security is locked down tightly on Internet Explorer, which Visual Studio invokes. To disable this enhanced security:

    - From the Windows start menu, search for and select **Server Manager**.
    - In the **Server Manager** settings, select **Local Server** and enter the name of the server you are currently using to disable IE Enhanced Security.
    - The **IE Enhanced Security Setting** defaults to **On**. Click the link to turn it off, thus disabling the enhanced security.
    - **Note**: Ensure that you disable the setting for both the **Administrator** and for all other users.
    - Select OK, and then exit the **Server Manager**.
    - Reboot for changes to take effect.

---

# Install the AWS CDI SDK

**Caution**: Do not install a new version of the AWS CDI SDK over an old one.

**Note**: Refer to the [linux installation guide](./INSTALL_GUIDE_LINUX.md#install-aws-cdi-sdk) for information about how to acquire the code repositories, steps to install, and folder descriptions.
**Windows PowerShell** and [git for windows](https://git-scm.com/download/win) may be used while following the steps outlined in the Linux installation guide, or the code may be downloaded directly from zip archives.

(Optional) Download and install **PDCurses**.
    **Note**: **PDCurses** is used for the ```cdi_test.exe``` application's multi-window mode for formatted console output. It is **not** required for the AWS CDI SDK to be built or used. Your download and use of this third party content is at your election and risk, and may be subject to additional terms and conditions. Amazon is not the distributor of content you elect to download from third party sources, and expressly disclaims all liability with respect to such content.

1. Download and install the most recent stable version of **PDCurses**.

1. Clone the GitHub repo linked at [PDCurses](https://pdcurses.org/).

1. Place the **PDCurses** folder at the same level as the **aws-cdi-sdk** and **libfabric** folders.

The **proj** directory contains the Visual Studio project solution for Windows development.

- The solution file builds the AWS CDI SDK static library, ```cdi_sdk.lib```, and the test applications ```cdi_test.exe```, ```cdi_test_min_tx.exe``` and ```cdi_test_min_rx.exe```.
- For detailed folder descriptions, refer to the descriptions in the [Linux installation guide](./INSTALL_GUIDE_LINUX.md#install-aws-cdi-sdk).

---

# Build the HTML documentation

To build the AWS CDI SDK documentation, install Doxygen using Chocolatey and configure Visual Studio to build it:

1. To build the documentation:
    - Open a ```powershell``` or ```cmd``` Window:
      - To open a Cmd window, press the Windows key or select the search icon in the Windows Start menu, and type ```cmd```.  To open a Powershell window, type ```powershell``` instead.
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

# Build CDI libraries and test applications

The extracted AWS CDI SDK solution, cdi_proj.sln, contains three test applications: *cdi_test*, *cdi_test_min_rx*, and *cdi_test_min_tx*. Each project can be built using either the Debug or Release configuration.

## Install the AWS SDK for C++ on Windows

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
1. Verify that CMake version 3.2 or higher is installed. If CMake is not installed, [download the latest version](https://cmake.org/download/).
1. Add CMake to the System Environment Variable: **Path**.
    - Select the **Windows Start button**.
    - Type **Environment**, and it will match with **Edit the system environment variables**. Select this suggested option.
    - In the resulting pop-up window, select **Environment Variables**.
    - In the **System Variables** section, select the **Path** variable and click **New**.
    - Add the following path into the new text area: ```C:\Program Files\CMake\bin``` and select **OK**.

## Download and build the AWS SDK for C++

**Note**: The AWS CDI SDK was tested with AWS SDK version 1.8.46.

1. Follow the instructions from the [AWS SDK Developer guide](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/setup.html) taking note of the following.
    - Additional files for AWS performance metrics gathering need to be copied from the AWS CDI SDK directory to the AWS SDK directory before running any of the ```cmake``` commands. Windows Explorer or a Powershell command like ```copy -recurse .\aws-cpp-sdk-cdi\ <path to AWS SDK>``` from the top level AWS CDI SDK can be used to perform this step.
    - Only the ```monitoring``` and ```cdi``` modules need to be built. Specify ```-D BUILD_ONLY="monitoring;cdi"``` with the first ```cmake``` command.
    - **Important:** You must run the install commands in a shell that was started in **Administrator mode**. For example:
        - Open Microsoft Visual Studio 2019 in Administrator mode.
        - Open **Tools > Command Line > Developer Powershell**.
    - **Note**: The AWS SDK build process has two configurations: Debug and Release. If you choose to run the AWS CDI SDK in Debug mode, set the appropriate flags in CMake to Debug when installing. Set the appropriate flags in CMake to Release if you install AWS CDI SDK in Release mode.
1. The AWS SDK C++ installation procedure in step 1 creates a folder called ```aws-cpp-sdk-all``` in ```C:\Program Files(x86)```. This is the location of the resulting .dlls and .libs along with the necessary include header files.
    - The folder contains 3 directories: ```bin```, ```include```, and ```lib```.
    - Add ```C:\Program Files (x86)\aws-cpp-sdk-all\bin``` to the **Path** System Environment Variables.

## Build the AWS CDI SDK

This procedure builds the entire AWS CDI SDK solution in a Debug configuration.

1. Use Microsoft Visual Studio 2019 to open the *cdi_proj.sln* solution.
1. Choose a configuration. For this example, choose **Debug**.
1. Clean the solution each time a configuration is changed by selecting: **Build** > **Clean Solution**.
1. Build the solution by selecting: **Build** > **Build Solution**. This builds all libraries and applications.
1. Choose the application to run. By default, the *cdi_test* application runs. To select another application, right-click on the target application and choose **Set as Startup Project** for the application you want to run.

## Disabling the display of performance metrics to your Amazon CloudWatch account

To disable the display of performance metrics to your Amazon CloudWatch account:

- In the file ```src/cdi/configuration.h```, comment out ```#define CLOUDWATCH_METRICS_ENABLED```.

---

# Running the Windows test application

The test application provides a means to quickly validate that your environment is set up correctly to use the AWS CDI SDK. Use the commands listed in this section from the aws-cdi-sdk folder.

## Allow test applications in Windows firewall

Do the following to ensure the firewall does not block traffic to or from the test applications:

- Open Windows Defender Firewall and select **Allow an app or feature through Windows Defender Firewall**.

- Click **Allow another app...**

- Click **Browse...** and navigate to the test application executable that was built in [Build CDI libraries and test applications](#build-cdi-libraries-and-test-applications).

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

1. Use **ipconfig.exe** in Powershell to retrieve the local IP address.
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

[[Return to README](README.md)]
