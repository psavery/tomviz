/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(_wrapping, m)
{
  m.doc() = "tomviz wrapped classes";
}
