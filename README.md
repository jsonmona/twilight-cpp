## Daylight Desktop Streaming Project

This project aims to build a open source remote desktop suitable for any uses including gaming.

This project is currently in pre-alpha, so no prebuilt binaries are provided.

### Current status
Before you read this section, take a look at the last modified date of this file (README.md).

Current objective is to implement basic streaming for Windows.
Both server and client only works on Windows, but other platforms could be supported in future.
At this point, capturing desktop to `D3D11Texture2D` and a crude hardware-accelerated H264 encoding is implemented.

Client is being implemented.
You can use ffplay for this time being.

### How to build
This project uses git submodule.
Please run `git submodule init` after cloning.

Please consult CMakeLists.txt on root directory for build options.

### License

**GPLv3 or later**

This project is licensed at GPLv3 or any later version published by Free Software Foundation.

Full copy of GPLv3 is available at `LICENSE.txt`.
