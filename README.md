VxWorks® 7 Layer for VSOMEIP
===
---

# Overview

The VxWorks 7 Layer for VSOMEIP provides the makefiles for building the
[vsomeip](https://github.com/COVESA/vsomeip) 3.1.20.3 release on VxWorks.

This layer is an adapter to make standard SOME/IP build and run on
VxWorks. This layer does not contain the vsomeip source, it only
contains all functions required to allow vsomeip to build and execute
on top of VxWorks. Use this layer to add the vsomeip library to your 
user space, and to build the vsomeip example applications as RTPs.

# Project License

The source code for this project is provided under the Mozilla Public License, v. 2.0. license. 
Text for the vsomeip dependencies and other applicable license notices can be found in 
the License_Notices.txt file in the project top level directory. Different 
files may be under different licenses. Each source file should include a 
license notice that designates the licensing terms for the respective file.

# Prerequisite(s)

* Install the Wind River® VxWorks® 7 operating system version 23.09 or later.

* The build system will need to download [vsomeip](https://github.com/COVESA/vsomeip) source code from github.com.  A
  working Internet connection with access to both sites is required.

* Install the git tool and ensure it operates from the command line.

### Setup

1. Download the **VxWorks 7 layer for VSOMEIP* from the following location:

https://github.com/Wind-River/vsomeip

2. Set WIND_LAYER_PATHS to point to the vxworks7-layer-for-VSOMEIP directory. Command-line users may set this directly using export on Linux or set on Windows. Developers working on a Microsoft Windows host may also set the system environment variables. On Microsoft Windows 10, these can be found in the Control Panel under View advanced system Settings. Click the "Advanced" tab to find the "Environment Variables" button. From here you may set WIND_LAYER_PATHS to point to the vxworks7-layers-for-VSOMEIP. Please refer to the VxWorks documentation for details on the WIND_LAYER_PATHS variable.  

3. Confirm the layer is present in your VxWorks 7 installation. In a VxWorks development shell, you may run "vxprj vsb listAll" and look for VSOMEIP_3_1_20_3 to confirm that the layer has been found.

# Legal Notices

All product names, logos, and brands are property of their respective owners. All company, 
product and service names used in this software are for identification purposes only. 
Wind River and VxWorks are registered trademarks of Wind River Systems, Inc.

Disclaimer of Warranty / No Support: Wind River does not provide support 
and maintenance services for this software, under Wind River’s standard 
Software Support and Maintenance Agreement or otherwise. Unless required 
by applicable law, Wind River provides the software (and each contributor 
provides its contribution) on an “AS IS” BASIS, WITHOUT WARRANTIES OF ANY 
KIND, either express or implied, including, without limitation, any warranties 
of TITLE, NONINFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A PARTICULAR 
PURPOSE. You are solely responsible for determining the appropriateness of 
using or redistributing the software and assume any risks associated with 
your exercise of permissions under the license.
