## Daylight Desktop Streaming Project

This project aims to build a open source remote desktop suitable for any uses including gaming.

This project is currently in pre-alpha, so no prebuilt binaries are provided.

### Current status
Before you read this section, take a look at the last modified date of this file (README.md).
This section might be out-of-date as this project is changing rather quickly.

Now it can stream desktop with audio over internet using TLS+TCP.
The data is encrypted and it tries to prevent MITM (Man-in-the-Middle) using PIN authentication.
It uses an idea borrowed from Bluetooth pairing mechaism, but I'm not sure how secure it is.
Remote control mechanism is comming soon!

Streaming in localhost lags by 6 frames (100 ms) on average.
I'm investigating why, but I'm not sure yet.

### How to clone
This project uses git submodule.
Please clone with `git clone --recursive` or, in case you cloned without that option,
run `git submodule update --init --recursive` after cloning.

### How to build
This project uses CMake build system.
You need a modern version of CMake with a C++17 compiler.
You also need Qt6 if you want to build client.

Currently only Windows 10 with MSVC is tested and supported.
More platforms will be supported later.

Please consult CMakeLists.txt on root directory for build options.

### License

This project is licensed under **GPLv3+**.

You can redistribute it and/or modify this project under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Beware that external libraries (under `external/`) may have different license terms,
and might be incompatible with later versions of GPL.

A full copy of GPLv3 is included at `LICENSE.txt`.
