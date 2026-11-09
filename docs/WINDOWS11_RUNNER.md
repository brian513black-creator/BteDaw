# Connect a Windows 11 test PC

The cloud development environment is Linux. To run the application on actual Windows 11, connect a Windows 11 x64 PC as a repository-scoped GitHub Actions runner. The Windows 11 validation workflow is manual only; pull requests and ordinary pushes do not run that job on your PC.

## Prepare the PC

Install these tools using their official installers:

- [Visual Studio 2022 Community](https://visualstudio.microsoft.com/vs/community/) with **Desktop development with C++**, MSVC x64 tools and a Windows SDK selected in Visual Studio Installer.
- [CMake](https://cmake.org/download/) and [Git for Windows](https://git-scm.com/download/win), added to PATH.
- [NSIS](https://nsis.sourceforge.io/Download), used to create the Windows installer.

Restart PowerShell after installing tools. Connect the audio interface and MIDI keyboard you intend to test and install their normal vendor drivers. The current build uses WASAPI; ASIO is not included.

## Register and start the runner

1. Open [BteDaw repository settings](https://github.com/brian513black-creator/BteDaw/settings/actions/runners).
2. Choose **Actions → Runners → New self-hosted runner**, then **Windows** and **x64**. Repository administrator access is needed for this page.
3. On the Windows PC, open PowerShell in a folder such as `C:\actions-runner` and follow the exact download, verification and configuration commands GitHub displays. Its temporary registration token is used only in those commands on your PC.
4. Accept the default runner group. At the additional-labels prompt, enter **`windows11-daw`**. Keep the default `self-hosted`, `Windows` and `X64` labels.
5. When asked whether to run as a Windows service, choose **No**. Run `.\run.cmd` while signed into Windows instead; GUI/audio validation needs an interactive desktop session.
6. Leave the runner terminal open. The repository's Runners page should show **Idle** when it is connected and waiting.

## Run the checks

Open [Actions → Windows 11 validation](https://github.com/brian513black-creator/BteDaw/actions/workflows/windows11-validation.yml), choose **Run workflow**, select `main`, and start it. Keep the PC awake, signed in, and unlocked while the GUI checks run.

The job verifies a desktop Windows 11 build (not Windows Server), the interactive session, and prerequisites before compiling. It runs the automated engine/workstation checks and application startup, creates unsigned installer/portable packages, and uploads source, checksums, OS/audio-device metadata and test logs. It does not install or uninstall the application on your PC automatically.

If the job stays **Queued**, confirm the runner is **Idle** and has the exact `windows11-daw` label. If preflight fails, its log names the missing prerequisite. Stop the runner with Ctrl+C when testing is finished; start `.\run.cmd` again to reconnect it.

A green run establishes the automated checks on that Windows 11 machine. Finish and record [Windows acceptance](WINDOWS_ACCEPTANCE.md), including installation, physical recording/MIDI, third-party plugins and long-session tests, before calling it a validated professional release.

The manual workflow now includes bounded CTest stress coverage and a larger generated-audio workload. Set `stress_seconds` when starting it; reports are included in the validation-evidence artifact. Read [STRESS_TESTING.md](STRESS_TESTING.md) to distinguish integrity results from callback timing and physical audio tests.
