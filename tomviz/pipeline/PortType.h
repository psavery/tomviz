/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelinePortType_h
#define tomvizPipelinePortType_h

#include <QFlags>
#include <QString>

namespace tomviz {
namespace pipeline {

/// Port data types.
///
/// Volume-like hierarchy:
///   ImageData  — any volumetric data (agnostic about tilt angles)
///   TiltSeries — volumetric data WITH tilt angles  (subtype of ImageData)
///   Volume     — volumetric data WITHOUT tilt angles (subtype of ImageData)
enum class PortType
{
  None = 0,
  ImageData = 1,
  TiltSeries = 2,
  Volume = 4,
  Image = 8,
  Scalar = 16,
  Array = 32,
  Table = 64,
  Molecule = 128
};

Q_DECLARE_FLAGS(PortTypes, PortType)
Q_DECLARE_OPERATORS_FOR_FLAGS(PortTypes)

/// True if @a type is any volume-like type (ImageData, TiltSeries, Volume).
inline bool isVolumeType(PortType type)
{
  return type == PortType::ImageData || type == PortType::TiltSeries ||
         type == PortType::Volume;
}

/// Subtype-aware compatibility check.
///
/// Returns true if an output of @a outputType can connect to an input that
/// accepts @a acceptedTypes.  ImageData is a supertype: an input that accepts
/// ImageData also accepts TiltSeries and Volume.
inline bool isPortTypeCompatible(PortType outputType, PortTypes acceptedTypes)
{
  // Direct match (works for all types)
  if (acceptedTypes.testFlag(outputType))
    return true;
  // ImageData is supertype of TiltSeries and Volume
  if (acceptedTypes.testFlag(PortType::ImageData) &&
      (outputType == PortType::TiltSeries || outputType == PortType::Volume))
    return true;
  return false;
}

/// PortType ↔ string conversions used by the state-file schema.
inline QString portTypeToString(PortType type)
{
  switch (type) {
    case PortType::None:
      return QStringLiteral("None");
    case PortType::ImageData:
      return QStringLiteral("ImageData");
    case PortType::TiltSeries:
      return QStringLiteral("TiltSeries");
    case PortType::Volume:
      return QStringLiteral("Volume");
    case PortType::Image:
      return QStringLiteral("Image");
    case PortType::Scalar:
      return QStringLiteral("Scalar");
    case PortType::Array:
      return QStringLiteral("Array");
    case PortType::Table:
      return QStringLiteral("Table");
    case PortType::Molecule:
      return QStringLiteral("Molecule");
  }
  return QStringLiteral("None");
}

inline PortType portTypeFromString(const QString& s)
{
  if (s == QLatin1String("ImageData"))
    return PortType::ImageData;
  if (s == QLatin1String("TiltSeries"))
    return PortType::TiltSeries;
  if (s == QLatin1String("Volume"))
    return PortType::Volume;
  if (s == QLatin1String("Image"))
    return PortType::Image;
  if (s == QLatin1String("Scalar"))
    return PortType::Scalar;
  if (s == QLatin1String("Array"))
    return PortType::Array;
  if (s == QLatin1String("Table"))
    return PortType::Table;
  if (s == QLatin1String("Molecule"))
    return PortType::Molecule;
  return PortType::None;
}

} // namespace pipeline
} // namespace tomviz

#endif
