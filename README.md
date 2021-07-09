## Daylight Desktop Streaming Project

This project aims to build a open source remote desktop suitable for any uses including gaming.

This project is currently in pre-alpha, so no prebuilt binaries are provided.

### Current status
Before you read this section, take a look at the last modified date of this file (README.md).
This section might be out-of-date.

Now it can stream desktop (without audio) over internet using TCP.
No remote control mechanism is implemented.

Client renders with bad frame pacing, but it does show the image.

### How to build
This project uses git submodule.
Please use `git clone --recursive` or run `git submodule update --init --recursive` after cloning.

Please consult CMakeLists.txt on root directory for build options.

### License

This project is licensed under **GPLv3+**.

You can redistribute it and/or modify this project under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Beware that external libraries (under `external/`) may have different license terms,
and might be incompatible with later versions of GPL.

A full copy of GPLv3 is included at `LICENSE.txt`.
