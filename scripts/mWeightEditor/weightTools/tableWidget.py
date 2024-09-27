from __future__ import absolute_import

from Qt import QtGui, QtCore, QtWidgets
from functools import partial
from maya import cmds, mel
from six.moves import range
import numpy as np
from .skinData import DataOfSkin


class TableModel(QtCore.QAbstractTableModel):
    def __init__(self, parent=None, *args):
        super(TableModel, self).__init__(parent)
        self.weightEditorWidget = parent
        self.datatable = None
        self.brownBrush = QtGui.QBrush(QtGui.QColor(130, 130, 90))
        self.greyBrush = QtGui.QBrush(QtGui.QColor(140, 140, 140))
        self.greyDarkerBrush = QtGui.QBrush(QtGui.QColor(80, 80, 80))
        self.sumBrush = QtGui.QBrush(QtGui.QColor(100, 100, 100))
        self.redBrush = QtGui.QBrush(QtGui.QColor(150, 100, 100))
        self.whiteBrush = QtGui.QBrush(QtGui.QColor(200, 200, 200))

    def update(self, dataIn):
        # dataIn (DataAbstract)
        self.datatable = dataIn

    def rowCount(self, parent=None):
        assert self.datatable is not None
        return self.datatable.rowCount

    def columnCount(self, parent=None):
        assert self.datatable is not None
        if isinstance(self.datatable, DataOfSkin):
            return self.datatable.columnCount + 1
        else:
            return self.datatable.columnCount

    def columnNames(self):
        assert self.datatable is not None
        return self.datatable.shortColumnsNames

    def fullColumnNames(self):
        assert self.datatable is not None
        return self.datatable.columnsNames

    def data(self, index, role=QtCore.Qt.DisplayRole):
        try:
            if role == QtCore.Qt.DisplayRole:
                return "{0:g}".format(round(self.realData(index) * 100, 1))
            elif role == QtCore.Qt.EditRole:
                ff = self.realData(index) * 100
                return "{0:.3f}".format(ff).rstrip("0") + "0"[0 : (ff % 1 == 0)]
            elif role == QtCore.Qt.TextAlignmentRole:
                return QtCore.Qt.AlignCenter
            elif role == QtCore.Qt.BackgroundRole:
                if self.isSumColumn(index):
                    return (
                        self.sumBrush
                        if round(self.realData(index) * 100, 1) == 100
                        else self.redBrush
                    )
                elif self.isLocked(index):
                    return self.greyBrush
                elif self.realData(index) != 0.0:
                    return self.brownBrush
            elif role == QtCore.Qt.ForegroundRole:
                if self.isSumColumn(index):
                    return self.whiteBrush
                elif self.isLocked(index):
                    return self.greyDarkerBrush
            else:
                return None
        except Exception:
            if self.weightEditorWidget is not None:
                self.weightEditorWidget.deselectAll()

    def setData(self, index, value, role=QtCore.Qt.EditRole):
        # now set the value
        if self.weightEditorWidget is not None:
            self.weightEditorWidget.prepareToSetValue()
            self.weightEditorWidget.doAddValue(value / 100.0, forceAbsolute=True)
            self.weightEditorWidget.postSetValue()
        return True

    def isLocked(self, index):
        assert self.datatable is not None
        row = index.row()
        column = index.column()
        return self.datatable.isLocked(row, column)

    def realData(self, index):
        assert self.datatable is not None
        row = index.row()
        column = index.column()
        return self.datatable.getValue(row, column)

    def isSumColumn(self, index):
        assert self.datatable is not None
        column = index.column()
        return isinstance(self.datatable, DataOfSkin) and column >= self.datatable.nbDrivers

    def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
        if role == QtCore.Qt.DisplayRole:
            assert self.datatable is not None
            if orientation == QtCore.Qt.Horizontal:
                return self.datatable.columnsNames[section]
            else:
                return self.datatable.rowText[section]
        elif role == QtCore.Qt.TextAlignmentRole:
            return QtCore.Qt.AlignCenter
        else:
            return None

    def getColumnText(self, col):
        assert self.datatable is not None
        try:
            return self.datatable.shortColumnsNames[col]
        except Exception:
            return "total"

    def getRowText(self, row):
        assert self.datatable is not None
        return self.datatable.rowText[row]

    def getColumnSide(self, col):
        assert self.datatable is not None
        try:
            driverName = self.datatable.columnsNames[col]
            for letter in "LRM":
                for sub in ["", "Bk", "Fr", "T", "B"]:
                    if "_{}{}_".format(letter, sub) in driverName:
                        return letter
            return "X"
        except Exception:
            return "X"

    def isSoftOn(self):
        assert self.datatable is not None
        return self.datatable.softOn

    def flags(self, index):
        assert self.datatable is not None
        try:
            if not index.isValid():
                return QtCore.Qt.ItemIsEnabled
            column = index.column()
            if isinstance(self.datatable, DataOfSkin) and column == self.datatable.nbDrivers:
                result = QtCore.Qt.ItemIsEnabled
            elif self.isLocked(index):
                result = QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable
            else:
                result = (
                    QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable | QtCore.Qt.ItemIsEditable
                )
            return QtCore.Qt.ItemFlags(result)
        except Exception:
            if self.weightEditorWidget is not None:
                self.weightEditorWidget.deselectAll()
            return QtCore.Qt.ItemIsEnabled


class HighlightDelegate(QtWidgets.QStyledItemDelegate):
    def createEditor(self, parent, option, index):
        editor = QtWidgets.QDoubleSpinBox(parent)
        editor.setStyleSheet("QDoubleSpinBox { background-color: yellow; color : black; }")
        editor.setMaximum(100)
        editor.setMinimum(0)
        editor.setMinimumWidth(50)
        editor.setButtonSymbols(QtWidgets.QAbstractSpinBox.NoButtons)
        return editor

    def paint(self, painter, option, index):
        if not index.isValid():
            return super(HighlightDelegate, self).paint(painter, option, index)
        model = index.model()
        assert isinstance(model, TableModel)
        realData = model.realData(index)
        if realData == 0.00:
            return super(HighlightDelegate, self).paint(painter, option, index)

        styleOption = QtWidgets.QStyleOptionViewItem(option)
        pal = styleOption.palette
        pal.setColor(
            pal.currentColorGroup(),
            QtGui.QPalette.Highlight,
            QtGui.QColor(140, 140, 235),
        )

        return super(HighlightDelegate, self).paint(painter, styleOption, index)


class VertHeaderView(QtWidgets.QHeaderView):
    def __init__(self, mainWindow=None, parent=None):
        super(VertHeaderView, self).__init__(QtCore.Qt.Vertical, parent)
        self.mainWindow = mainWindow
        self.setMinimumWidth(20)

        self.setSectionsClickable(True)
        self.setHighlightSections(True)
        self.setSectionResizeMode(QtWidgets.QHeaderView.Fixed)

        self.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self.showMenu)

        self.whiteCol = QtGui.QColor(200, 200, 200)
        self.regularCol = QtGui.QColor(130, 130, 130)

        self.regularBG = QtGui.QBrush(QtGui.QColor(130, 130, 130))
        self.whiteBG = QtGui.QBrush(QtGui.QColor(200, 200, 200))
        self.greyBG = QtGui.QBrush(QtGui.QColor(100, 100, 100))

    def showMenu(self, pos):
        popMenu = QtWidgets.QMenu(self)
        selectionIsEmpty = self.selectionModel().selection().isEmpty()

        selAction = popMenu.addAction("select vertices")
        selAction.triggered.connect(self.selectVerts)
        selAction.setEnabled(not selectionIsEmpty)
        popMenu.addSeparator()

        lockAction = popMenu.addAction("lock selected")
        lockAction.triggered.connect(self.lockSelectedRows)
        lockAction.setEnabled(not selectionIsEmpty)

        lockAllButSelAction = popMenu.addAction("lock all but selected")
        lockAllButSelAction.triggered.connect(self.lockAllButSelectedRows)
        lockAllButSelAction.setEnabled(not selectionIsEmpty)

        unlockAction = popMenu.addAction("unlock selected")
        unlockAction.triggered.connect(self.unlockSelectedRows)
        unlockAction.setEnabled(not selectionIsEmpty)

        highliteAction = popMenu.addAction("highlite lock Verts")
        highliteAction.triggered.connect(self.highliteLockRows)

        clearLocksAction = popMenu.addAction("clear all Locks")
        clearLocksAction.triggered.connect(self.clearLocks)
        popMenu.exec_(self.mapToGlobal(pos))

    def paintSection(self, painter, rect, index):
        if not rect.isValid():
            return
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None

        text = model.getRowText(index)
        multVal = model.datatable.verticesWeight[index]
        painter.save()
        theBGBrush = self.greyBG

        if not model.datatable.isRowLocked(index):
            if model.isSoftOn():
                col = int(multVal * 255 * 2)
                if col > 255:
                    RCol = 255
                    GCol = col - 255
                else:
                    GCol = 0
                    RCol = col
                theBGBrush = QtGui.QBrush(QtGui.QColor(RCol, GCol, 0, 100))
            else:
                theBGBrush = self.regularBG

        painter.setBrush(theBGBrush)
        pen = QtGui.QPen(QtGui.QColor(0, 0, 0))
        pen.setWidth(2)
        painter.setPen(pen)
        painter.drawRect(rect)
        painter.restore()
        painter.drawText(rect, QtCore.Qt.AlignCenter, text)

    def getSelectedRows(self):
        sel = self.selectionModel().selection()
        chunks = np.array([], dtype=int)
        for item in sel:
            chunks = np.union1d(chunks, list(range(item.top(), item.bottom() + 1)))

        return chunks

    def selectVerts(self):
        selectedIndices = self.getSelectedRows()
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        model.datatable.selectVerts(selectedIndices)

    def highliteLockRows(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        modData = model.datatable
        newSel = self.selectionModel().selection()
        newSel.clear()
        nbColumns = modData.columnCount
        for row in range(self.count()):
            if modData.vertices[row] in modData.lockedVertices:
                newSel.select(model.index(row, 0), model.index(row, nbColumns - 1))

        self.selectionModel().select(newSel, QtCore.QItemSelectionModel.ClearAndSelect)

    def lockSelectedRows(self):
        selectedIndices = self.getSelectedRows()
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        model.datatable.lockRows(selectedIndices)

    def lockAllButSelectedRows(self):
        selectedIndices = set(range(self.count()))
        selectedIndices.difference_update(self.getSelectedRows())
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        model.datatable.lockRows(selectedIndices)

    def unlockSelectedRows(self):
        selectedIndices = self.getSelectedRows()
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        model.datatable.unLockRows(selectedIndices)

    def clearLocks(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        model.datatable.unLockRows(list(range(self.count())))


class HorizHeaderView(QtWidgets.QHeaderView):
    def getColors(self):
        self._colors = []
        for i in range(1, 9):
            col = cmds.displayRGBColor("userDefined{0}".format(i), q=True)
            self._colors.append([int(el * 255) for el in col])

    def __init__(self, mainWindow=None, colWidth=10, parent=None):
        super(HorizHeaderView, self).__init__(QtCore.Qt.Horizontal, parent)

        self.mainWindow = mainWindow
        self.getColors()
        self.colWidth = colWidth
        self._font = QtGui.QFont("Myriad Pro", 10)
        self._font.setBold(False)
        self._metrics = QtGui.QFontMetrics(self._font)
        self._descent = self._metrics.descent()
        self._margin = 5
        self._colorDrawHeight = 20
        self.setSectionsClickable(True)
        self.setHighlightSections(True)
        self.setSectionResizeMode(QtWidgets.QHeaderView.Fixed)

        self.letVerticesDraw = True

        self.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self.showMenu)

        self.regularBG = QtGui.QBrush(QtGui.QColor(130, 130, 130))
        self.greyBG = QtGui.QBrush(QtGui.QColor(100, 100, 100))

        self.blueBG = QtGui.QBrush(QtGui.QColor(112, 124, 137))
        self.redBG = QtGui.QBrush(QtGui.QColor(134, 119, 127))
        self.yellowBG = QtGui.QBrush(QtGui.QColor(144, 144, 122))

    def mouseDoubleClickEvent(self, event):
        if self.height() - event.pos().y() < 20:
            index = self.visualIndexAt(event.pos().x())

            pos = event.globalPos() - QtCore.QPoint(355, 100)
            theColor = [int(el / 255.0) for el in self.color(index)]
            cmds.colorEditor(mini=True, position=[pos.x(), pos.y()], rgbValue=theColor)
            if cmds.colorEditor(query=True, result=True, mini=True):
                col = cmds.colorEditor(query=True, rgb=True)
                model = self.model()
                assert isinstance(model, TableModel)
                influence = model.fullColumnNames()[index]
                cmds.setAttr(influence + ".wireColorRGB", *col)
                self.repaint()
                if self.mainWindow is not None:
                    self.mainWindow.refreshPaintEditor()
        else:
            super(HorizHeaderView, self).mouseDoubleClickEvent(event)

    def mousePressEvent(self, event):
        index = self.visualIndexAt(event.pos().x())
        outClick = index == -1
        if outClick:
            if event.button() == QtCore.Qt.MidButton:
                if self.mainWindow is not None:
                    self.mainWindow.resizeToMinimum()
            elif event.button() == QtCore.Qt.LeftButton:
                self.parent().clearSelection()
        else:
            self.letVerticesDraw = False
            super(HorizHeaderView, self).mousePressEvent(event)

    def mouseReleaseEvent(self, event):
        self.letVerticesDraw = True
        super(HorizHeaderView, self).mouseReleaseEvent(event)

    def color(self, ind):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None

        if not isinstance(model.datatable, DataOfSkin):
            return [255, 155, 55]

        colName = model.fullColumnNames()[ind]

        wireColor = cmds.getAttr(colName + ".wireColorRGB")[0]
        if wireColor == (0.0, 0.0, 0.0):
            objColor = cmds.getAttr(colName + ".objectColor")
            wireColor = cmds.displayRGBColor("userDefined{0}".format(objColor + 1), query=True)

        ret = [int(255 * el) for el in wireColor]
        return ret

    def setColor(self, pos, index):
        menu = ColorMenu(self)
        pos = self.mapToGlobal(pos)
        menu.exec_(pos)
        color = menu.color()
        if color is None:
            return
        else:
            model = self.model()
            assert isinstance(model, TableModel)
            cmds.setAttr(model.fullColumnNames()[index] + ".objectColor", color)

    def getSelectedColumns(self):
        sel = self.selectionModel().selection()
        chunks = np.array([], dtype=int)
        for item in sel:
            chunks = np.union1d(chunks, list(range(item.left(), item.right() + 1)))

        selectedIndices = [indCol for indCol in chunks if not self.isSectionHidden(indCol)]

        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None

        if isinstance(model.datatable, DataOfSkin):
            lastCol = self.count() - 1
            if lastCol in selectedIndices:
                selectedIndices.remove(lastCol)
        return selectedIndices

    def lockSelectedColumns(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        selectedIndices = self.getSelectedColumns()
        model.datatable.lockColumns(selectedIndices)
        if self.mainWindow is not None:
            self.mainWindow.refreshPaintEditor()

    def lockAllButSelectedColumns(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        selectedIndices = set(range(self.count() - 1))
        model.datatable.unLockColumns(selectedIndices)
        selectedIndices.difference_update(self.getSelectedColumns())
        model.datatable.lockColumns(selectedIndices)
        if self.mainWindow is not None:
            self.mainWindow.refreshPaintEditor()

    def unlockSelectedColumns(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        selectedIndices = self.getSelectedColumns()
        model.datatable.unLockColumns(selectedIndices)
        if self.mainWindow is not None:
            self.mainWindow.refreshPaintEditor()

    def selectDeformers(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        selectedIndices = self.getSelectedColumns()
        model.datatable.selectDeformers(selectedIndices)

    def displayVertices(self, doSelect=True):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        selectedColumns = self.getSelectedColumns()
        model.datatable.selectVertsOfColumns(selectedColumns, doSelect=doSelect)

    def clearLocks(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        model.datatable.unLockColumns(list(range(self.count() - 1)))
        if self.mainWindow is not None:
            self.mainWindow.refreshPaintEditor()

    def enterPaintAttribute(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        selectedColumns = self.getSelectedColumns()
        colIndex = selectedColumns.pop()
        theAtt = model.datatable.attributesToPaint[model.datatable.shortColumnsNames[colIndex]]
        mel.eval('artSetToolAndSelectAttr( "artAttrCtx", "{}" );'.format(theAtt))
        # print theIndex

    def showMenu(self, pos):
        popMenu = QtWidgets.QMenu(self)
        selectionIsEmpty = self.selectionModel().selection().isEmpty()

        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        if isinstance(model.datatable, DataOfSkin):
            selAction = popMenu.addAction("select deformers")
            selAction.triggered.connect(self.selectDeformers)
            selAction.setEnabled(not selectionIsEmpty)

        selVertices = popMenu.addAction("select vertices")
        selVertices.triggered.connect(partial(self.displayVertices, True))
        selVertices.setEnabled(not selectionIsEmpty)

        if isinstance(model.datatable, DataOfSkin):
            popMenu.addSeparator()

            lockAction = popMenu.addAction("lock selected")
            lockAction.triggered.connect(self.lockSelectedColumns)
            lockAction.setEnabled(not selectionIsEmpty)

            lockAllButSelAction = popMenu.addAction("lock all but selected")
            lockAllButSelAction.triggered.connect(self.lockAllButSelectedColumns)
            lockAllButSelAction.setEnabled(not selectionIsEmpty)

            unlockAction = popMenu.addAction("unlock selected")
            unlockAction.triggered.connect(self.unlockSelectedColumns)
            unlockAction.setEnabled(not selectionIsEmpty)

            clearLocksAction = popMenu.addAction("clear all Locks")
            clearLocksAction.triggered.connect(self.clearLocks)

            hideColumnIndices = model.datatable.hideColumnIndices
            columnNames = model.columnNames()
            popMenu.addSeparator()
            hideZeroColumnsAction = popMenu.addAction("hide zero columns")
            hideZeroColumnsAction.setCheckable(True)
            if self.mainWindow is not None:
                hideZeroColumnsAction.setChecked(self.mainWindow.hideZeroColumn)
                hideZeroColumnsAction.toggled.connect(self.mainWindow.toggleZeroColumn)

            subMenuFollow = popMenu.addMenu("show Columns")
            for ind in hideColumnIndices:
                chbox = QtWidgets.QCheckBox(columnNames[ind], subMenuFollow)

                chbox.setChecked(not self.isSectionHidden(ind))
                chbox.toggled.connect(partial(self.toggledColumn, ind, columnNames[ind]))

                checkableAction = QtWidgets.QWidgetAction(subMenuFollow)
                checkableAction.setDefaultWidget(chbox)
                subMenuFollow.addAction(checkableAction)

        else:
            paintAttr = popMenu.addAction("paint attribute")
            paintAttr.triggered.connect(self.enterPaintAttribute)
            paintAttr.setEnabled(not selectionIsEmpty)

        popMenu.exec_(self.mapToGlobal(pos))

    def toggledColumn(self, ind, ColumnName, checked):
        par = self.parent()
        assert isinstance(par, QtWidgets.QTableView)

        if not checked:
            par.hideColumn(ind)
        else:
            par.showColumn(ind)

    def paintSection(self, painter, rect, index):
        # https://github.com/openwebos/qt/blob/master/src/gui/itemviews/qheaderview.cpp
        if not rect.isValid():
            return

        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None

        isLastColumn = (
            isinstance(model.datatable, DataOfSkin) and index >= model.datatable.nbDrivers
        )
        data = self._get_data(index)

        if isLastColumn:
            painter.save()
            painter.setBrush(self.greyBG)
            pen = QtGui.QPen(QtGui.QColor(0, 0, 0))
            pen.setWidth(2)
            painter.setPen(pen)
            painter.drawRect(rect)
            painter.restore()

            painter.setPen(QtGui.QColor(200, 200, 200))
            painter.drawText(rect, QtCore.Qt.AlignCenter, data)
        else:
            isBold = False
            par = self.parent()
            assert isinstance(par, QtWidgets.QTableView)
            sel = par.selectionModel().selection()
            for item in sel:
                isBold = item.left() <= index <= item.right()
                if isBold:
                    break

            self._font.setBold(isBold)
            painter.setFont(self._font)
            painter.rotate(-90)
            x = -rect.height()
            y = rect.left()

            side = model.getColumnSide(index)
            defaultBGInd = "RLMX".index(side)
            defaultBG = [self.blueBG, self.redBG, self.yellowBG, self.regularBG][defaultBGInd]

            theBGBrush = self.greyBG if model.datatable.isColumnLocked(index) else defaultBG

            painter.setBrush(theBGBrush)
            painter.drawRect(x + 1, y - 1, rect.height() - 1, rect.width())

            theColor = self.color(index)
            painter.setBrush(QtGui.QBrush(QtGui.QColor(*theColor)))
            painter.drawRect(x + 1, y - 1, 20, rect.width())

            painter.drawText(
                int(-rect.height() + self._margin + self._colorDrawHeight),
                int(rect.left() + (rect.width() + self._descent) // 2),
                data,
            )

    def sizeHint(self):
        return QtCore.QSize(10, self._get_text_width() + 2 * self._margin + self._colorDrawHeight)

    def _get_text_width(self):
        model = self.model()
        assert isinstance(model, TableModel)
        colNames = model.columnNames()
        allMetrics = [self._metrics.width(colName) for colName in colNames]
        if allMetrics:
            return max(allMetrics) + 15
        else:
            return 50

    def _get_data(self, index):
        model = self.model()
        assert isinstance(model, TableModel)
        return model.getColumnText(index)


class FastTableView(QtWidgets.QTableView):
    """
    A simple table to demonstrate the QComboBox delegate.
    """

    selEmptied = QtCore.Signal(bool, name="selEmptied")

    def __init__(self, parent, colWidth=10):
        self.ignoreReselect = False

        colWidth = colWidth  # kwargs.pop('colWidth', None)
        QtWidgets.QTableView.__init__(self, parent)
        self.mainWindow = parent
        self._hd = HighlightDelegate(self)
        self.setItemDelegate(self._hd)
        self.HHeaderView = HorizHeaderView(self.mainWindow, colWidth)
        self.VHeaderView = VertHeaderView(self.mainWindow)

        self.setHorizontalHeader(self.HHeaderView)
        self.setVerticalHeader(self.VHeaderView)

        self.verticalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Fixed)

        self._font = QtGui.QFont("Myriad Pro", 10)
        self._font.setBold(False)
        self._metrics = QtGui.QFontMetrics(self._font)
        self._descent = self._metrics.descent()
        self._margin = 5
        self._colorDrawHeight = 20
        self.regularBG = QtGui.QBrush(QtGui.QColor(130, 130, 130))

        self.__nw_heading = "Vtx"
        self.addRedrawButton()

    def keyPressEvent(self, event):
        txt = event.text()
        isIn = txt and txt in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
        if isIn:
            return

        super(FastTableView, self).keyPressEvent(event)

    def rmvRedrawButton(self):
        btn = self.findChild(QtWidgets.QAbstractButton)
        btn.removeEventFilter(self)
        self.repaint()

    def addRedrawButton(self):
        btn = self.findChild(QtWidgets.QAbstractButton)
        btn.setText(self.__nw_heading)
        btn.setToolTip("Toggle selecting all table cells")
        btn.installEventFilter(self)
        opt = QtWidgets.QStyleOptionHeader()
        opt.text = btn.text()
        s = QtCore.QSize(
            btn.style()
            .sizeFromContents(QtWidgets.QStyle.CT_HeaderSection, opt, QtCore.QSize(), btn)
            .expandedTo(QtWidgets.QApplication.globalStrut())
        )

        if s.isValid():
            self.verticalHeader().setMinimumWidth(s.width())
        self.repaint()

    def selectionChanged(self, selected, deselected):
        super(FastTableView, self).selectionChanged(selected, deselected)
        if not self.ignoreReselect:
            sel = self.selectionModel().selection()
            if self.HHeaderView.letVerticesDraw:
                rowsSel = []
                for item in sel:
                    rowsSel += list(range(item.top(), item.bottom() + 1))

                model = self.model()
                assert isinstance(model, TableModel)
                assert model.datatable is not None
                model.datatable.updateDisplayVerts(rowsSel)
            else:
                self.HHeaderView.displayVertices(doSelect=False)

            self.selEmptied.emit(not sel.isEmpty())

    def createPixMap(self, rect):
        thePixmap = QtGui.QPixmap(500, 500)
        painter = QtGui.QPainter()
        painter.begin(thePixmap)

        painter.rotate(-90)
        x = -rect.height()
        y = rect.left()

        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        data = model.datatable.shapeShortName

        painter.setBrush(self.regularBG)
        painter.setFont(self._font)
        painter.drawRect(x + 1, y - 1, rect.height() - 1, rect.width())

        painter.drawText(
            self._margin - rect.height(),
            rect.left() + (rect.width() + self._descent) // 2,
            data,
        )
        painter.end()

        return thePixmap

    def eventFilter(self, obj, event):
        try:
            if event.type() != QtCore.QEvent.Paint or not isinstance(
                obj, QtWidgets.QAbstractButton
            ):
                return False
        except Exception:
            return False

        # Paint by hand (borrowed from QTableCornerButton)
        opt = QtWidgets.QStyleOptionHeader()
        opt.initFrom(obj)
        styleState = QtWidgets.QStyle.State_None
        if obj.isEnabled():
            styleState |= QtWidgets.QStyle.State_Enabled
        if obj.isActiveWindow():
            styleState |= QtWidgets.QStyle.State_Active
        if obj.isDown():
            styleState |= QtWidgets.QStyle.State_Sunken
        opt.state = styleState
        opt.rect = obj.rect()
        opt.position = QtWidgets.QStyleOptionHeader.OnlyOneSection
        painter = QtWidgets.QStylePainter(obj)
        painter.drawItemPixmap(opt.rect, 1, self.createPixMap(opt.rect))
        painter.drawControl(QtWidgets.QStyle.CE_Header, opt)

        return True


#
# COLOR
#
class ColorMenu(QtWidgets.QMenu):
    def __init__(self, parent):
        super(ColorMenu, self).__init__(parent)
        self.getColors()
        self._color = None

        self.setFixedWidth(20)

        for index, color in enumerate(self._colors):
            pixmap = QtGui.QPixmap(12, 12)
            pixmap.fill(QtGui.QColor(*color))
            act = self.addAction("")
            act.setIcon(QtGui.QIcon(pixmap))
            act.triggered.connect(partial(self.pickColor, index))

    def getColors(self):
        self._colors = []
        for i in range(1, 9):
            col = cmds.displayRGBColor("userDefined{0}".format(i), q=True)
            self._colors.append([int(el * 255) for el in col])

    def pickColor(self, index):
        self._color = index

    def color(self):
        return self._color
