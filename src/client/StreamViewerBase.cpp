#include "StreamViewerBase.h"

#include "common/RingBuffer.h"
#include "common/platform/windows/winheaders.h"

#include <packet.pb.h>
#include <algorithm>


StreamViewerBase::StreamViewerBase() : QWidget() {
}

StreamViewerBase::~StreamViewerBase() {
}
