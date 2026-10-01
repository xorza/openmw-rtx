# Open issues
- `ContentModel::dropMimeData` (`components/contentselector/model/contentmodel.cpp`) indexes `mFiles.at(sourceRow)` with row numbers read from the drop's data, unchecked; a drop from another process carries rows of that process's list.
