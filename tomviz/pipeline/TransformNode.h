/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineTransformNode_h
#define tomvizPipelineTransformNode_h

#include "Node.h"
#include "PortData.h"

#include <QJsonObject>
#include <QMap>
#include <QString>

class QWidget;

namespace tomviz {
namespace pipeline {

class EditTransformWidget;

class TransformNode : public Node
{
  Q_OBJECT

public:
  TransformNode(QObject* parent = nullptr);
  ~TransformNode() override = default;

  QIcon icon() const override;

  InputPort* addInput(const QString& name, PortTypes acceptedTypes);
  OutputPort* addOutput(const QString& name, PortType type);

  bool execute() override;

  /// Whether this transform provides a properties widget.
  virtual bool hasPropertiesWidget() const;

  /// Whether the properties widget needs current input port data to display.
  virtual bool propertiesWidgetNeedsInput() const;

  /// Create the properties widget. Caller owns the returned widget.
  virtual EditTransformWidget* createPropertiesWidget(QWidget* parent);

  /// Serialize this transform's persistent state to JSON in a shape
  /// compatible with legacy .tvsm operator entries. Base-class
  /// implementation writes "label". Subclasses call the base first
  /// and add their own fields.
  virtual QJsonObject serialize() const;

  /// Apply JSON produced by serialize() (or by a legacy
  /// *Operator::serialize()) to this transform. Returns false on
  /// unrecoverable errors. Base-class implementation reads "label".
  virtual bool deserialize(const QJsonObject& json);

signals:
  /// Emitted when the user applies new parameter values via the properties
  /// widget. The node has already been marked stale; connect this to
  /// pipeline re-execution.
  void parametersApplied();

protected:
  virtual QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) = 0;
};

} // namespace pipeline
} // namespace tomviz

#endif
