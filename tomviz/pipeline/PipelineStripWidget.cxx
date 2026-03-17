/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PipelineStripWidget.h"

#include "InputPort.h"
#include "Link.h"
#include "Node.h"
#include "OutputPort.h"
#include "Pipeline.h"
#include "PortType.h"
#include "SinkNode.h"
#include "SourceNode.h"
#include "TransformNode.h"
#include "sinks/LegacyModuleSink.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QStyle>
#include <QStyleOption>

namespace tomviz {
namespace pipeline {

PipelineStripWidget::PipelineStripWidget(QWidget* parent) : QWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  setMouseTracking(true);

  m_spinnerTimer.setInterval(50);
  connect(&m_spinnerTimer, &QTimer::timeout, this, [this]() {
    m_spinnerAngle = (m_spinnerAngle + 30) % 360;
    update();
  });
}

void PipelineStripWidget::setPipeline(Pipeline* pipeline)
{
  if (m_pipeline == pipeline) {
    return;
  }
  disconnectPipeline();
  m_pipeline = pipeline;
  m_selectedIndex = -1;
  m_selectedPort = nullptr;
  m_selectedLink = nullptr;
  m_hoveredLink = nullptr;
  m_expandedNodes.clear();
  connectPipeline();
  rebuildLayout();
  update();
}

Pipeline* PipelineStripWidget::pipeline() const
{
  return m_pipeline;
}

void PipelineStripWidget::setSortOrder(SortOrder order)
{
  if (m_sortOrder != order) {
    m_sortOrder = order;
    rebuildLayout();
    update();
  }
}

SortOrder PipelineStripWidget::sortOrder() const
{
  return m_sortOrder;
}

Node* PipelineStripWidget::selectedNode() const
{
  if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
    return m_layout[m_selectedIndex].node;
  }
  return nullptr;
}

OutputPort* PipelineStripWidget::selectedPort() const
{
  if (m_selectedPort) {
    return m_selectedPort;
  }
  if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
    auto& item = m_layout[m_selectedIndex];
    if (item.type == LayoutItem::PortCard) {
      return item.port;
    }
  }
  return nullptr;
}

Link* PipelineStripWidget::selectedLink() const
{
  return m_selectedLink;
}

void PipelineStripWidget::setNodeMenuProvider(NodeMenuProvider provider)
{
  m_nodeMenuProvider = std::move(provider);
}

void PipelineStripWidget::setPortMenuProvider(PortMenuProvider provider)
{
  m_portMenuProvider = std::move(provider);
}

void PipelineStripWidget::setLinkMenuProvider(LinkMenuProvider provider)
{
  m_linkMenuProvider = std::move(provider);
}

void PipelineStripWidget::setLinkValidator(LinkValidator validator)
{
  m_linkValidator = std::move(validator);
}

bool PipelineStripWidget::isExpanded(Node* node) const
{
  return m_expandedNodes.contains(node);
}

void PipelineStripWidget::setExpanded(Node* node, bool expanded)
{
  if (expanded) {
    m_expandedNodes.insert(node);
  } else {
    m_expandedNodes.remove(node);
  }
  rebuildLayout();
  update();
}

QSize PipelineStripWidget::minimumSizeHint() const
{
  return QSize(100, 50);
}

QSize PipelineStripWidget::sizeHint() const
{
  if (m_layout.isEmpty()) {
    return QSize(200, 50);
  }
  auto& last = m_layout.last();
  int h = last.rect.bottom() + Padding + CardSpacing;
  return QSize(200, h);
}

void PipelineStripWidget::rebuildLayout()
{
  // Remember what was selected so we can restore after rebuilding
  Node* selectedNode = nullptr;
  OutputPort* selectedPort = nullptr;
  if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
    auto& sel = m_layout[m_selectedIndex];
    selectedNode = sel.node;
    selectedPort = sel.port; // non-null only for PortCard
  }
  // Also consider a selected output dot
  if (m_selectedPort) {
    selectedPort = m_selectedPort;
  }

  m_layout.clear();
  m_linkGeometries.clear();
  m_selectedIndex = -1;
  m_selectedPort = nullptr;

  if (!m_pipeline) {
    updateGeometry();
    return;
  }

  auto sorted = m_pipeline->topologicalSort({}, m_sortOrder);
  int y = Padding;
  int cardWidth = qMax(width() - GutterWidth - Padding * 2, 80);

  bool anyExecuting = false;

  Node* prevNode = nullptr;
  bool prevCollapsed = false;

  for (int ni = 0; ni < sorted.size(); ++ni) {
    auto* node = sorted[ni];
    auto* nextNode = (ni + 1 < sorted.size()) ? sorted[ni + 1] : nullptr;

    // Extra top margin for input ports that are NOT direct connections.
    // A "direct" input is one connected from the previous adjacent node
    // whose output ports are collapsed — those get straight lines.
    int maxNonDirectInputIdx = -1;
    auto inputs = node->inputPorts();
    for (int i = 0; i < inputs.size(); ++i) {
      if (!inputs[i]->link()) {
        continue;
      }
      Node* src = inputs[i]->link()->from()->node();
      bool isDirect = prevCollapsed && src == prevNode;
      if (!isDirect) {
        maxNonDirectInputIdx = i;
      }
    }
    if (maxNonDirectInputIdx >= 0) {
      y += DotClearance + maxNonDirectInputIdx * LaneSpacing;
    }

    // Port sub-cards (shown only when node is expanded)
    auto outputs = node->outputPorts();
    bool showPorts =
      !outputs.isEmpty() && m_expandedNodes.contains(node);

    // Compute node card height: base height + port cards inside when expanded
    int nodeHeight = NodeCardHeight;
    if (showPorts) {
      nodeHeight += CardSpacing; // top padding below header
      nodeHeight += outputs.size() * PortCardHeight;
      nodeHeight += (outputs.size() - 1) * CardSpacing;
      nodeHeight += PortIndent; // bottom padding
    }

    // Compute indentation level based on node type and connections.
    // - All inputs disconnected: indent 0
    // - Source nodes: always indent 0
    // - Transform nodes with connections: indent 1
    // - Sink nodes with connections: indent 2 if connected to at least one
    //   transform, otherwise indent 1
    int indentLevel = 0;
    bool hasConnectedInput = false;
    for (auto* inp : inputs) {
      if (inp->link()) {
        hasConnectedInput = true;
        break;
      }
    }
    if (hasConnectedInput && !qobject_cast<SourceNode*>(node)) {
      if (qobject_cast<SinkNode*>(node)) {
        bool connectedToTransform = false;
        for (auto* inp : inputs) {
          if (inp->link() &&
              qobject_cast<TransformNode*>(inp->link()->from()->node())) {
            connectedToTransform = true;
            break;
          }
        }
        indentLevel = connectedToTransform ? 2 : 1;
      } else {
        // TransformNode (or any non-source, non-sink)
        indentLevel = 1;
      }
    }
    int indent = indentLevel * IndentWidth;

    // Node card
    LayoutItem nodeItem;
    nodeItem.type = LayoutItem::NodeCard;
    nodeItem.node = node;
    nodeItem.rect =
      QRect(GutterWidth + Padding + indent, y, cardWidth - indent, nodeHeight);
    m_layout.append(nodeItem);

    if (showPorts) {
      int portY = y + NodeCardHeight + CardSpacing;
      for (auto* port : outputs) {
        LayoutItem portItem;
        portItem.type = LayoutItem::PortCard;
        portItem.node = node;
        portItem.port = port;
        portItem.rect =
          QRect(GutterWidth + Padding + indent + PortIndent, portY,
                cardWidth - indent - 2 * PortIndent, PortCardHeight);
        m_layout.append(portItem);
        portY += PortCardHeight + CardSpacing;
      }
    }
    y += nodeHeight + CardSpacing;

    // Extra bottom margin for collapsed outputs with non-direct links.
    // Direct outputs (all links go to the next adjacent node) get
    // straight lines and don't need gutter clearance.
    if (!showPorts) {
      int maxNonDirectOutputIdx = -1;
      for (int i = 0; i < outputs.size(); ++i) {
        if (outputs[i]->links().isEmpty()) {
          continue;
        }
        bool allDirect = (nextNode != nullptr);
        if (allDirect) {
          for (auto* link : outputs[i]->links()) {
            if (link->to()->node() != nextNode) {
              allDirect = false;
              break;
            }
          }
        }
        if (!allDirect) {
          maxNonDirectOutputIdx = i;
        }
      }
      if (maxNonDirectOutputIdx >= 0) {
        y += DotClearance + maxNonDirectOutputIdx * LaneSpacing;
      } else if (!outputs.isEmpty()) {
        // All outputs are direct — use wider spacing for the straight
        // lines, but only if the next node also has no non-direct inputs
        // (otherwise the approach margin already provides enough space).
        bool nextHasNonDirect = false;
        if (nextNode) {
          auto nextInputs = nextNode->inputPorts();
          for (auto* inp : nextInputs) {
            if (inp->link() && inp->link()->from()->node() != node) {
              nextHasNonDirect = true;
              break;
            }
          }
        }
        if (!nextHasNonDirect) {
          y += DirectConnectionSpacing - ConnectionSpacing;
        }
      }
    }

    y += ConnectionSpacing;

    prevNode = node;
    prevCollapsed = !showPorts && !outputs.isEmpty();
  }

  Q_UNUSED(anyExecuting);

  // Restore selection by matching the previously selected node/port
  if (selectedPort) {
    // Try to find as a port card first
    bool found = false;
    for (int i = 0; i < m_layout.size(); ++i) {
      if (m_layout[i].port == selectedPort) {
        m_selectedIndex = i;
        found = true;
        break;
      }
    }
    // If not a port card (collapsed), restore as selected dot
    if (!found) {
      m_selectedPort = selectedPort;
    }
  } else if (selectedNode) {
    for (int i = 0; i < m_layout.size(); ++i) {
      if (m_layout[i].type == LayoutItem::NodeCard &&
          m_layout[i].node == selectedNode) {
        m_selectedIndex = i;
        break;
      }
    }
  }

  computeLinkGeometries();
  updateGeometry();
}

void PipelineStripWidget::connectPipeline()
{
  if (!m_pipeline) {
    return;
  }

  auto connectNode = [this](Node* node) {
    connect(node, &Node::stateChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::labelChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::breakpointChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::executionStarted, this, [this]() {
      if (!m_spinnerTimer.isActive()) {
        m_spinnerTimer.start();
      }
    });
    connect(node, &Node::executionFinished, this, [this](bool) {
      m_spinnerTimer.stop();
      update();
    });
    auto* sink = qobject_cast<LegacyModuleSink*>(node);
    if (sink) {
      connect(sink, &LegacyModuleSink::visibilityChanged, this,
              QOverload<>::of(&QWidget::update));
    }
  };

  connect(m_pipeline, &Pipeline::nodeAdded, this, [this, connectNode](Node* node) {
    connectNode(node);
    rebuildLayout();
    update();
  });

  connect(m_pipeline, &Pipeline::nodeRemoved, this, [this](Node* node) {
    node->disconnect(this);
    if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size() &&
        m_layout[m_selectedIndex].node == node) {
      m_selectedIndex = -1;
    }
    m_expandedNodes.remove(node);
    rebuildLayout();
    update();
  });

  connect(m_pipeline, &Pipeline::linkCreated, this, [this](Link* link) {
    connect(link, &Link::validityChanged, this, [this]() {
      rebuildLayout();
      update();
    });
    rebuildLayout();
    update();
  });

  connect(m_pipeline, &Pipeline::linkRemoved, this, [this](Link*) {
    rebuildLayout();
    update();
  });

  // Connect existing nodes
  for (auto* node : m_pipeline->nodes()) {
    connectNode(node);
  }

  // Connect existing links for validity change notifications
  for (auto* link : m_pipeline->links()) {
    connect(link, &Link::validityChanged, this, [this]() {
      rebuildLayout();
      update();
    });
  }
}

void PipelineStripWidget::disconnectPipeline()
{
  if (!m_pipeline) {
    return;
  }
  m_pipeline->disconnect(this);
  for (auto* node : m_pipeline->nodes()) {
    node->disconnect(this);
  }
  m_spinnerTimer.stop();
}

void PipelineStripWidget::selectItem(int index)
{
  if (index == m_selectedIndex && !m_selectedPort && !m_selectedLink) {
    return;
  }
  m_selectedIndex = index;
  m_selectedPort = nullptr;
  m_selectedLink = nullptr;
  update();

  if (index >= 0 && index < m_layout.size()) {
    auto& item = m_layout[index];
    if (item.type == LayoutItem::PortCard) {
      emit portSelected(item.port);
    } else {
      emit nodeSelected(item.node);
    }
  } else {
    emit nodeSelected(nullptr);
  }
}

int PipelineStripWidget::hitTest(const QPoint& pos) const
{
  // Search in reverse so port cards (which are inside their parent node card's
  // rect) are found before the enclosing node card.
  for (int i = m_layout.size() - 1; i >= 0; --i) {
    if (m_layout[i].rect.contains(pos)) {
      return i;
    }
  }
  return -1;
}

int PipelineStripWidget::selectedIndex() const
{
  return m_selectedIndex;
}

void PipelineStripWidget::selectLink(Link* link)
{
  if (link == m_selectedLink && m_selectedIndex < 0 && !m_selectedPort) {
    return;
  }
  m_selectedIndex = -1;
  m_selectedPort = nullptr;
  m_selectedLink = link;
  update();
  emit linkSelected(link);
}

Link* PipelineStripWidget::linkHitTest(const QPoint& pos) const
{
  QPainterPathStroker stroker;
  stroker.setWidth(8.0);
  for (auto& lg : m_linkGeometries) {
    QPainterPath hitPath = stroker.createStroke(lg.path);
    if (hitPath.contains(pos)) {
      return lg.link;
    }
  }
  return nullptr;
}

// --- Painting ---

void PipelineStripWidget::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), Qt::white);

  // Paint connections first (behind cards)
  paintConnections(painter);

  // Paint cards
  for (int i = 0; i < m_layout.size(); ++i) {
    bool selected = (i == m_selectedIndex);
    bool hovered = (i == m_hoveredIndex);
    auto& item = m_layout[i];

    if (item.type == LayoutItem::NodeCard) {
      paintNodeCard(painter, item, selected, hovered);
      paintInputDots(painter, item);
      paintOutputDots(painter, item);
    } else {
      paintPortCard(painter, item, selected, hovered);
      paintPortCardDot(painter, item);
    }
  }

  // Paint pending link on top of everything during drag
  paintPendingLink(painter);
}

void PipelineStripWidget::paintNodeCard(QPainter& painter,
                                        const LayoutItem& item, bool selected,
                                        bool hovered)
{
  auto r = item.rect;
  auto* node = item.node;

  // Node-type colored border; fill only when selected
  QColor fg = selected ? palette().highlightedText().color()
                       : palette().buttonText().color();
  QColor borderColor = badgeColor(node);

  QPainterPath path;
  path.addRoundedRect(QRectF(r), CardRadius, CardRadius);
  if (selected) {
    // Only fill the header row, not the full expanded area
    QRectF headerRect(r.left(), r.top(), r.width(), NodeCardHeight);
    QPainterPath headerPath;
    headerPath.addRoundedRect(headerRect, CardRadius, CardRadius);
    painter.fillPath(headerPath, palette().highlight());
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(borderColor, 1.5));
  painter.drawPath(path);

  int x = r.left() + 6;
  int cy = r.top() + NodeCardHeight / 2; // header row center
  int headerHeight = NodeCardHeight;

  // Node icon
  QIcon nodeIcon = node->icon();
  int iconPaintSize = 14;
  QRect iconRect(x + (BadgeSize - iconPaintSize) / 2,
                 cy - iconPaintSize / 2,
                 iconPaintSize, iconPaintSize);
  nodeIcon.paint(&painter, iconRect);

  x += BadgeSize + 6;

  // Label
  QFont labelFont = font();
  labelFont.setPixelSize(12);
  painter.setFont(labelFont);
  painter.setPen(fg);
  QFontMetrics fm(labelFont);

  // Right side layout: [breakpoint] [state] [expand]
  // Expand toggle is rightmost (only for nodes with outputs), but state
  // icon is always at the same X so it stays aligned across all nodes.
  auto outputs = node->outputPorts();
  int toggleX = r.right() - HeaderExpandWidth - HeaderRightPad;
  int stateX = toggleX - HeaderIconSize;
  int bpX = stateX - HeaderIconSize;

  int labelWidth = bpX - x - 2;
  QString elidedLabel =
    fm.elidedText(node->label(), Qt::ElideRight, labelWidth);
  painter.drawText(QRect(x, r.top(), labelWidth, headerHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, elidedLabel);

  // Breakpoint indicator / hover hint
  QRect bpRect = breakpointRect(r);
  QIcon bpIcon(QStringLiteral(":/pipeline/breakpoint.png"));
  if (node->hasBreakpoint()) {
    bpIcon.paint(&painter, bpRect);
  } else if (hovered) {
    painter.setOpacity(0.25);
    bpIcon.paint(&painter, bpRect);
    painter.setOpacity(1.0);
  }

  // State indicator icon
  int stateY = r.top() + (headerHeight - HeaderIconSize) / 2;
  QRect stateRect(stateX, stateY, HeaderIconSize, HeaderIconSize);

  if (m_spinnerTimer.isActive() && node->state() == NodeState::Stale) {
    // Draw rotating spinner icon
    QPixmap spinner(QStringLiteral(":/pipeline/spinner.png"));
    painter.save();
    painter.translate(stateRect.center());
    painter.rotate(m_spinnerAngle);
    painter.drawPixmap(-HeaderIconSize / 2, -HeaderIconSize / 2,
                       HeaderIconSize, HeaderIconSize, spinner);
    painter.restore();
  } else {
    QIcon icon = stateIcon(node);
    icon.paint(&painter, stateRect);
  }

  // Expand toggle for nodes with output ports (uses native style arrows)
  if (!outputs.isEmpty()) {
    bool expanded = m_expandedNodes.contains(node);
    QStyleOption opt;
    opt.initFrom(this);
    opt.rect = QRect(toggleX + 2, r.top() + (headerHeight - 12) / 2, 12, 12);
    opt.state |= QStyle::State_Children;
    if (expanded) {
      opt.state |= QStyle::State_Open;
    }
    if (selected) {
      opt.state |= QStyle::State_Selected;
    }
    style()->drawPrimitive(QStyle::PE_IndicatorBranch, &opt, &painter, this);
  } else {
    // Action button for nodes without outputs (e.g. visibility toggle)
    QIcon actIcon = node->actionIcon();
    if (!actIcon.isNull()) {
      QRect actRect = actionButtonRect(r);
      actIcon.paint(&painter, actRect);
    }
  }

  // Reset brush so it doesn't leak into subsequent paint calls
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::paintPortCard(QPainter& painter,
                                        const LayoutItem& item, bool selected,
                                        bool hovered)
{
  Q_UNUSED(hovered);
  auto r = item.rect;
  auto* port = item.port;

  // Port-type colored border; fill only when selected
  QColor fg = selected ? palette().highlightedText().color()
                       : palette().text().color();
  QColor borderColor = portTypeColor(port);

  QPainterPath path;
  path.addRoundedRect(QRectF(r), CardRadius - 1, CardRadius - 1);
  if (selected) {
    painter.fillPath(path, palette().highlight());
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(borderColor, 1.0));
  painter.drawPath(path);

  int x = r.left() + 6;

  // Port type icon
  QIcon pIcon = portTypeIcon(port);
  int portIconSize = 12;
  QRect pIconRect(x, r.top() + (r.height() - portIconSize) / 2,
                  portIconSize, portIconSize);
  pIcon.paint(&painter, pIconRect);
  x += portIconSize + 4;

  // Port name
  QFont portFont = font();
  portFont.setPixelSize(11);
  painter.setFont(portFont);
  painter.setPen(fg);
  QFontMetrics fm(portFont);
  QString name = fm.elidedText(port->name(), Qt::ElideRight, r.width() - 24);
  painter.drawText(QRect(x, r.top(), r.width() - x + r.left(), r.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, name);
}

QPoint PipelineStripWidget::inputDotPos(Node* node, int portIndex,
                                        const QRect& nodeRect) const
{
  int startX = nodeRect.left() + PortIndent;
  int x = (node->inputPorts().size() <= 1) ? startX
                                           : startX + portIndex * DotSpacing;
  return QPoint(x, nodeRect.top());
}

QPoint PipelineStripWidget::outputDotPos(Node* node, int portIndex,
                                         const QRect& nodeRect) const
{
  int startX = nodeRect.left() + PortIndent;
  int x = (node->outputPorts().size() <= 1) ? startX
                                            : startX + portIndex * DotSpacing;
  return QPoint(x, nodeRect.bottom());
}

QPoint PipelineStripWidget::outputPortPos(OutputPort* port) const
{
  auto* node = port->node();
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::PortCard && item.port == port) {
      return QPoint(item.rect.left(), item.rect.center().y());
    }
  }
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard && item.node == node) {
      int portIdx = node->outputPorts().indexOf(port);
      return outputDotPos(node, portIdx, item.rect);
    }
  }
  return QPoint();
}

QPoint PipelineStripWidget::inputPortPos(InputPort* port) const
{
  auto* node = port->node();
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard && item.node == node) {
      int portIdx = node->inputPorts().indexOf(port);
      return inputDotPos(node, portIdx, item.rect);
    }
  }
  return QPoint();
}

bool PipelineStripWidget::isPortCardVisible(OutputPort* port) const
{
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::PortCard && item.port == port) {
      return true;
    }
  }
  return false;
}

void PipelineStripWidget::paintInputDots(QPainter& painter,
                                         const LayoutItem& item)
{
  auto* node = item.node;
  auto inputs = node->inputPorts();
  if (inputs.isEmpty()) {
    return;
  }

  for (int i = 0; i < inputs.size(); ++i) {
    auto* inPort = inputs[i];
    QColor color;
    bool validLink = inPort->link() && inPort->link()->isValid();
    if (validLink) {
      color = portTypeColor(inPort->link()->from());
    } else {
      // Use the first accepted type's color for unconnected inputs
      PortTypes accepted = inPort->acceptedTypes();
      PortType primaryType = PortType::None;
      for (auto t : { PortType::ImageData, PortType::TiltSeries,
                      PortType::Volume, PortType::Image, PortType::Scalar,
                      PortType::Array, PortType::Table, PortType::Molecule }) {
        if (accepted.testFlag(t)) {
          primaryType = t;
          break;
        }
      }
      color = portTypeColor(primaryType);
    }
    QPoint pos = inputDotPos(node, i, item.rect);
    bool invalidLink = inPort->link() && !inPort->link()->isValid();
    if (invalidLink) {
      // Draw an "X" for invalid links
      painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
      painter.setBrush(Qt::NoBrush);
      int r = DotRadius;
      painter.drawLine(pos.x() - r, pos.y() - r, pos.x() + r, pos.y() + r);
      painter.drawLine(pos.x() - r, pos.y() + r, pos.x() + r, pos.y() - r);
    } else {
      // Input dots filled with app background, stroke with port color
      painter.setBrush(palette().window());
      painter.setPen(QPen(color, 1.5));
      painter.drawEllipse(pos, DotRadius, DotRadius);
    }
  }
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::paintOutputDots(QPainter& painter,
                                          const LayoutItem& item)
{
  auto* node = item.node;
  auto outputs = node->outputPorts();
  if (outputs.isEmpty()) {
    return;
  }

  // Don't draw output dots on the node if ports are shown as cards
  if (m_expandedNodes.contains(node)) {
    return;
  }

  for (int i = 0; i < outputs.size(); ++i) {
    QColor color = portTypeColor(outputs[i]);
    bool isSelected = (m_selectedPort == outputs[i]);
    // Output dots are filled; draw larger with highlight ring when selected
    painter.setBrush(color);
    if (isSelected) {
      painter.setPen(QPen(palette().highlight().color(), 2.0));
      painter.drawEllipse(outputDotPos(node, i, item.rect),
                          DotRadius + 2, DotRadius + 2);
    } else {
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(outputDotPos(node, i, item.rect),
                          DotRadius, DotRadius);
    }
  }
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::paintPortCardDot(QPainter& painter,
                                           const LayoutItem& item)
{
  auto* port = item.port;
  QColor color = portTypeColor(port);
  // Port card dots are filled (output)
  painter.setBrush(color);
  painter.setPen(Qt::NoPen);
  QPoint pos(item.rect.left(), item.rect.center().y());
  painter.drawEllipse(pos, DotRadius, DotRadius);
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::computeLinkGeometries()
{
  m_linkGeometries.clear();

  if (!m_pipeline) {
    return;
  }

  // Build node order from layout for adjacency checks
  QList<Node*> nodeOrder;
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard) {
      nodeOrder.append(item.node);
    }
  }

  // Classify links as direct (straight line) or gutter-routed.
  auto links = m_pipeline->links();

  auto isDirect = [&](Link* link) -> bool {
    auto* srcNode = link->from()->node();
    auto* dstNode = link->to()->node();
    int srcIdx = nodeOrder.indexOf(srcNode);
    int dstIdx = nodeOrder.indexOf(dstNode);
    if (dstIdx != srcIdx + 1) {
      return false;
    }
    return !isPortCardVisible(link->from());
  };

  // Compute departure/approach Y for a gutter-routed link
  auto departY = [this](OutputPort* port) -> int {
    QPoint pt = outputPortPos(port);
    if (isPortCardVisible(port)) {
      return pt.y();
    }
    int outIdx = port->node()->outputPorts().indexOf(port);
    return pt.y() + DotClearance + outIdx * LaneSpacing;
  };

  auto approachY = [this](InputPort* port) -> int {
    QPoint pt = inputPortPos(port);
    int inIdx = port->node()->inputPorts().indexOf(port);
    return pt.y() - DotClearance - inIdx * LaneSpacing;
  };

  // Assign gutter lanes using interval coloring
  struct GutterSpan
  {
    OutputPort* port;
    int minY;
    int maxY;
  };
  QList<GutterSpan> spans;
  QMap<OutputPort*, int> spanIndex;

  for (auto* link : links) {
    if (isDirect(link)) {
      continue;
    }
    auto* outPort = link->from();
    if (outputPortPos(outPort).isNull() ||
        inputPortPos(link->to()).isNull()) {
      continue;
    }

    int dY = departY(outPort);
    int aY = approachY(link->to());
    int segMinY = qMin(dY, aY);
    int segMaxY = qMax(dY, aY);

    if (spanIndex.contains(outPort)) {
      auto& span = spans[spanIndex[outPort]];
      span.minY = qMin(span.minY, segMinY);
      span.maxY = qMax(span.maxY, segMaxY);
    } else {
      spanIndex[outPort] = spans.size();
      spans.append({ outPort, segMinY, segMaxY });
    }
  }

  std::sort(spans.begin(), spans.end(),
            [](const GutterSpan& a, const GutterSpan& b) {
              return a.minY < b.minY;
            });

  QMap<OutputPort*, int> portLaneIndex;
  QList<QList<GutterSpan>> lanes;

  for (auto& span : spans) {
    int assignedLane = -1;
    for (int lane = 0; lane < lanes.size(); ++lane) {
      bool conflict = false;
      for (auto& existing : lanes[lane]) {
        if (span.minY <= existing.maxY && span.maxY >= existing.minY) {
          conflict = true;
          break;
        }
      }
      if (!conflict) {
        assignedLane = lane;
        break;
      }
    }
    if (assignedLane < 0) {
      assignedLane = lanes.size();
      lanes.append(QList<GutterSpan>());
    }
    lanes[assignedLane].append(span);
    portLaneIndex[span.port] = assignedLane;
  }

  m_gutterLaneCount = lanes.size();

  // Build link geometries using buildLinkPath
  for (auto* link : links) {
    auto* outPort = link->from();
    int gutterX = 0;
    if (!isDirect(link) && portLaneIndex.contains(outPort)) {
      int laneIdx = portLaneIndex[outPort];
      gutterX = GutterWidth - LaneSpacing / 2 - laneIdx * LaneSpacing;
    }
    QPainterPath path = buildLinkPath(outPort, link->to(), gutterX);
    if (!path.isEmpty()) {
      m_linkGeometries.append(
        { link, path, portTypeColor(outPort), link->isValid() });
    }
  }
}

QPainterPath PipelineStripWidget::buildLinkPath(OutputPort* fromPort,
                                                InputPort* toPort,
                                                int gutterX) const
{
  QPoint srcPt = outputPortPos(fromPort);
  QPoint dstPt = inputPortPos(toPort);
  if (srcPt.isNull() || dstPt.isNull()) {
    return QPainterPath();
  }

  bool portCard = isPortCardVisible(fromPort);

  // Check if direct (adjacent nodes, output not expanded to port cards)
  bool direct = false;
  if (!portCard) {
    auto* srcNode = fromPort->node();
    auto* dstNode = toPort->node();
    QList<Node*> nodeOrder;
    for (auto& item : m_layout) {
      if (item.type == LayoutItem::NodeCard) {
        nodeOrder.append(item.node);
      }
    }
    int srcOrderIdx = nodeOrder.indexOf(srcNode);
    int dstOrderIdx = nodeOrder.indexOf(dstNode);
    direct = (dstOrderIdx == srcOrderIdx + 1);
  }

  QPainterPath path;
  if (direct) {
    path.moveTo(srcPt);
    path.lineTo(dstPt);
  } else {
    int outIdx = fromPort->node()->outputPorts().indexOf(fromPort);
    int departY = portCard
                    ? srcPt.y()
                    : srcPt.y() + DotClearance + outIdx * LaneSpacing;
    int inIdx = toPort->node()->inputPorts().indexOf(toPort);
    int approachY = dstPt.y() - DotClearance - inIdx * LaneSpacing;

    path.moveTo(srcPt);
    if (departY != srcPt.y()) {
      path.lineTo(srcPt.x(), departY);
    }
    path.lineTo(gutterX, departY);
    path.lineTo(gutterX, approachY);
    path.lineTo(dstPt.x(), approachY);
    if (approachY != dstPt.y()) {
      path.lineTo(dstPt);
    }
  }
  return path;
}

void PipelineStripWidget::paintConnections(QPainter& painter)
{
  painter.setBrush(Qt::NoBrush);

  for (auto& lg : m_linkGeometries) {
    bool hovered = (lg.link == m_hoveredLink);
    bool selected = (lg.link == m_selectedLink);
    qreal baseWidth = 3.0;

    // Selected: draw a shadow/glow behind the link
    if (selected) {
      QColor shadowColor = lg.color;
      shadowColor.setAlpha(60);
      painter.setPen(QPen(shadowColor, baseWidth + 6.0, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(lg.path);
    }

    // Draw the link line (hovered = +1px width)
    qreal strokeWidth = hovered ? baseWidth + 2.0 : baseWidth;
    painter.setPen(QPen(lg.color, strokeWidth, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(lg.path);
  }
}

OutputPort* PipelineStripWidget::outputPortHitTest(const QPoint& pos) const
{
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard &&
        !m_expandedNodes.contains(item.node)) {
      auto outs = item.node->outputPorts();
      for (int i = 0; i < outs.size(); ++i) {
        QPoint dotPos = outputDotPos(item.node, i, item.rect);
        int dx = pos.x() - dotPos.x();
        int dy = pos.y() - dotPos.y();
        if (dx * dx + dy * dy <= (DotRadius + 2) * (DotRadius + 2)) {
          return outs[i];
        }
      }
    } else if (item.type == LayoutItem::PortCard) {
      QPoint dotPos(item.rect.left(), item.rect.center().y());
      int dx = pos.x() - dotPos.x();
      int dy = pos.y() - dotPos.y();
      if (dx * dx + dy * dy <= (DotRadius + 2) * (DotRadius + 2)) {
        return item.port;
      }
    }
  }
  return nullptr;
}

InputPort* PipelineStripWidget::inputPortHitTest(const QPoint& pos) const
{
  for (auto& item : m_layout) {
    if (item.type != LayoutItem::NodeCard) {
      continue;
    }
    auto inputs = item.node->inputPorts();
    for (int i = 0; i < inputs.size(); ++i) {
      QPoint dotPos = inputDotPos(item.node, i, item.rect);
      int dx = pos.x() - dotPos.x();
      int dy = pos.y() - dotPos.y();
      if (dx * dx + dy * dy <= (DotRadius + 3) * (DotRadius + 3)) {
        return inputs[i];
      }
    }
  }
  return nullptr;
}

void PipelineStripWidget::paintPendingLink(QPainter& painter)
{
  if (!m_draggingLink || !m_dragFromPort) {
    return;
  }

  QColor lineColor = portTypeColor(m_dragFromPort);
  painter.setBrush(Qt::NoBrush);

  if (m_dragToPort) {
    // Valid target — reuse the same path logic as real links
    int gutterX = GutterWidth - LaneSpacing / 2 -
                  m_gutterLaneCount * LaneSpacing;
    gutterX = qMax(gutterX, 2);

    QPainterPath path = buildLinkPath(m_dragFromPort, m_dragToPort, gutterX);
    if (!path.isEmpty()) {
      painter.setPen(QPen(lineColor, 3.0, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(path);
    }
  } else {
    // No valid target — draw from output to gutter, ending at mouse Y
    QPoint srcPt = outputPortPos(m_dragFromPort);
    if (srcPt.isNull()) {
      return;
    }

    bool portCard = isPortCardVisible(m_dragFromPort);
    int outIdx = m_dragFromPort->node()->outputPorts().indexOf(m_dragFromPort);
    int departY = portCard ? srcPt.y()
                           : srcPt.y() + DotClearance + outIdx * LaneSpacing;

    int gutterX = GutterWidth - LaneSpacing / 2 -
                  m_gutterLaneCount * LaneSpacing;
    gutterX = qMax(gutterX, 2);

    QPainterPath path;
    path.moveTo(srcPt);
    if (departY != srcPt.y()) {
      path.lineTo(srcPt.x(), departY);
    }
    path.lineTo(gutterX, departY);
    path.lineTo(gutterX, m_dragCurrentPos.y());

    painter.setPen(QPen(lineColor, 3.0, Qt::DashLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
  }
}

QColor PipelineStripWidget::badgeColor(Node* node) const
{
  if (qobject_cast<SourceNode*>(node)) {
    return QColor(76, 175, 80); // green
  }
  if (qobject_cast<TransformNode*>(node)) {
    return QColor(33, 150, 243); // blue
  }
  if (qobject_cast<SinkNode*>(node)) {
    return QColor(255, 152, 0); // orange
  }
  return QColor(158, 158, 158); // gray
}

QIcon PipelineStripWidget::portTypeIcon(OutputPort* port) const
{
  if (!port) {
    return QIcon(QStringLiteral(":/icons/pqInspect.png"));
  }
  switch (port->type()) {
    case PortType::ImageData:
    case PortType::TiltSeries:
    case PortType::Volume:
      return QIcon(QStringLiteral(":/icons/pqInspect.png"));
    case PortType::Table:
      return QIcon(QStringLiteral(":/pqWidgets/Icons/pqSpreadsheet.svg"));
    case PortType::Molecule:
      return QIcon(QStringLiteral(":/icons/pqInspect.png"));
    default:
      return QIcon(QStringLiteral(":/icons/pqInspect.png"));
  }
}

QColor PipelineStripWidget::portTypeColor(PortType type) const
{
  // Port type colors are chosen to be distinct from node badge colors
  // (Source=green, Transform=blue, Sink=orange).
  switch (type) {
    case PortType::ImageData:
      return QColor(158, 118, 47); // amber (generic volume data)
    case PortType::TiltSeries:
      return QColor(57, 73, 171);  // indigo (tilt series)
    case PortType::Volume:
      return QColor(171, 71, 188); // orchid (reconstructed volume)
    case PortType::Table:
      return QColor(0, 172, 172); // teal
    case PortType::Molecule:
      return QColor(194, 60, 108); // rose
    case PortType::Image:
      return QColor(120, 144, 56); // olive
    default:
      return QColor(158, 158, 158); // gray
  }
}

QColor PipelineStripWidget::portTypeColor(OutputPort* port) const
{
  if (!port) {
    return QColor(158, 158, 158);
  }
  return portTypeColor(port->type());
}

QIcon PipelineStripWidget::stateIcon(Node* node) const
{
  switch (node->state()) {
    case NodeState::Current:
      return QIcon(QStringLiteral(":/pipeline/check.png"));
    case NodeState::Stale:
      return QIcon(QStringLiteral(":/pipeline/question.png"));
    case NodeState::New:
      return QIcon(QStringLiteral(":/pipeline/edit.png"));
    default:
      return QIcon();
  }
}

QRect PipelineStripWidget::breakpointRect(const QRect& cardRect) const
{
  // Layout: [breakpoint] [state] [expand/action]
  int toggleX = cardRect.right() - HeaderExpandWidth - HeaderRightPad;
  int stateX = toggleX - HeaderIconSize;
  int bpX = stateX - HeaderIconSize;
  int bpY = cardRect.top() + (NodeCardHeight - HeaderIconSize) / 2;
  return QRect(bpX, bpY, HeaderIconSize, HeaderIconSize);
}

QRect PipelineStripWidget::actionButtonRect(const QRect& cardRect) const
{
  // Same position as the expand toggle
  int toggleX = cardRect.right() - HeaderExpandWidth - HeaderRightPad;
  int toggleY = cardRect.top() + (NodeCardHeight - HeaderIconSize) / 2;
  return QRect(toggleX, toggleY, HeaderExpandWidth, HeaderIconSize);
}

// --- Interaction ---

void PipelineStripWidget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    // Check for output port dot press (potential link drag start)
    auto* outPort = outputPortHitTest(event->pos());
    if (outPort) {
      m_dragFromPort = outPort;
      m_dragStartPos = event->pos();
      m_dragCurrentPos = event->pos();
      m_draggingLink = false;
      m_dragToPort = nullptr;
      return;
    }

    int idx = hitTest(event->pos());

    // Check clicks on node card action areas
    if (idx >= 0 && m_layout[idx].type == LayoutItem::NodeCard) {
      auto* node = m_layout[idx].node;
      auto& cardRect = m_layout[idx].rect;

      // Breakpoint area
      QRect bpRect = breakpointRect(cardRect);
      if (bpRect.contains(event->pos())) {
        node->setBreakpoint(!node->hasBreakpoint());
        update();
        return;
      }

      // Expand toggle (rightmost area) or action button
      auto outputs = node->outputPorts();
      if (!outputs.isEmpty()) {
        int toggleX = cardRect.right() - HeaderExpandWidth - HeaderRightPad;
        if (event->pos().x() >= toggleX &&
            event->pos().x() <= cardRect.right() - HeaderRightPad) {
          setExpanded(node, !isExpanded(node));
          return;
        }
      } else if (!node->actionIcon().isNull()) {
        QRect actRect = actionButtonRect(cardRect);
        if (actRect.contains(event->pos())) {
          node->triggerAction();
          update();
          return;
        }
      }
    }

    if (idx >= 0) {
      selectItem(idx);
    } else {
      // No card hit — check links
      auto* link = linkHitTest(event->pos());
      if (link) {
        selectLink(link);
      } else {
        selectItem(-1);
      }
    }
  }
  QWidget::mousePressEvent(event);
}

void PipelineStripWidget::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && m_dragFromPort) {
    if (m_draggingLink) {
      // Complete the drag — emit linkRequested if valid target
      if (m_dragToPort) {
        emit linkRequested(m_dragFromPort, m_dragToPort);
      }
    } else {
      // Was a click on output dot (no drag) — select it
      m_selectedIndex = -1;
      m_selectedPort = m_dragFromPort;
      m_selectedLink = nullptr;
      emit portSelected(m_dragFromPort);
    }
    m_dragFromPort = nullptr;
    m_dragToPort = nullptr;
    m_draggingLink = false;
    update();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void PipelineStripWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    int idx = hitTest(event->pos());
    if (idx >= 0 && m_layout[idx].type == LayoutItem::NodeCard) {
      emit nodeDoubleClicked(m_layout[idx].node);
    }
  }
  QWidget::mouseDoubleClickEvent(event);
}

void PipelineStripWidget::keyPressEvent(QKeyEvent* event)
{
  switch (event->key()) {
    case Qt::Key_Up:
      if (m_selectedIndex > 0) {
        selectItem(m_selectedIndex - 1);
      }
      break;
    case Qt::Key_Down:
      if (m_selectedIndex < m_layout.size() - 1) {
        selectItem(m_selectedIndex + 1);
      } else if (m_selectedIndex < 0 && !m_layout.isEmpty()) {
        selectItem(0);
      }
      break;
    case Qt::Key_Escape:
      if (m_draggingLink) {
        m_draggingLink = false;
        m_dragFromPort = nullptr;
        m_dragToPort = nullptr;
        update();
      }
      break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
      // Show context menu at widget origin for the selected element
      showContextMenu(mapToGlobal(QPoint(0, 0)));
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  event->accept();
}

void PipelineStripWidget::contextMenuEvent(QContextMenuEvent* event)
{
  int idx = hitTest(event->pos());

  if (idx >= 0) {
    auto& item = m_layout[idx];
    if (item.type == LayoutItem::PortCard && m_portMenuProvider) {
      selectItem(idx);
      QMenu menu(this);
      m_portMenuProvider(item.port, menu);
      if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
      }
      return;
    }
    if (item.type == LayoutItem::NodeCard && m_nodeMenuProvider) {
      selectItem(idx);
      QMenu menu(this);
      m_nodeMenuProvider(item.node, menu);
      if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
      }
      return;
    }
    return;
  }

  // No card hit — check links
  auto* link = linkHitTest(event->pos());
  if (link && m_linkMenuProvider) {
    selectLink(link);
    QMenu menu(this);
    m_linkMenuProvider(link, menu);
    if (!menu.isEmpty()) {
      menu.exec(event->globalPos());
    }
  }
}

void PipelineStripWidget::showContextMenu(const QPoint& globalPos)
{
  if (m_selectedLink && m_linkMenuProvider) {
    QMenu menu(this);
    m_linkMenuProvider(m_selectedLink, menu);
    if (!menu.isEmpty()) {
      menu.exec(globalPos);
    }
    return;
  }

  if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
    auto& item = m_layout[m_selectedIndex];
    if (item.type == LayoutItem::PortCard && m_portMenuProvider) {
      QMenu menu(this);
      m_portMenuProvider(item.port, menu);
      if (!menu.isEmpty()) {
        menu.exec(globalPos);
      }
      return;
    }
    if (item.type == LayoutItem::NodeCard && m_nodeMenuProvider) {
      QMenu menu(this);
      m_nodeMenuProvider(item.node, menu);
      if (!menu.isEmpty()) {
        menu.exec(globalPos);
      }
    }
  }
}

void PipelineStripWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  rebuildLayout();
}

void PipelineStripWidget::mouseMoveEvent(QMouseEvent* event)
{
  // Handle link creation drag
  if (m_dragFromPort) {
    if (!m_draggingLink) {
      // Check drag threshold
      if ((event->pos() - m_dragStartPos).manhattanLength() >=
          QApplication::startDragDistance()) {
        m_draggingLink = true;
      }
    }
    if (m_draggingLink) {
      m_dragCurrentPos = event->pos();
      // Check if hovering a valid input port
      auto* inPort = inputPortHitTest(event->pos());
      if (inPort) {
        bool valid = !m_linkValidator ||
                     m_linkValidator(m_dragFromPort, inPort);
        m_dragToPort = valid ? inPort : nullptr;
      } else {
        m_dragToPort = nullptr;
      }
      update();
    }
    return;
  }

  int idx = hitTest(event->pos());
  // Only track hover on node cards (for breakpoint hint)
  if (idx >= 0 && m_layout[idx].type != LayoutItem::NodeCard) {
    // Find the parent node card for port cards
    for (int i = idx - 1; i >= 0; --i) {
      if (m_layout[i].type == LayoutItem::NodeCard) {
        idx = i;
        break;
      }
    }
  }

  // Track link hover (only when not hovering a card)
  Link* hoveredLink = nullptr;
  if (idx < 0) {
    hoveredLink = linkHitTest(event->pos());
  }

  bool needsUpdate = false;
  if (idx != m_hoveredIndex) {
    m_hoveredIndex = idx;
    needsUpdate = true;
  }
  if (hoveredLink != m_hoveredLink) {
    m_hoveredLink = hoveredLink;
    needsUpdate = true;
  }
  if (needsUpdate) {
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void PipelineStripWidget::leaveEvent(QEvent* event)
{
  bool needsUpdate = false;
  if (m_hoveredIndex >= 0) {
    m_hoveredIndex = -1;
    needsUpdate = true;
  }
  if (m_hoveredLink) {
    m_hoveredLink = nullptr;

    needsUpdate = true;
  }
  if (needsUpdate) {
    update();
  }
  QWidget::leaveEvent(event);
}

} // namespace pipeline
} // namespace tomviz
