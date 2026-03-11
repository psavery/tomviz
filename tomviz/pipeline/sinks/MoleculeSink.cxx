/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "MoleculeSink.h"

#include <vtkActor.h>
#include <vtkMolecule.h>
#include <vtkMoleculeMapper.h>
#include <vtkPVRenderView.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

namespace tomviz {
namespace pipeline {

MoleculeSink::MoleculeSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("molecule", PortType::Molecule);
  setLabel("Molecule");

  m_actor->SetMapper(m_moleculeMapper);
}

MoleculeSink::~MoleculeSink()
{
  finalize();
}

bool MoleculeSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  renderView()->GetRenderer()->AddActor(m_actor);
  return true;
}

bool MoleculeSink::finalize()
{
  if (renderView()) {
    renderView()->GetRenderer()->RemoveActor(m_actor);
  }
  return LegacyModuleSink::finalize();
}

bool MoleculeSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "molecule")) {
    return false;
  }

  // Extract vtkMolecule from PortData and connect to mapper
  try {
    auto molecule =
      inputs["molecule"].value<vtkSmartPointer<vtkMolecule>>();
    if (molecule) {
      m_moleculeMapper->SetInputData(molecule);
    }
  } catch (const std::bad_any_cast&) {
    // PortData doesn't contain vtkSmartPointer<vtkMolecule> yet
  }

  m_actor->SetVisibility(visibility() ? 1 : 0);

  if (renderView()) {
    renderView()->Update();
  }

  emit renderNeeded();
  return true;
}

double MoleculeSink::ballRadius() const
{
  return m_moleculeMapper->GetAtomicRadiusScaleFactor();
}

void MoleculeSink::setBallRadius(double radius)
{
  m_moleculeMapper->SetAtomicRadiusScaleFactor(radius);
  emit renderNeeded();
}

double MoleculeSink::bondRadius() const
{
  return m_moleculeMapper->GetBondRadius();
}

void MoleculeSink::setBondRadius(double radius)
{
  m_moleculeMapper->SetBondRadius(radius);
  emit renderNeeded();
}

QJsonObject MoleculeSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  json["ballRadius"] = m_moleculeMapper->GetAtomicRadiusScaleFactor();
  json["bondRadius"] = m_moleculeMapper->GetBondRadius();
  return json;
}

bool MoleculeSink::deserialize(const QJsonObject& json)
{
  if (!LegacyModuleSink::deserialize(json)) {
    return false;
  }
  if (json.contains("ballRadius")) {
    setBallRadius(json["ballRadius"].toDouble());
  }
  if (json.contains("bondRadius")) {
    setBondRadius(json["bondRadius"].toDouble());
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
