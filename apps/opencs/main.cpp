#include <cstdint>
#include <string>

#include <QIcon>
#include <QSurfaceFormat>
#include <osg/DisplaySettings>

#include <apps/opencs/model/doc/messages.hpp>
#include <apps/opencs/model/world/disabletag.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <components/debug/debugging.hpp>
#include <components/platform/application.hpp>
#include <components/platform/platform.hpp>

#include "editor.hpp"

Q_DECLARE_METATYPE(std::string)

class QEvent;
class QObject;

namespace
{
    void setQSurfaceFormat()
    {
        osg::DisplaySettings* ds = osg::DisplaySettings::instance().get();
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setVersion(2, 1);
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setDepthBufferSize(24);
        format.setSamples(ds->getMultiSamples());
        format.setStencilBufferSize(static_cast<std::int32_t>(ds->getMinimumNumStencilBits()));
        format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
        format.setAlphaBufferSize(static_cast<std::int32_t>(ds->getMinimumNumAlphaBits()));
        QSurfaceFormat::setDefaultFormat(format);
    }
}

// Qt's Q_INIT_RESOURCE declares its function in the caller's namespace, where an unnamed one would hide the
// definition the resource compiler gives the global one.
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
static std::int32_t runApplication(std::int32_t argc, char* argv[])
{
    Platform::init();

    Q_INIT_RESOURCE(resources);

    Q_INIT_RESOURCE(dark);

    qRegisterMetaType<std::string>("std::string");
    qRegisterMetaType<CSMWorld::UniversalId>("CSMWorld::UniversalId");
    qRegisterMetaType<CSMWorld::DisableTag>("CSMWorld::DisableTag");
    qRegisterMetaType<CSMDoc::Message>("CSMDoc::Message");

    setQSurfaceFormat();
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    Platform::Application application(argc, argv, Platform::Look::System);

    application.setDesktopFileName("org.openmw.cs");
    application.setWindowIcon(QIcon(":openmw-cs"));

    CS::Editor editor(argc, argv);
    // Numbers read and written with a point, as Qt's start on Linux had set the system's; Windows starts in "C"
    // already. Before any thread starts; a locale the system lacks leaves the one it had.
    static_cast<void>(setlocale(LC_NUMERIC, "C")); // NOLINT(concurrency-mt-unsafe): the editor's first act

    if (!editor.makeIPCServer())
    {
        editor.connectToIPCServer();
        return 0;
    }

    return editor.run();
}

int main(std::int32_t argc, char* argv[])
{
    return Debug::wrapApplication(&runApplication, argc, argv, "OpenMW-CS");
}
