#pragma once

class QApplication;

namespace Graphite
{
    /// The process's one `QApplication`, made on the first call on the offscreen platform, which draws
    /// into memory and opens no window: what a test of the look renders against.
    QApplication& offscreenApplication();
}
