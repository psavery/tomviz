/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "AddExpressionReaction.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "EditOperatorDialog.h"
#include "OperatorPython.h"
#include "PipelineManager.h"
#include "Utilities.h"

namespace tomviz {

AddExpressionReaction::AddExpressionReaction(QAction* parentObject)
  : Reaction(parentObject)
{
}

OperatorPython* AddExpressionReaction::addExpression(DataSource* source)
{
  source = source ? source : ActiveObjects::instance().activeParentDataSource();
  if (!source) {
    return nullptr;
  }

  QString script = getDefaultExpression(source);

  OperatorPython* opPython = new OperatorPython(source);
  opPython->setScript(script);
  opPython->setLabel("Transform Data");
  opPython->setHelpUrl("operator");

  // Create a non-modal dialog, delete it once it has been closed.
  auto dialog =
    new EditOperatorDialog(opPython, source, true, tomviz::mainWidget());
  dialog->setAttribute(Qt::WA_DeleteOnClose, true);
  dialog->show();
  connect(opPython, SIGNAL(destroyed()), dialog, SLOT(reject()));
  return nullptr;
}

QString AddExpressionReaction::getDefaultExpression(DataSource* source)
{
  QString actionString = parentAction()->text();
  if (actionString == "Custom ITK Transform") {
    return readInPythonScript("DefaultITKTransform");
  } else {
    // Build the default script for the python operator
    // This was done in the Dialog's UI file, but since it needs to change
    // based on the type of dataset, do it here
    return QString("# Transform entry point, do not change function name.\n"
                   "def transform(dataset):\n"
                   "    \"\"\"Define this method for Python operators that \n"
                   "    transform the input array\"\"\"\n"
                   "\n"
                   "    import numpy as np\n"
                   "\n"
                   "%1"
                   "    # Get the current volume as a numpy array.\n"
                   "    array = dataset.active_scalars\n"
                   "\n"
                   "    # This is where you operate on your data, here we "
                   "square root it.\n"
                   "    result = np.sqrt(array)\n"
                   "\n"
                   "    # This is where the transformed data is set, it will "
                   "display in tomviz.\n"
                   "    dataset.active_scalars = result\n")
      .arg(source->type() == DataSource::Volume
             ? ""
             : "    # Get the tilt angles array as a numpy array.\n"
               "    # You may also set tilt angles with dataset.tilt_angles\n"
               "    tilt_angles = dataset.tilt_angles\n"
               "\n");
  }
}

void AddExpressionReaction::updateEnableState()
{
  // This is currently compatible in all execution environments
  auto compatibleExecutionMode = true;

  parentAction()->setEnabled(ActiveObjects::instance().activeDataSource() !=
                               nullptr &&
                             compatibleExecutionMode);
}

} // namespace tomviz
