/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "VolumeOutputPort.h"

#include "data/VolumeData.h"

#include <vtkImageData.h>
#include <vtkNew.h>

#include <QMetaObject>
#include <QThread>

namespace tomviz {
namespace pipeline {

VolumeOutputPort::VolumeOutputPort(const QString& name, PortType type,
                                   QObject* parent)
  : OutputPort(name, type, parent)
{}

void VolumeOutputPort::setIntermediateData(const PortData& incoming)
{
  auto apply = [this, incoming]() {
    auto fresh = incoming.value<VolumeDataPtr>();
    if (!fresh || !fresh->imageData()) {
      return;
    }

    if (hasData() && isVolumeType(data().type())) {
      auto existing = data().value<VolumeDataPtr>();
      if (existing && existing->imageData()) {
        existing->imageData()->DeepCopy(fresh->imageData());
        existing->imageData()->Modified();
        emit dataChanged();
      }
    } else {
      // First intermediate update — deep-copy into a new VolumeData
      // so the port owns its own vtkImageData (independent of Python).
      vtkNew<vtkImageData> copy;
      copy->DeepCopy(fresh->imageData());
      auto vol = std::make_shared<VolumeData>(copy.Get());
      setData(PortData(std::any(vol), type()));
    }

    emit intermediateDataApplied();
  };

  if (QThread::currentThread() == thread()) {
    apply();
  } else {
    QMetaObject::invokeMethod(this, apply, Qt::BlockingQueuedConnection);
  }
}

} // namespace pipeline
} // namespace tomviz
