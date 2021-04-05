/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef vtkImageRotationsData_h
#define vtkImageRotationsData_h

#include <vtkImageData.h>

#include <vector>

class vtkImageRotationsData : public vtkImageData
{
public:
  static vtkImageRotationsData* New();
  vtkTypeMacro(vtkImageRotationsData, vtkImageData)

    void SetRotations(const std::vector<double>& rotations)
  {
    m_rotations = rotations;
  }

  std::vector<double> GetRotations() const { return m_rotations; }

private:
  std::vector<double> m_rotations;
};

#endif // vtkImageRotationsData_h
