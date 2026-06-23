/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizDataExchangeFormat_h
#define tomvizDataExchangeFormat_h

#include <string>

#include <QVariantMap>

#include "HDF5ReadResult.h"

class vtkImageData;

namespace tomviz {

class DataExchangeFormat
{
public:
  // This will only read /exchange/data, nothing else
  bool read(const std::string& fileName, vtkImageData* data,
            const QVariantMap& options = QVariantMap());
  // Read everything into an HDF5ReadResult (no DataSource needed)
  HDF5ReadResult readAll(const std::string& fileName,
                         const QVariantMap& options = QVariantMap());
private:
  // Read the dark dataset into the image data
  bool readDark(const std::string& fileName, vtkImageData* data,
                const QVariantMap& options = QVariantMap());
  // Read the white dataset into the image data
  bool readWhite(const std::string& fileName, vtkImageData* data,
                 const QVariantMap& options = QVariantMap());
  // Read the theta angles from /exchange/theta
  QVector<double> readTheta(const std::string& fileName,
                            const QVariantMap& options = QVariantMap());
};
} // namespace tomviz

#endif // tomvizDataExchangeFormat_h
