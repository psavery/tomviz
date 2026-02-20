/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PipelineModel.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "Module.h"
#include "ModuleManager.h"
#include "MoleculeSource.h"
#include "Operator.h"
#include "OperatorResult.h"
#include "Pipeline.h"

#include <QFileInfo>
#include <QFont>
#include <cassert>

#include <vtkRectilinearGrid.h>
#include <vtkStructuredGrid.h>
#include <vtkTable.h>
#include <vtkUnstructuredGrid.h>

namespace tomviz {

struct PipelineModel::Item
{
  Item(DataSource* source) : tag(DATASOURCE), s(source) {}
  Item(Module* module) : tag(MODULE), m(module) {}
  Item(Operator* op) : tag(OPERATOR), o(op) {}
  Item(OperatorResult* result) : tag(RESULT), r(result) {}
  Item(MoleculeSource* source) : tag(MOLECULESOURCE), ms(source) {}

  DataSource* dataSource() { return tag == DATASOURCE ? s : nullptr; }
  Module* module() { return tag == MODULE ? m : nullptr; }
  Operator* op() { return tag == OPERATOR ? o : nullptr; }
  OperatorResult* result() { return tag == RESULT ? r : nullptr; }
  MoleculeSource* moleculeSource()
  {
    return tag == MOLECULESOURCE ? ms : nullptr;
  }

  enum
  {
    DATASOURCE,
    MODULE,
    OPERATOR,
    RESULT,
    MOLECULESOURCE
  } tag;
  union
  {
    DataSource* s;
    Module* m;
    Operator* o;
    OperatorResult* r;
    MoleculeSource* ms;
  };
};

class PipelineModel::TreeItem
{
public:
  explicit TreeItem(const PipelineModel::Item& item,
                    TreeItem* parent = nullptr);
  ~TreeItem();

  TreeItem* parent() { return m_parent; }
  void setParent(TreeItem* parent) { m_parent = parent; }
  TreeItem* child(int index);
  TreeItem* lastChild();
  int childCount() const { return m_children.count(); }
  QList<TreeItem*>& children() { return m_children; }
  int childIndex() const;
  bool appendChild(const PipelineModel::Item& item);
  bool insertChild(int position, const PipelineModel::Item& item);
  bool removeChild(int position);
  PipelineModel::TreeItem* detachChild(int position);
  PipelineModel::TreeItem* detach();
  bool attach(PipelineModel::TreeItem* treeItem);

  bool remove(DataSource* source);
  bool remove(MoleculeSource* source);
  bool remove(Module* module);
  bool remove(Operator* op);

  /// Recursively search entire tree for given object.
  TreeItem* find(MoleculeSource* source);
  TreeItem* find(Module* module);
  TreeItem* find(Operator* op);
  TreeItem* find(OperatorResult* result);

  void setItem(const PipelineModel::Item& item) { m_item = item; }
  DataSource* dataSource() { return m_item.dataSource(); }
  Module* module() { return m_item.module(); }
  Operator* op() { return m_item.op(); }
  MoleculeSource* moleculeSource() { return m_item.moleculeSource(); }
  OperatorResult* result() { return m_item.result(); }

private:
  QList<TreeItem*> m_children;
  PipelineModel::Item m_item;
  TreeItem* m_parent = nullptr;
};

PipelineModel::TreeItem::TreeItem(const PipelineModel::Item& i, TreeItem* p)
  : m_item(i), m_parent(p)
{}

PipelineModel::TreeItem::~TreeItem()
{
  qDeleteAll(m_children);
}

PipelineModel::TreeItem* PipelineModel::TreeItem::child(int index)
{
  return m_children.value(index);
}

PipelineModel::TreeItem* PipelineModel::TreeItem::lastChild()
{
  if (childCount() == 0) {
    return nullptr;
  }
  return child(childCount() - 1);
}

int PipelineModel::TreeItem::childIndex() const
{
  if (m_parent) {
    return m_parent->m_children.indexOf(const_cast<TreeItem*>(this));
  }
  return 0;
}

bool PipelineModel::TreeItem::insertChild(int pos,
                                          const PipelineModel::Item& item)
{
  if (pos < 0 || pos > m_children.size()) {
    return false;
  }
  TreeItem* treeItem = new TreeItem(item, this);
  m_children.insert(pos, treeItem);
  return true;
}

bool PipelineModel::TreeItem::appendChild(const PipelineModel::Item& item)
{
  TreeItem* treeItem = new TreeItem(item, this);
  m_children.append(treeItem);
  return true;
}

bool PipelineModel::TreeItem::removeChild(int pos)
{
  if (pos < 0 || pos >= m_children.size()) {
    return false;
  }
  delete m_children.takeAt(pos);
  return true;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::detachChild(int pos)
{
  if (pos < 0 || pos >= m_children.size()) {
    return nullptr;
  }
  auto child = m_children.takeAt(pos);
  child->setParent(nullptr);

  return child;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::detach()
{

  return parent()->detachChild(childIndex());
}

bool PipelineModel::TreeItem::attach(PipelineModel::TreeItem* treeItem)
{
  m_children.append(treeItem);
  treeItem->setParent(this);
  return true;
}

bool PipelineModel::TreeItem::remove(DataSource* source)
{
  if (source != dataSource()) {
    return false;
  }

  // This item is a DataSource item. Remove all children.
  foreach (auto childItem, m_children) {
    if (childItem->op()) {
      auto op = childItem->op();
      // Pause the pipeline
      auto pipeline = op->dataSource()->pipeline();
      if (pipeline != nullptr) {
        pipeline->pause();
      }
      ModuleManager::instance().removeOperator(childItem->op());
      if (pipeline != nullptr) {
        // Resume but don't execute as we are removing this data source.
        pipeline->resume();
      }
    } else if (childItem->module()) {
      ModuleManager::instance().removeModule(childItem->module());
    }
  }
  if (parent()) {
    parent()->removeChild(childIndex());
    return true;
  }
  return false;
}

bool PipelineModel::TreeItem::remove(MoleculeSource* source)
{
  if (source != moleculeSource()) {
    return false;
  }
  // This item is a DataSource item. Remove all children.
  foreach (auto childItem, m_children) {
    if (childItem->module()) {
      ModuleManager::instance().removeModule(childItem->module());
    }
  }
  return true;
}

bool PipelineModel::TreeItem::remove(Module* module)
{
  foreach (auto childItem, m_children) {
    if (childItem->module() == module) {
      removeChild(childItem->childIndex());
      return true;
    }
  }
  return false;
}

bool PipelineModel::TreeItem::remove(Operator* o)
{
  foreach (auto childItem, m_children) {
    if (childItem->op() == o) {
      foreach (auto resultItem, childItem->children()) {
        // Remove results
        childItem->removeChild(resultItem->childIndex());
      }
      removeChild(childItem->childIndex());
      return true;
    }
  }
  return false;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::find(Module* module)
{
  if (this->module() == module) {
    return this;
  } else {
    foreach (auto childItem, m_children) {
      auto moduleItem = childItem->find(module);
      if (moduleItem) {
        return moduleItem;
      }
    }
  }
  return nullptr;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::find(Operator* op)
{
  if (this->op() == op) {
    return this;
  } else {
    foreach (auto childItem, m_children) {
      auto operatorItem = childItem->find(op);
      if (operatorItem) {
        return operatorItem;
      }
    }
  }
  return nullptr;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::find(OperatorResult* result)
{
  if (this->result() == result) {
    return this;
  } else {
    foreach (auto childItem, m_children) {
      auto resultItem = childItem->find(result);
      if (resultItem) {
        return resultItem;
      }
    }
  }
  return nullptr;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::find(MoleculeSource* source)
{
  if (this->moleculeSource() == source) {
    return this;
  }
  return nullptr;
}

PipelineModel::PipelineModel(QObject* p) : QAbstractItemModel(p)
{
  connect(&ModuleManager::instance(), SIGNAL(dataSourceAdded(DataSource*)),
          SLOT(dataSourceAdded(DataSource*)));
  connect(&ModuleManager::instance(), SIGNAL(moduleAdded(Module*)),
          SLOT(moduleAdded(Module*)));
  connect(&ModuleManager::instance(),
          SIGNAL(moleculeSourceAdded(MoleculeSource*)),
          SLOT(moleculeSourceAdded(MoleculeSource*)));

  connect(&ActiveObjects::instance(), SIGNAL(viewChanged(vtkSMViewProxy*)),
          SIGNAL(modelReset()));
  connect(&ModuleManager::instance(), SIGNAL(dataSourceRemoved(DataSource*)),
          SLOT(dataSourceRemoved(DataSource*)));
  connect(&ModuleManager::instance(),
          SIGNAL(moleculeSourceRemoved(MoleculeSource*)),
          SLOT(moleculeSourceRemoved(MoleculeSource*)));
  connect(&ModuleManager::instance(), SIGNAL(moduleRemoved(Module*)),
          SLOT(moduleRemoved(Module*)));
  connect(&ModuleManager::instance(), SIGNAL(operatorRemoved(Operator*)),
          SLOT(operatorRemoved(Operator*)));
  // Need to register this for cross thread dataChanged signal
  qRegisterMetaType<QVector<int>>("QVector<int>");
}

PipelineModel::~PipelineModel() {}

namespace {
QIcon iconForDataObject(vtkDataObject* dataObject)
{
  if (vtkTable::SafeDownCast(dataObject)) {
    return QIcon(":/pqWidgets/Icons/pqSpreadsheet.svg");
  } else if (vtkUnstructuredGrid::SafeDownCast(dataObject)) {
    return QIcon(":/pqWidgets/Icons/pqUnstructuredGrid16.png");
  } else if (vtkStructuredGrid::SafeDownCast(dataObject)) {
    return QIcon(":/pqWidgets/Icons/pqStructuredGrid16.png");
  } else if (vtkRectilinearGrid::SafeDownCast(dataObject)) {
    return QIcon(":/pqWidgets/Icons/pqRectilinearGrid16.png");
  }

  return QIcon(":/icons/pqInspect.png");
}

QIcon iconForOperatorState(tomviz::OperatorState state)
{
  switch (state) {
    case OperatorState::Complete:
      return QIcon(":/icons/check.png");
    case OperatorState::Edit:
      return QIcon(":/icons/edit.png");
    case OperatorState::Queued:
      return QIcon(":/icons/question.png");
    case OperatorState::Error:
      return QIcon(":/icons/error_notification.png");
    case OperatorState::Canceled:
      return QIcon(":/icons/red_cross.png");
    case OperatorState::Running:
      // Our subclass of QItemDelegate will take care of this animated icon
      break;
  }

  return QIcon();
}

QString tooltipForOperatorState(tomviz::OperatorState state)
{
  switch (state) {
    case OperatorState::Running:
      return QString("Running");
    case OperatorState::Complete:
      return QString("Complete");
    case OperatorState::Edit:
      return QString("Editing");
    case OperatorState::Queued:
      return QString("Queued");
    case OperatorState::Error:
      return QString("Error");
    case OperatorState::Canceled:
      return QString("Canceled");
  }

  return "";
}
} // namespace

QVariant PipelineModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid() || index.column() > Column::state)
    return QVariant();

  auto treeItem = this->treeItem(index);
  auto dataSource = treeItem->dataSource();
  auto moleculeSource = treeItem->moleculeSource();
  auto module = treeItem->module();
  auto op = treeItem->op();
  auto result = treeItem->result();

  // Data source
  if (dataSource) {
    if (index.column() == Column::label) {
      switch (role) {
        case Qt::DecorationRole:
          return QIcon(":/icons/pqInspect.png");
        case Qt::DisplayRole: {
          QString label = dataSource->label();
          if (dataSource->persistenceState() ==
              DataSource::PersistenceState::Modified) {
            label += QString(" *");
          }
          return label;
        }
        case Qt::ToolTipRole:
          return dataSource->fileName();
        case Qt::FontRole:
          if (dataSource->persistenceState() ==
              DataSource::PersistenceState::Modified) {
            QFont font;
            font.setItalic(true);
            return font;
          } else {
            return QVariant();
          }
        default:
          return QVariant();
      }
    }
  } else if (moleculeSource) {
    if (index.column() == Column::label) {
      switch (role) {
        case Qt::DecorationRole:
          return QIcon(":/icons/gradient_opacity.png");
        case Qt::DisplayRole:
          return moleculeSource->label();
        case Qt::ToolTipRole:
          return moleculeSource->label();
        default:
          return QVariant();
      }
    } else {
      return QVariant();
    }
  } else if (module) {
    if (index.column() == Column::label) {
      switch (role) {
        case Qt::DecorationRole:
          return module->icon();
        case Qt::DisplayRole:
          return module->label();
        case Qt::ToolTipRole:
          return module->label();
        default:
          return QVariant();
      }
    } else if (index.column() == Column::state) {
      if (role == Qt::DecorationRole) {
        if (module->visibility()) {
          return QIcon(":/pqWidgets/Icons/pqEyeball.svg");
        } else {
          return QIcon(":/pqWidgets/Icons/pqEyeballClosed.svg");
        }
      }
    }
  } else if (op) {
    if (index.column() == Column::label) {
      switch (role) {
        case Qt::DecorationRole:
          return op->icon();
        case Qt::DisplayRole:
          return op->label();
        case Qt::ToolTipRole:
          if (op->isCanceled()) {
            return "Operator was canceled";
          } else {
            return op->label();
          }
        case Qt::FontRole:
          if (op->isCanceled()) {
            QFont font;
            font.setStrikeOut(true);
            return font;
          } else {
            return QVariant();
          }
        default:
          return QVariant();
      }
    } else if (index.column() == Column::state) {
      switch (role) {
        case Qt::DecorationRole:
          return iconForOperatorState(op->state());
        case Qt::ToolTipRole:
          return tooltipForOperatorState(op->state());
        default:
          return QVariant();
      }
    }
  } else if (result) {
    if (index.column() == Column::label) {
      switch (role) {
        case Qt::DecorationRole:
          return iconForDataObject(result->dataObject());
        case Qt::DisplayRole:
          return result->label();
        case Qt::ToolTipRole:
          return result->description();
        default:
          return QVariant();
      }
    }
  }
  return QVariant();
}

bool PipelineModel::setData(const QModelIndex& index, const QVariant& value,
                            int role)
{
  if (role != Qt::CheckStateRole) {
    return false;
  }

  auto treeItem = this->treeItem(index);
  if (index.column() == Column::state && treeItem->module()) {
    treeItem->module()->setVisibility(value == Qt::Checked);
    emit dataChanged(index, index);
  }
  return true;
}

Qt::ItemFlags PipelineModel::flags(const QModelIndex& index) const
{
  if (!index.isValid())
    return Qt::ItemFlags();

  auto treeItem = this->treeItem(index);
  auto module = treeItem->module();
  auto view = ActiveObjects::instance().activeView();

  if (module && module->view() != view) {
    return Qt::NoItemFlags;
  }
  return QAbstractItemModel::flags(index);
}

QVariant PipelineModel::headerData(int, Qt::Orientation, int) const
{
  return QVariant();
}

QModelIndex PipelineModel::index(int row, int column,
                                 const QModelIndex& parent) const
{
  if (!parent.isValid() && row < m_treeItems.count()) {
    // Data source
    return createIndex(row, column, m_treeItems[row]);
  } else {
    // Module or operator
    auto treeItem = this->treeItem(parent);
    if (treeItem && row < treeItem->childCount()) {
      return createIndex(row, column, treeItem->child(row));
    }
  }

  return QModelIndex();
}

QModelIndex PipelineModel::parent(const QModelIndex& index) const
{
  if (!index.isValid()) {
    return QModelIndex();
  }
  auto treeItem = this->treeItem(index);
  if (!treeItem->parent()) {
    return QModelIndex();
  }
  return createIndex(treeItem->parent()->childIndex(), 0, treeItem->parent());
}

int PipelineModel::rowCount(const QModelIndex& parent) const
{
  if (!parent.isValid()) {
    return m_treeItems.count();
  } else {
    auto treeItem = this->treeItem(parent);
    return treeItem->childCount();
  }
}

int PipelineModel::columnCount(const QModelIndex&) const
{
  return 2;
}

DataSource* PipelineModel::dataSource(const QModelIndex& idx)
{
  if (idx.isValid()) {
    auto treeItem = this->treeItem(idx);
    return (treeItem ? treeItem->dataSource() : nullptr);
  } else {
    return nullptr;
  }
}

MoleculeSource* PipelineModel::moleculeSource(const QModelIndex& idx)
{
  if (idx.isValid()) {
    auto treeItem = this->treeItem(idx);
    return (treeItem ? treeItem->moleculeSource() : nullptr);
  } else {
    return nullptr;
  }
}

Module* PipelineModel::module(const QModelIndex& idx)
{
  if (idx.isValid()) {
    auto treeItem = this->treeItem(idx);
    return (treeItem ? treeItem->module() : nullptr);
  } else {
    return nullptr;
  }
}

Operator* PipelineModel::op(const QModelIndex& idx)
{
  if (idx.isValid()) {
    auto treeItem = this->treeItem(idx);
    return (treeItem ? treeItem->op() : nullptr);
  } else {
    return nullptr;
  }
}

OperatorResult* PipelineModel::result(const QModelIndex& idx)
{
  if (idx.isValid()) {
    auto treeItem = this->treeItem(idx);
    return (treeItem ? treeItem->result() : nullptr);
  } else {
    return nullptr;
  }
}

QModelIndex PipelineModel::dataSourceIndexHelper(
  PipelineModel::TreeItem* treeItem, DataSource* source)
{
  Q_ASSERT(treeItem != nullptr);
  if (!source) {
    return QModelIndex();
  } else if (treeItem->dataSource() == source) {

    auto row = treeItem->childIndex();
    // If this item has no parent then we are dealing with a root data source,
    // childIndex() will return 0. We need to find the item in m_treeItems to
    // determine the correct index.
    if (treeItem->parent() == nullptr) {
      row = m_treeItems.indexOf(treeItem);
      Q_ASSERT(row != -1);
    }

    return createIndex(row, 0, treeItem);
  } else {
    // Recurse on children
    foreach (auto childItem, treeItem->children()) {
      QModelIndex childIndex = dataSourceIndexHelper(childItem, source);
      if (childIndex.isValid()) {
        return childIndex;
      }
    }
  }
  return QModelIndex();
}

QModelIndex PipelineModel::dataSourceIndex(DataSource* source)
{
  for (int i = 0; i < m_treeItems.count(); ++i) {
    QModelIndex index = dataSourceIndexHelper(m_treeItems[i], source);
    if (index.isValid()) {
      return index;
    }
  }

  return QModelIndex();
}

QModelIndex PipelineModel::moleculeSourceIndex(MoleculeSource* source)
{
  foreach (auto treeItem, m_treeItems) {
    auto moleculeItem = treeItem->find(source);
    if (moleculeItem) {
      return createIndex(moleculeItem->childIndex(), 0, moleculeItem);
    }
  }
  return QModelIndex();
}

QModelIndex PipelineModel::moduleIndex(Module* module)
{
  foreach (auto treeItem, m_treeItems) {
    auto moduleItem = treeItem->find(module);
    if (moduleItem) {
      return createIndex(moduleItem->childIndex(), 0, moduleItem);
    }
  }
  return QModelIndex();
}

QModelIndex PipelineModel::operatorIndexHelper(
  PipelineModel::TreeItem* treeItem, Operator* op)
{
  Q_ASSERT(treeItem != nullptr);
  if (!op) {
    return QModelIndex();
  } else if (treeItem->op() == op) {
    return createIndex(treeItem->childIndex(), 0, treeItem);
  } else {
    // Recurse on children
    foreach (auto childItem, treeItem->children()) {
      QModelIndex childIndex = operatorIndexHelper(childItem, op);
      if (childIndex.isValid()) {
        return childIndex;
      }
    }
  }
  return QModelIndex();
}

QModelIndex PipelineModel::operatorIndex(Operator* op)
{
  foreach (auto treeItem, m_treeItems) {
    auto operatorItem = treeItem->find(op);
    if (operatorItem) {
      return createIndex(operatorItem->childIndex(), 0, operatorItem);
    }
  }
  return QModelIndex();
}

QModelIndex PipelineModel::resultIndex(OperatorResult* result)
{
  foreach (auto treeItem, m_treeItems) {
    auto resultItem = treeItem->find(result);
    if (resultItem) {
      return createIndex(resultItem->childIndex(), 0, resultItem);
    }
  }
  return QModelIndex();
}

void PipelineModel::dataSourceAdded(DataSource* dataSource)
{
  auto treeItem = new PipelineModel::TreeItem(PipelineModel::Item(dataSource));
  beginInsertRows(QModelIndex(), m_treeItems.size(), m_treeItems.size());
  m_treeItems.append(treeItem);
  endInsertRows();
  auto pipeline = dataSource->pipeline();
  connect(pipeline, &Pipeline::operatorAdded, this,
          [this](Operator* op, DataSource* ds) { operatorAdded(op, ds); });

  // Fire signal to indicate that the transformed data source has been modified
  // when the pipeline has been executed.
  // TODO This should probably be move else where!
  connect(pipeline, &Pipeline::finished, [this, pipeline]() {
    auto transformed = pipeline->transformedDataSource();
    emit dataSourceModified(transformed);
  });

  // Refresh all operator rows when a breakpoint is reached: the breakpoint
  // operator's label column shows the play icon, and operators from the
  // breakpoint onwards have their state column updated (Complete → Queued).
  connect(pipeline, &Pipeline::breakpointReached, [this](Operator* op) {
    auto ds = op->dataSource();
    if (!ds)
      return;
    auto ops = ds->operators();
    int bpIdx = ops.indexOf(op);
    for (int i = bpIdx; i < ops.size(); ++i) {
      auto idx = operatorIndex(ops[i]);
      auto stateIdx = index(idx.row(), Column::state, idx.parent());
      emit dataChanged(idx, stateIdx);
    }
  });

  // When restoring a data source from a state file it will have its operators
  // before we can listen to the signal above. Display those operators.
  foreach (auto op, dataSource->operators()) {
    operatorAdded(op);
  }
  emit dataSourceItemAdded(dataSource);
}

void PipelineModel::moleculeSourceAdded(MoleculeSource* moleculeSource)
{
  auto treeItem =
    new PipelineModel::TreeItem(PipelineModel::Item(moleculeSource));
  beginInsertRows(QModelIndex(), m_treeItems.size(), m_treeItems.size());
  m_treeItems.append(treeItem);
  endInsertRows();
  emit moleculeSourceItemAdded(moleculeSource);
}

void PipelineModel::moduleAdded(Module* module)
{
  Q_ASSERT(module);
  auto dataSource = module->dataSource();
  auto moleculeSource = module->moleculeSource();
  auto operatorResult = module->operatorResult();
  QModelIndex index;
  if (moleculeSource) {
    index = moleculeSourceIndex(moleculeSource);
  } else if (operatorResult) {
    index = resultIndex(operatorResult);
  } else if (dataSource) {
    index = dataSourceIndex(dataSource);
  }
  if (index.isValid()) {
    auto dataSourceItem = treeItem(index);
    // Modules straight after the data source so append after any current
    // modules.
    int insertionRow = dataSourceItem->childCount();
    for (int j = 0; j < dataSourceItem->childCount(); ++j) {
      if (!dataSourceItem->child(j)->module()) {
        insertionRow = j;
        break;
      }
    }

    beginInsertRows(index, insertionRow, insertionRow);
    dataSourceItem->insertChild(insertionRow, PipelineModel::Item(module));
    endInsertRows();
  }
  emit moduleItemAdded(module);
}

void PipelineModel::operatorAdded(Operator* op,
                                  DataSource* transformedDataSource)
{
  // Operators are special, they operate on all data and are shown in the
  // visualization modules. So there are some moves necessary to show this.
  Q_ASSERT(op);
  auto dataSource = op->dataSource();
  Q_ASSERT(dataSource);
  connect(op, &Operator::labelModified, this, &PipelineModel::operatorModified);
  connect(op, &Operator::transformingDone, this,
          &PipelineModel::operatorTransformDone);
  connect(op, &Operator::newOutputDataSource, this,
          &PipelineModel::outputDataSourceAdded);
  connect(op, &Operator::dataSourceMoved, this,
          &PipelineModel::dataSourceMoved);
  // Make sure dataChange signal is emitted when operator is complete
  connect(op, &Operator::transformingDone, [this, op]() {
    auto opIndex = operatorIndex(op);
    auto statusIndex = index(opIndex.row(), Column::state, opIndex.parent());
    emit dataChanged(statusIndex, statusIndex);
  });
  // Refresh label column when breakpoint state changes (delegate paints it)
  connect(op, &Operator::breakpointChanged, [this, op]() {
    auto opIndex = operatorIndex(op);
    emit dataChanged(opIndex, opIndex);
  });

  auto index = dataSourceIndex(dataSource);
  auto dataSourceItem = treeItem(index);
  // Find the correct insertion row based on the operator's position in the
  // DataSource's operator list.
  int insertionRow = dataSourceItem->childCount();
  auto operators = dataSource->operators();
  int opIndex = operators.indexOf(op);
  if (opIndex >= 0 && opIndex < operators.size() - 1) {
    // Mid-chain insertion: find the tree item of the next operator and insert
    // before it.
    auto nextOp = operators[opIndex + 1];
    for (int i = 0; i < dataSourceItem->childCount(); ++i) {
      if (dataSourceItem->child(i)->op() == nextOp) {
        insertionRow = i;
        break;
      }
    }
  }
  beginInsertRows(index, insertionRow, insertionRow);
  dataSourceItem->insertChild(insertionRow, PipelineModel::Item(op));
  endInsertRows();

  // Insert operator results in the operator tree item
  auto operatorTreeItem = dataSourceItem->find(op);
  auto operatorIndex = this->operatorIndex(op);
  int numResults = op->numberOfResults();
  if (numResults) {

    beginInsertRows(operatorIndex, 0, numResults - 1);
    for (int j = 0; j < numResults; ++j) {
      OperatorResult* result = op->resultAt(j);
      operatorTreeItem->appendChild(PipelineModel::Item(result));
    }
    endInsertRows();
  }

  if (transformedDataSource) {
    moveDataSourceHelper(transformedDataSource, op);
  }

  emit operatorItemAdded(op);
}

void PipelineModel::operatorRemoved(Operator* op)
{
  removeOp(op);
}

void PipelineModel::operatorModified()
{
  auto op = qobject_cast<Operator*>(sender());
  Q_ASSERT(op);

  auto index = operatorIndex(op);
  dataChanged(index, index);
}

void PipelineModel::operatorTransformDone()
{
  auto op = qobject_cast<Operator*>(sender());
  Q_ASSERT(op);
}

void PipelineModel::dataSourceRemoved(DataSource* source)
{
  auto index = dataSourceIndex(source);

  if (index.isValid()) {
    auto item = treeItem(index);
    beginRemoveRows(parent(index), index.row(), index.row());
    item->remove(source);
    m_treeItems.removeAll(item);
    delete item;
    endRemoveRows();
  }
}

void PipelineModel::moleculeSourceRemoved(MoleculeSource* moleculeSource)
{
  auto index = moleculeSourceIndex(moleculeSource);

  if (index.isValid()) {
    auto item = treeItem(index);
    beginRemoveRows(parent(index), index.row(), index.row());
    item->remove(moleculeSource);
    m_treeItems.removeAll(item);
    delete item;
    endRemoveRows();
  }
}

void PipelineModel::moduleRemoved(Module* module)
{
  auto index = moduleIndex(module);

  if (index.isValid()) {
    beginRemoveRows(parent(index), index.row(), index.row());
    auto item = treeItem(index);
    item->parent()->remove(module);
    endRemoveRows();
  }
}

bool PipelineModel::removeDataSource(DataSource* source)
{
  dataSourceRemoved(source);
  ModuleManager::instance().removeDataSource(source);
  return true;
}

bool PipelineModel::removeMoleculeSource(MoleculeSource* moleculeSource)
{
  moleculeSourceRemoved(moleculeSource);
  ModuleManager::instance().removeMoleculeSource(moleculeSource);
  return true;
}

bool PipelineModel::removeModule(Module* module)
{
  moduleRemoved(module);
  ModuleManager::instance().removeModule(module);
  return true;
}

bool PipelineModel::removeOp(Operator* o)
{
  auto index = operatorIndex(o);
  if (index.isValid()) {
    // We need to do this outside the beginRemoveRow(...), otherwise
    // the model is not correctly invalidated.
    o->dataSource()->removeOperator(o);
    beginRemoveRows(parent(index), index.row(), index.row());
    auto item = treeItem(index);
    item->parent()->remove(o);
    endRemoveRows();

    return true;
  }

  return false;
}

PipelineModel::TreeItem* PipelineModel::treeItem(const QModelIndex& index) const
{
  if (!index.isValid()) {
    return nullptr;
  }

  return static_cast<PipelineModel::TreeItem*>(index.internalPointer());
}

void PipelineModel::outputDataSourceAdded(DataSource* dataSource)
{
  // Find the operator that owns this output data source.
  auto op = qobject_cast<Operator*>(sender());
  if (!op) {
    return;
  }

  auto opIdx = operatorIndex(op);
  if (!opIdx.isValid()) {
    return;
  }

  auto opItem = treeItem(opIdx);
  int insertionRow = opItem->childCount();
  beginInsertRows(opIdx, insertionRow, insertionRow);
  opItem->appendChild(PipelineModel::Item(dataSource));
  endInsertRows();

  emit dataSourceItemAdded(dataSource);
}

void PipelineModel::moveDataSourceHelper(DataSource* dataSource,
                                         Operator* newParent)
{
  if (!dataSource || !newParent) {
    return;
  }

  // Find the data source's current tree item by scanning operator children.
  TreeItem* dsItem = nullptr;
  QModelIndex oldParentIdx;

  foreach (auto topItem, m_treeItems) {
    // Scan all operators under each top-level data source
    for (int i = 0; i < topItem->childCount(); ++i) {
      auto child = topItem->child(i);
      if (!child->op()) {
        continue;
      }
      for (int j = 0; j < child->childCount(); ++j) {
        if (child->child(j)->dataSource() == dataSource) {
          oldParentIdx = createIndex(child->childIndex(), 0, child);
          int pos = j;
          beginRemoveRows(oldParentIdx, pos, pos);
          dsItem = child->detachChild(pos);
          endRemoveRows();
          break;
        }
      }
      if (dsItem) {
        break;
      }
    }
    if (dsItem) {
      break;
    }
  }

  if (!dsItem) {
    return;
  }

  // Now attach it to the new parent operator.
  auto newParentIdx = operatorIndex(newParent);
  if (!newParentIdx.isValid()) {
    delete dsItem;
    return;
  }

  auto newParentItem = treeItem(newParentIdx);
  int newRow = newParentItem->childCount();
  beginInsertRows(newParentIdx, newRow, newRow);
  newParentItem->attach(dsItem);
  endInsertRows();
}

void PipelineModel::dataSourceMoved(DataSource* dataSource)
{
  // Find the operator that now owns this data source.
  auto op = qobject_cast<Operator*>(sender());
  if (!op) {
    return;
  }

  moveDataSourceHelper(dataSource, op);
}

} // namespace tomviz
