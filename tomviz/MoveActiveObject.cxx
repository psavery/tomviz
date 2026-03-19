/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "MoveActiveObject.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "Utilities.h"
#include "pipeline/Pipeline.h"
#include "pipeline/PipelineExecutor.h"

#include <pqView.h>

#include <vtkActor.h>
#include <vtkBoundingBox.h>
#include <vtkBoxWidget2.h>
#include <vtkCommand.h>
#include <vtkCustomBoxRepresentation.h>
#include <vtkDataObject.h>
#include <vtkEvent.h>
#include <vtkEventQtSlotConnect.h>
#include <vtkImageData.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSMSourceProxy.h>
#include <vtkSMViewProxy.h>
#include <vtkTransform.h>
#include <vtkTrivialProducer.h>
#include <vtkWidgetEvent.h>
#include <vtkWidgetEventTranslator.h>

#include <QScopedValueRollback>

namespace tomviz {

MoveActiveObject::MoveActiveObject(QObject* p) : Superclass(p)
{
  ActiveObjects& activeObjs = ActiveObjects::instance();
  this->BoxRep->SetPlaceFactor(1.0);
  this->BoxRep->HandlesOn();
  this->BoxRep->SetHandleSize(10);

  this->BoxWidget->SetRepresentation(this->BoxRep.GetPointer());
  this->BoxWidget->SetPriority(1);

  // TODO: dataSourceActivated no longer exists. Using activeNodeChanged
  // as a proxy, but the handler needs rework since it receives a Node*
  // instead of a DataSource*.
  this->connect(&activeObjs, &ActiveObjects::activeNodeChanged,
                this, &MoveActiveObject::onActiveNodeChanged);
  this->connect(&activeObjs,
                QOverload<vtkSMViewProxy*>::of(&ActiveObjects::viewChanged),
                this, &MoveActiveObject::onViewChanged);

  // TODO: interactionDataSourceFixed signal no longer exists on ActiveObjects.
  // The fixed-interaction-data-source concept needs to be reworked for the
  // new pipeline architecture.

  this->connect(&activeObjs, &ActiveObjects::translationStateChanged, this,
                &MoveActiveObject::updateInteractionStates);

  this->connect(&activeObjs, &ActiveObjects::rotationStateChanged, this,
                &MoveActiveObject::updateInteractionStates);

  this->connect(&activeObjs, &ActiveObjects::scalingStateChanged, this,
                &MoveActiveObject::updateInteractionStates);

  this->EventLink->Connect(this->BoxWidget.GetPointer(),
                           vtkCommand::InteractionEvent, this,
                           SLOT(interactionEnd(vtkObject*)));
  this->EventLink->Connect(this->BoxWidget.GetPointer(),
                           vtkCommand::EndInteractionEvent, this,
                           SLOT(interactionEnd(vtkObject*)));
}

MoveActiveObject::~MoveActiveObject() {}

void MoveActiveObject::onViewChanged(vtkSMViewProxy* view)
{
  pqView* pqview = tomviz::convert<pqView*>(view);
  if (this->View == pqview) {
    return;
  }

  if (view && view->GetRenderWindow()) {
    this->BoxWidget->SetInteractor(view->GetRenderWindow()->GetInteractor());
  } else {
    this->BoxWidget->SetInteractor(nullptr);
    this->BoxWidget->EnabledOff();
  }

  // Render the old view and then the new one
  render();
  this->View = pqview;
  updateInteractionStates();
}

void MoveActiveObject::interactionEnd(vtkObject* caller)
{
  Q_UNUSED(caller);

  vtkNew<vtkTransform> t;
  this->BoxRep->GetTransform(t);

  QScopedValueRollback<bool> rollback(this->Interacting, true);
  auto ds = this->currentDataSource;
  ds->setDisplayPosition(t->GetPosition());
  ds->setDisplayOrientation(t->GetOrientation());
  ds->setSpacing(t->GetScale());
  render();
}

void MoveActiveObject::onActiveNodeChanged(pipeline::Node* /*node*/)
{
  // TODO: The old dataSourceActivated took a DataSource* and connected to
  // its signals (displayPositionChanged, displayOrientationChanged,
  // dataPropertiesChanged). The new pipeline uses Node* which doesn't
  // have the same signals. This needs rework to extract a DataSource
  // from the node (if applicable) to manage widget interaction.

  // TODO: fixedInteractionDataSource() no longer exists on ActiveObjects.
  // The fixed-interaction concept needs rework for the new pipeline.

  // For now, clear the current data source and disable the widget.
  if (this->currentDataSource) {
    this->disconnect(this->currentDataSource);
  }

  this->currentDataSource = nullptr;
  updateInteractionStates();
}

void MoveActiveObject::resetWidgetPlacement()
{
  auto source = this->currentDataSource;
  if (!source) {
    return;
  }

  // Use the extents for the widget bounds. Need to convert to double.
  double bounds[6];
  auto* extent = source->imageData()->GetExtent();
  for (int i = 0; i < 6; ++i) {
    bounds[i] = static_cast<double>(extent[i]);
  }

  this->BoxRep->PlaceWidget(bounds);
  this->updateWidgetTransform();
}

void MoveActiveObject::onDataPropertiesChanged()
{
  if (this->Interacting) {
    return;
  }

  updateWidgetTransform();
  render();
}

void MoveActiveObject::updateWidgetTransform()
{
  auto ds = this->currentDataSource;
  if (!ds) {
    return;
  }

  vtkNew<vtkTransform> t;
  t->Identity();

  // Translate
  t->Translate(ds->displayPosition());

  // Rotate
  // Do as vtkProp3D does: rotate Z first, then X, then Y.
  auto* orientation = ds->displayOrientation();
  t->RotateZ(orientation[2]);
  t->RotateX(orientation[0]);
  t->RotateY(orientation[1]);

  // Scale
  t->Scale(ds->getSpacing());

  this->BoxRep->SetTransform(t);
}

void MoveActiveObject::onDataPositionChanged(double, double, double)
{
  this->onDataPropertiesChanged();
}

void MoveActiveObject::onDataOrientationChanged(double, double, double)
{
  this->onDataPropertiesChanged();
}

// TODO: onInteractionDataSourceFixed was connected to the removed
// interactionDataSourceFixed signal. This method is no longer called.
// Kept as dead code until the interaction-fixing concept is reworked.

void MoveActiveObject::updateInteractionStates()
{
  auto* ds = this->currentDataSource;
  auto* widget = this->BoxWidget.Get();
  auto* rep = this->BoxRep.Get();

  if (!ds || !this->View || activePipelineIsRunning()) {
    widget->EnabledOff();
    render();
    return;
  }

  auto& activeObjects = ActiveObjects::instance();
  bool translate = activeObjects.translationEnabled();
  bool rotate = activeObjects.rotationEnabled();
  bool scale = activeObjects.scalingEnabled();

  bool anyTransforms = translate || rotate || scale;

  widget->SetEnabled(anyTransforms);
  if (!anyTransforms) {
    render();
    return;
  }

  widget->SetTranslationEnabled(translate);
  widget->SetRotationEnabled(rotate);
  widget->SetScalingEnabled(scale);
  widget->SetMoveFacesEnabled(scale);

  for (int i = 0; i < 6; ++i) {
    rep->GetHandle()[i]->SetVisibility(scale);
  }

  rep->GetHandle()[6]->SetVisibility(translate);

  render();
}

void MoveActiveObject::render()
{
  if (!this->View) {
    return;
  }

  this->View->render();
}

bool MoveActiveObject::activePipelineIsRunning() const
{
  auto* pipeline = ActiveObjects::instance().pipeline();
  if (!pipeline) {
    return false;
  }
  auto* exec = pipeline->executor();
  return exec && exec->isRunning();
}

} // namespace tomviz
