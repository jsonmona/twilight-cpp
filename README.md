## Twilight Remote Desktop
This project aims to build a open source remote desktop suitable for any uses including gaming.

This project is currently in pre-alpha, so no prebuilt binaries are provided.

### Current status
Before you read this section, take a look at the last modified date of this file (README.md).
This section might be out-of-date as this project is changing rather quickly.

Currently it only covers one scenario: Windows host + Windows client.
When that works quite well, I'll start working on Android client.

Now it can stream desktop with audio over internet using TLS.

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

### Security
I'm no security expert, so if anyone knows it is insecure, please open an issue.

If client has a certificate signed by server, it can connect without any manual authentication.
Otherwise, server will sign a certificate if client can complete a manual authentication.

Manual authentication works like this:
1. Pin is derived by hashing server's certificate pubkey, client's certificate pubkey, server's nonce, client's nonce concatenated.
1. Then the pin is calculated on the both side.
1. Client will display the pin, and server will prompt to enter the pin.
1. If the pin entered matches the calculated one, the manual authentication successes.
1. Server signs a certificate and sends it to client.

I'm planning to change it not sign a certificate, but rather record client's pubkey into server's registry.
That way, server can list allowed client list and revoke freely.

### License

This project is licensed under **GPLv3+**.

You can redistribute it and/or modify this project under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Beware that external libraries (under `external/`) may have different license terms,
and might be incompatible with later versions of GPL.

A full copy of GPLv3 is included at `LICENSE.txt`.
