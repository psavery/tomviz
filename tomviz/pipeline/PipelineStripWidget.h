/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineStripWidget_h
#define tomvizPipelineStripWidget_h

#include "tomviz_pipeline_export.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QWidget>

namespace tomviz {
namespace pipeline {

class Link;
class Node;
class OutputPort;
class Pipeline;

struct LayoutItem
{
  enum Type
  {
    NodeCard,
    PortCard
  };

  Type type = NodeCard;
  Node* node = nullptr;
  OutputPort* port = nullptr; // set only for PortCard
  QRect rect;
};

class TOMVIZ_PIPELINE_EXPORT PipelineStripWidget : public QWidget
{
  Q_OBJECT

public:
  explicit PipelineStripWidget(QWidget* parent = nullptr);
  ~PipelineStripWidget() override = default;

  void setPipeline(Pipeline* pipeline);
  Pipeline* pipeline() const;

  Node* selectedNode() const;
  OutputPort* selectedPort() const;

  bool isExpanded(Node* node) const;
  void setExpanded(Node* node, bool expanded);

  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

signals:
  void nodeSelected(Node* node);
  void portSelected(OutputPort* port);
  void nodeDoubleClicked(Node* node);
  void contextMenuRequested(Node* node, QPoint globalPos);

public slots:
  void rebuildLayout();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

private:
  void connectPipeline();
  void disconnectPipeline();
  void selectItem(int index);
  int hitTest(const QPoint& pos) const;
  int selectedIndex() const;

  // Painting helpers
  void paintNodeCard(QPainter& painter, const LayoutItem& item,
                     bool selected, bool hovered);
  void paintPortCard(QPainter& painter, const LayoutItem& item,
                     bool selected, bool hovered);
  void paintConnections(QPainter& painter);
  void paintInputDots(QPainter& painter, const LayoutItem& item);
  void paintOutputDots(QPainter& painter, const LayoutItem& item);
  void paintPortCardDot(QPainter& painter, const LayoutItem& item);
  QPoint inputDotPos(Node* node, int portIndex, const QRect& nodeRect) const;
  QPoint outputDotPos(Node* node, int portIndex, const QRect& nodeRect) const;
  QColor badgeColor(Node* node) const;
  QColor portTypeColor(OutputPort* port) const;
  QString stateText(Node* node) const;
  QString badgeText(Node* node) const;

  Pipeline* m_pipeline = nullptr;
  QList<LayoutItem> m_layout;
  int m_selectedIndex = -1;
  OutputPort* m_selectedPort = nullptr; // selected output dot (collapsed nodes)
  QSet<Node*> m_expandedNodes;
  QTimer m_spinnerTimer;
  int m_spinnerAngle = 0;

  // Layout constants
  static constexpr int GutterWidth = 24;
  static constexpr int NodeCardHeight = 28;
  static constexpr int PortCardHeight = 22;
  static constexpr int CardSpacing = 4;
  static constexpr int ConnectionSpacing = 8;
  static constexpr int DirectConnectionSpacing = 12;
  static constexpr int PortIndent = 8;
  static constexpr int CardRadius = 4;
  static constexpr int BadgeSize = 12;
  static constexpr int DotRadius = 4;
  static constexpr int DotSpacing = 10; // center-to-center between adjacent dots
  static constexpr int DotMargin = 6;   // left margin for dots on node cards
  static constexpr int LaneSpacing = 4;              // spacing between parallel lines
  static constexpr int DotClearance = DotRadius * 2; // initial offset from dot before first lane
  static constexpr int Padding = 4;
};

} // namespace pipeline
} // namespace tomviz

#endif
