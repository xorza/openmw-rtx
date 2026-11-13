# Open issues

- **`SceneUtil::Material`'s `.osgt` wrapper is registered only by `SceneUtil::writeScene`**
  (`SceneUtil::registerSerializers`): a model the game loads that names `SceneUtil::Material` loses its material, and
  the log says `InputStream::readObject(): Unsupported wrapper class SceneUtil::Material`. The embedded error marker
  (`Resource::ErrorMarker::sValue`) names one.
