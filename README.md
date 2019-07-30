# WMRSensors
Access sensors and tracking status through the Windows Mixed Reality drivers

## Usage
- Copy WMRInterceptHost.dll to a place the Local Services account can access (e.g. `C:\Windows\System32`) or set permissions manually.

- Inject WMRInterceptHost.dll into the WUDFHost process of the Mixed Reality driver (for instance with [Process Hacker](https://processhacker.sourceforge.io/)). Since there may be multiple WUDFHost processes, verify that the target process has its current directory inside `System32\DriverStore\FileRepository\hololenssensors.inf_amd64_<hash>`.

This will open a pipe at `\\.\pipe\wmrcam`, which local programs can connect to in order to retrieve camera images and controller tracking information.

In order to retrieve this information in other projects, include the WMRPipeClient library in your project and create a WMRInterceptPipeClient. A sample is included with the WMRViewer project.

## Build
Building was only tested with Visual Studio Community 2017. x86 (32 bit) build is not supported.
- Get a [glm](https://github.com/g-truc/glm) release and copy the inner glm directory to WMRCamViewer/include.
- Get a binary package of [glew](http://glew.sourceforge.net/) and copy the include directory to WMRCamViewer.
- Correct the Additional Library Directories property of WMRCamViewer to point to the glew lib/Release/x64 directory.
