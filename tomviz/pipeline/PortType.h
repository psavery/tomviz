/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelinePortType_h
#define tomvizPipelinePortType_h

#include <QFlags>

namespace tomviz {
namespace pipeline {

enum class PortType
{
  None = 0,
  Volume = 1,
  Image = 2,
  Scalar = 4,
  Array = 8,
  Table = 16,
  Molecule = 32
};

Q_DECLARE_FLAGS(PortTypes, PortType)
Q_DECLARE_OPERATORS_FOR_FLAGS(PortTypes)

} // namespace pipeline
} // namespace tomviz

#endif
