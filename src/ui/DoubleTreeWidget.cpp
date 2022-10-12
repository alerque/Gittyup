//
//          Copyright (c) 2020
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Martin Marmsoler
//

#include "ContextMenuButton.h"
#include "DoubleTreeWidget.h"
#include "BlameEditor.h"
#include "DiffTreeModel.h"
#include "FileContextMenu.h"
#include "StatePushButton.h"
#include "TreeProxy.h"
#include "TreeView.h"
#include "ViewDelegate.h"
#include "conf/Settings.h"
#include "DiffView/DiffView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QButtonGroup>

namespace {

const QString kSplitterKey = QString("diffsplitter");
const QString kExpandAll = QString(QObject::tr("Expand all"));
const QString kCollapseAll = QString(QObject::tr("Collapse all"));
const QString kStagedFiles = QString(QObject::tr("Staged Files"));
const QString kUnstagedFiles = QString(QObject::tr("Unstaged Files"));
const QString kCommitedFiles = QString(QObject::tr("Committed Files"));
const QString kAllFiles = QString(QObject::tr("Workdir Files"));

class SegmentedButton : public QWidget {
public:
  SegmentedButton(QWidget *parent = nullptr) : QWidget(parent) {
    mLayout = new QHBoxLayout(this);
    mLayout->setContentsMargins(0, 0, 0, 0);
    mLayout->setSpacing(0);
  }

  void addButton(QAbstractButton *button, const QString &text = QString(),
                 bool checkable = false) {
    button->setToolTip(text);
    button->setCheckable(checkable);

    mLayout->addWidget(button);
    mButtons.addButton(button, mButtons.buttons().size());
  }

  const QButtonGroup *buttonGroup() const { return &mButtons; }

private:
  QHBoxLayout *mLayout;
  QButtonGroup mButtons;
};

} // namespace

DoubleTreeWidget::DoubleTreeWidget(const git::Repository &repo, QWidget *parent)
    : ContentWidget(parent) {
  // first column
  // top (Buttons to switch between Blame editor and DiffView)
  SegmentedButton *segmentedButton = new SegmentedButton(this);
  QPushButton *blameView = new QPushButton(tr("Blame"), this);
  segmentedButton->addButton(blameView, tr("Show Blame Editor"), true);
  QPushButton *diffView = new QPushButton(tr("Diff"), this);
  segmentedButton->addButton(diffView, tr("Show Diff View"), true);

  // Context button.
  ContextMenuButton *contextButton = new ContextMenuButton(this);
  QMenu *contextMenu = new QMenu(this);
  contextButton->setMenu(contextMenu);
  QAction *singleTree = new QAction(tr("Single Tree View"));
  singleTree->setCheckable(true);
  singleTree->setChecked(
      Settings::instance()->value("doubletreeview/single", false).toBool());
  connect(singleTree, &QAction::triggered, this, [this](bool checked) {
    Settings::instance()->setValue("doubletreeview/single", checked);
    RepoView::parentView(this)->refresh();
  });
  QAction *listView = new QAction(tr("List View"));
  listView->setCheckable(true);
  listView->setChecked(
      Settings::instance()->value("doubletreeview/listview", false).toBool());
  connect(listView, &QAction::triggered, this, [this](bool checked) {
    Settings::instance()->setValue("doubletreeview/listview", checked);
    RepoView::parentView(this)->refresh();
  });
  contextMenu->addAction(singleTree);
  contextMenu->addAction(listView);
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  buttonLayout->addWidget(segmentedButton);
  buttonLayout->addStretch();
  buttonLayout->addWidget(contextButton);

  // bottom (Stacked widget with Blame editor and DiffView)
  QVBoxLayout *fileViewLayout = new QVBoxLayout();
  mFileView = new QStackedWidget(this);
  mEditor = new BlameEditor(repo, this);
  mDiffView = new DiffView(repo, this);
  mFileView->addWidget(mEditor);
  mFileView->addWidget(mDiffView);

  fileViewLayout->addLayout(buttonLayout);
  fileViewLayout->addWidget(mFileView);
  mFileView->setCurrentIndex(DoubleTreeWidget::Diff);
  mFileView->show();
  QWidget *fileView = new QWidget(this);
  fileView->setLayout(fileViewLayout);

  // second column
  // staged files
  QVBoxLayout *vBoxLayout = new QVBoxLayout();
  stagedFiles = new TreeView(this, "Staged");
  stagedFiles->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
  stagedFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);
  mDiffTreeModel = new DiffTreeModel(repo, this);
  mDiffView->setModel(mDiffTreeModel);
  auto *repoView = qobject_cast<RepoView *>(parent->parent());
  Q_ASSERT(repoView);
  connect(mDiffTreeModel, &DiffTreeModel::updateSubmodules,
          [repoView](const QList<git::Submodule> &submodules, bool recursive,
                     bool init, bool force_checkout) {
            repoView->updateSubmodules(submodules, recursive, init,
                                       force_checkout);
          });
  TreeProxy *treewrapperStaged = new TreeProxy(true, this);
  treewrapperStaged->setSourceModel(mDiffTreeModel);
  stagedFiles->setModel(treewrapperStaged);
  stagedFiles->setHeaderHidden(true);
  ViewDelegate *stagedDelegate = new ViewDelegate();
  stagedDelegate->setDrawArrow(false);
  stagedFiles->setItemDelegateForColumn(0, stagedDelegate);

  QHBoxLayout *hBoxLayout = new QHBoxLayout();
  QLabel *label = new QLabel(kStagedFiles);
  hBoxLayout->addWidget(label);
  hBoxLayout->addStretch();
  collapseButtonStagedFiles =
      new StatePushButton(kCollapseAll, kExpandAll, this);
  hBoxLayout->addWidget(collapseButtonStagedFiles);

  vBoxLayout->addLayout(hBoxLayout);
  vBoxLayout->addWidget(stagedFiles);
  mStagedWidget = new QWidget();
  mStagedWidget->setLayout(vBoxLayout);

  // unstaged files
  vBoxLayout = new QVBoxLayout();
  unstagedFiles = new TreeView(this, "Unstaged");
  unstagedFiles->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
  unstagedFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);
  TreeProxy *treewrapperUnstaged = new TreeProxy(false, this);
  treewrapperUnstaged->setSourceModel(mDiffTreeModel);
  unstagedFiles->setModel(treewrapperUnstaged);
  unstagedFiles->setHeaderHidden(true);
  ViewDelegate *unstagedDelegate = new ViewDelegate();
  unstagedDelegate->setDrawArrow(false);
  unstagedFiles->setItemDelegateForColumn(0, unstagedDelegate);

  hBoxLayout = new QHBoxLayout();
  mUnstagedCommitedFiles = new QLabel(kUnstagedFiles);
  hBoxLayout->addWidget(mUnstagedCommitedFiles);
  hBoxLayout->addStretch();
  collapseButtonUnstagedFiles =
      new StatePushButton(kCollapseAll, kExpandAll, this);
  hBoxLayout->addWidget(collapseButtonUnstagedFiles);

  vBoxLayout->addLayout(hBoxLayout);
  vBoxLayout->addWidget(unstagedFiles);
  QWidget *unstagedWidget = new QWidget();
  unstagedWidget->setLayout(vBoxLayout);

  // splitter between the staged and unstaged section
  QSplitter *treeViewSplitter = new QSplitter(Qt::Vertical, this);
  treeViewSplitter->setHandleWidth(10);
  treeViewSplitter->addWidget(mStagedWidget);
  treeViewSplitter->addWidget(unstagedWidget);
  treeViewSplitter->setStretchFactor(0, 0);
  treeViewSplitter->setStretchFactor(1, 1);

  // splitter between editor/diffview and TreeViews
  QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setHandleWidth(0);
  splitter->addWidget(fileView);
  splitter->addWidget(treeViewSplitter);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 1);
  connect(splitter, &QSplitter::splitterMoved, this, [splitter] {
    QSettings().setValue(kSplitterKey, splitter->saveState());
  });

  // Restore splitter state.
  splitter->restoreState(QSettings().value(kSplitterKey).toByteArray());

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(splitter);

  setLayout(layout);

  const QButtonGroup *viewGroup = segmentedButton->buttonGroup();
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
  connect(
      viewGroup, QOverload<int>::of(&QButtonGroup::idClicked), this,
      [this](int id) {
        mFileView->setCurrentIndex(id);
        // Change selection mode.
        if (id == Blame) {
          stagedFiles->setSelectionMode(QAbstractItemView::SingleSelection);
          unstagedFiles->setSelectionMode(QAbstractItemView::SingleSelection);
        } else {
          stagedFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);
          unstagedFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);
        }
      });
#else
  connect(
      viewGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
      [this, viewGroup](QAbstractButton *button) {
        mFileView->setCurrentIndex(viewGroup->id(button));
        // Change selection mode.
        if (viewGroup->id(button) == Blame) {
          stagedFiles->setSelectionMode(QAbstractItemView::SingleSelection);
          unstagedFiles->setSelectionMode(QAbstractItemView::SingleSelection);
        } else {
          stagedFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);
          unstagedFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);
        }
      });
#endif

  connect(mDiffTreeModel, &DiffTreeModel::checkStateChanged, this,
          &DoubleTreeWidget::treeModelStateChanged);

  connect(stagedFiles, &TreeView::filesSelected, this,
          &DoubleTreeWidget::filesSelected);
  connect(stagedFiles, &TreeView::collapseCountChanged, this,
          &DoubleTreeWidget::collapseCountChanged);

  connect(unstagedFiles, &TreeView::filesSelected, this,
          &DoubleTreeWidget::filesSelected);
  connect(unstagedFiles, &TreeView::collapseCountChanged, this,
          &DoubleTreeWidget::collapseCountChanged);

  connect(collapseButtonStagedFiles, &StatePushButton::clicked, this,
          &DoubleTreeWidget::toggleCollapseStagedFiles);
  connect(collapseButtonUnstagedFiles, &StatePushButton::clicked, this,
          &DoubleTreeWidget::toggleCollapseUnstagedFiles);

  connect(repo.notifier(), &git::RepositoryNotifier::indexChanged, this,
          [this](const QStringList &paths) { mDiffTreeModel->refresh(paths); });

  RepoView *view = RepoView::parentView(this);
  connect(mEditor, &BlameEditor::linkActivated, view, &RepoView::visitLink);
}

QModelIndex DoubleTreeWidget::selectedIndex() const {
  TreeProxy *proxy = static_cast<TreeProxy *>(stagedFiles->model());
  QModelIndexList indexes = stagedFiles->selectionModel()->selectedIndexes();
  if (!indexes.isEmpty()) {
    return proxy->mapToSource(indexes.first());
  }

  indexes = unstagedFiles->selectionModel()->selectedIndexes();
  proxy = static_cast<TreeProxy *>(unstagedFiles->model());
  if (!indexes.isEmpty()) {
    return proxy->mapToSource(indexes.first());
  }
  return QModelIndex();
}

QList<QModelIndex> DoubleTreeWidget::selectedIndices() const {
  QList<QModelIndex> list;

  TreeProxy *proxy = static_cast<TreeProxy *>(stagedFiles->model());
  QModelIndexList indexes = stagedFiles->selectionModel()->selectedIndexes();
  for (auto index : indexes)
    list.append(proxy->mapToSource(index));

  proxy = static_cast<TreeProxy *>(unstagedFiles->model());
  indexes = unstagedFiles->selectionModel()->selectedIndexes();
  for (auto index : indexes)
    list.append(proxy->mapToSource(index));

  return list;
}

QString DoubleTreeWidget::selectedFile() const {
  QModelIndexList indexes = stagedFiles->selectionModel()->selectedIndexes();
  if (!indexes.isEmpty()) {
    return indexes.first().data(Qt::DisplayRole).toString();
  }

  indexes = unstagedFiles->selectionModel()->selectedIndexes();
  if (!indexes.isEmpty()) {
    return indexes.first().data(Qt::DisplayRole).toString();
  }
  return "";
}

/*!
 * \brief DoubleTreeWidget::setDiff
 * \param diff
 * \param file
 * \param pathspec
 */
void DoubleTreeWidget::setDiff(const git::Diff &diff, const QString &file,
                               const QString &pathspec) {
  Q_UNUSED(file)
  Q_UNUSED(pathspec)

  qDebug() << "DoubleTreeWidget::setDiff: time: " << QDateTime::currentDateTime();

  mDiff = diff;

  // Remember selection.
  storeSelection();

  // Reset model.
  // because of this, the content in the view is shown.
  TreeProxy *proxy = static_cast<TreeProxy *>(unstagedFiles->model());
  DiffTreeModel *model = static_cast<DiffTreeModel *>(proxy->sourceModel());
  model->setDiff(diff);
  // do not expand if to many files exist, it takes really long
  // So do it only when there are less than 100
  if (diff.isValid() && diff.count() < 100)
    unstagedFiles->expandAll();
  else
    unstagedFiles->collapseAll();

  // Single tree & list view.
  bool singleTree =
      Settings::instance()->value("doubletreeview/single", false).toBool();
  bool listView =
      Settings::instance()->value("doubletreeview/listview", false).toBool();

  // Widget modifications.
  model->enableListView(listView);
  stagedFiles->setRootIsDecorated(!listView);
  unstagedFiles->setRootIsDecorated(!listView);
  // mUnstagedCommitedFiles->setVisible(!singleTree);
  collapseButtonStagedFiles->setVisible(!listView);
  collapseButtonUnstagedFiles->setVisible(!listView);

  // If statusDiff, there exist no staged/unstaged, but only
  // the commited files must be shown
  if (!diff.isValid() || diff.isStatusDiff()) {
    mUnstagedCommitedFiles->setText(singleTree ? kAllFiles : kUnstagedFiles);
    if (diff.isValid() && diff.count() < 100)
      stagedFiles->expandAll();
    else
      stagedFiles->collapseAll();

    proxy->enableFilter(!singleTree);
    mStagedWidget->setVisible(!singleTree);
  } else {
    mUnstagedCommitedFiles->setText(kCommitedFiles);
    mStagedWidget->setVisible(false);
  }

  // Clear editor.
  mEditor->clear();

  mDiffView->setDiff(diff);

  // Restore selection.
  if (diff.isValid())
    loadSelection();
}

void DoubleTreeWidget::find() { mEditor->find(); }

void DoubleTreeWidget::findNext() { mEditor->findNext(); }

void DoubleTreeWidget::findPrevious() { mEditor->findPrevious(); }

void DoubleTreeWidget::contextMenuEvent(QContextMenuEvent *event) {
  QStringList files;
  QModelIndexList indexes = unstagedFiles->selectionModel()->selectedIndexes();
  foreach (const QModelIndex &index, indexes)
    files.append(index.data(Qt::EditRole).toString());

  indexes = stagedFiles->selectionModel()->selectedIndexes();
  foreach (const QModelIndex &index, indexes)
    files.append(index.data(Qt::EditRole).toString());

  if (files.isEmpty())
    return;

  RepoView *view = RepoView::parentView(this);
  FileContextMenu menu(view, files);
  menu.exec(event->globalPos());
}

void DoubleTreeWidget::cancelBackgroundTasks() { mEditor->cancelBlame(); }

void DoubleTreeWidget::storeSelection() {
  QModelIndexList indexes = stagedFiles->selectionModel()->selectedIndexes();
  if (!indexes.isEmpty()) {
    mSelectedFile.filename = indexes.first().data(Qt::EditRole).toString();
    mSelectedFile.stagedModel = true;
    return;
  }

  indexes = unstagedFiles->selectionModel()->selectedIndexes();
  if (!indexes.isEmpty()) {
    mSelectedFile.filename = indexes.first().data(Qt::EditRole).toString();
    mSelectedFile.stagedModel = false;
    return;
  }
  mSelectedFile.filename = "";
}

void DoubleTreeWidget::loadSelection() {
  QModelIndex index;
  git::Index::StagedState state;

  if (mSelectedFile.filename != "") {
    index = mDiffTreeModel->index(mSelectedFile.filename);
    state = static_cast<git::Index::StagedState>(
        mDiffTreeModel->data(index, Qt::CheckStateRole).toInt());
  }

  if (!index.isValid() ||
      (mSelectedFile.stagedModel &&
       state == git::Index::StagedState::Unstaged) ||
      (!mSelectedFile.stagedModel &&
       state == git::Index::StagedState::Staged)) {
    mSelectedFile.filename = "";
    if (mDiffTreeModel->rowCount() > 0) {
      index = mDiffTreeModel->index(0, 0);
      git::Index::StagedState s = static_cast<git::Index::StagedState>(
          mDiffTreeModel->data(index, Qt::CheckStateRole).toInt());
      mSelectedFile.stagedModel = (s == git::Index::StagedState::Staged);
    }
  }

  // TODO: problem: if file does not exist anymore, the current index cannot be
  // restored anymore

  mIgnoreSelectionChange = true;
  if (mSelectedFile.stagedModel) {
    TreeProxy *proxy = static_cast<TreeProxy *>(stagedFiles->model());
    index = proxy->mapFromSource(index);
    stagedFiles->selectionModel()->setCurrentIndex(index,
                                                   QItemSelectionModel::Select);
  } else {
    TreeProxy *proxy = static_cast<TreeProxy *>(unstagedFiles->model());
    index = proxy->mapFromSource(index);
    unstagedFiles->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::Select);
  }
  mIgnoreSelectionChange = false;
}

void DoubleTreeWidget::treeModelStateChanged(const QModelIndex &index,
                                             int checkState) {
  Q_UNUSED(index)
  Q_UNUSED(checkState)

  // clear editor and disable diffView when no item is selected
  QModelIndexList stagedSelections =
      stagedFiles->selectionModel()->selectedIndexes();
  if (stagedSelections.count())
    return;

  QModelIndexList unstagedSelections =
      unstagedFiles->selectionModel()->selectedIndexes();
  if (unstagedSelections.count())
    return;

  mDiffView->enable(false);
  mEditor->clear();
}

void DoubleTreeWidget::collapseCountChanged(int count) {
  TreeView *view = static_cast<TreeView *>(QObject::sender());

  if (view == stagedFiles)
    collapseButtonStagedFiles->setState(count == 0);
  else
    collapseButtonUnstagedFiles->setState(count == 0);
}

void DoubleTreeWidget::filesSelected(const QModelIndexList &indexes) {
  if (indexes.isEmpty())
    return;

  QObject *obj = QObject::sender();
  if (obj) {
    TreeView *treeview = static_cast<TreeView *>(obj);
    if (treeview == stagedFiles) {
      unstagedFiles->deselectAll();
      stagedFiles->setFocus();
    } else if (treeview == unstagedFiles) {
      stagedFiles->deselectAll();
      unstagedFiles->setFocus();
    }
  }
  loadEditorContent(indexes);
}

void DoubleTreeWidget::loadEditorContent(const QModelIndexList &indexes) {
  QString name;
  git::Blob blob;
  git::Commit commit;

  if (indexes.count() == 1) {
    RepoView *view = RepoView::parentView(this);
    name = indexes.first().data(Qt::EditRole).toString();
    QList<git::Commit> commits = view->commits();
    commit = !commits.isEmpty() ? commits.first() : git::Commit();
    int idx = mDiff.isValid() ? mDiff.indexOf(name) : -1;
    blob = idx < 0 ? git::Blob()
                   : view->repo().lookupBlob(mDiff.id(idx, git::Diff::NewFile));
  }

  mEditor->load(name, blob, commit);

  mDiffView->enable(true);
  mDiffView->updateFiles();
}

void DoubleTreeWidget::toggleCollapseStagedFiles() {
  if (collapseButtonStagedFiles->toggleState())
    stagedFiles->expandAll();
  else
    stagedFiles->collapseAll();
}

void DoubleTreeWidget::toggleCollapseUnstagedFiles() {
  if (collapseButtonUnstagedFiles->toggleState())
    unstagedFiles->expandAll();
  else
    unstagedFiles->collapseAll();
}
