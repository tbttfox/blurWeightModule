from __future__ import absolute_import, division

from Qt import QtGui, QtCore, QtWidgets
from functools import partial
from maya import cmds, mel
import numpy as np
import string
from .skinData import DataOfSkin
from six.moves import range


TOL = 1e-5


def _autoProp(name, default=None, typ=QtGui.QColor):
    """A convenience function that gets/sets properties in a dictionary on a class
    This lets me set up a bunch of QtCore.Property objects without having
    to define getter/setter methods for each property
    """
    if default is None:
        default = typ()
    return QtCore.Property(
        typ,
        lambda obj: obj._multiStore.get(name, default),
        lambda obj, val: obj._multiStore.update({name: val}),
    )


class TableModel(QtCore.QAbstractTableModel):
    def __init__(self, parent=None, *args):
        super(TableModel, self).__init__(parent)
        self.weightEditorWidget = parent
        self.datatable = None

    def update(self, dataIn):
        self.datatable = dataIn

    def rowCount(self, parent=None):
        if parent is None:
            parent = QtCore.QModelIndex()
        if self.datatable is None:
            return 0
        return self.datatable.rowCount

    def columnCount(self, parent=None):
        if parent is None:
            parent = QtCore.QModelIndex()
        if self.datatable is None:
            return 0
        elif isinstance(self.datatable, DataOfSkin):
            return self.datatable.columnCount + 1
        else:
            return self.datatable.columnCount

    def columnNames(self):
        if self.datatable is None:
            return []
        return self.datatable.shortColumnsNames

    def fullColumnNames(self):
        if self.datatable is None:
            return []
        return self.datatable.columnsNames

    def data(self, index, role=QtCore.Qt.DisplayRole):
        if role == QtCore.Qt.DisplayRole:
            return "{0:g}".format(round(self.realData(index) * 100, 1))
        elif role == QtCore.Qt.EditRole:
            return self.realData(index) * 100
        elif role == QtCore.Qt.TextAlignmentRole:
            return QtCore.Qt.AlignCenter
        return None

    def setData(self, index, value, role=QtCore.Qt.EditRole):
        if role == QtCore.Qt.EditRole:
            par = self.parent()
            par.prepareToSetValue()
            par.doAddValue(value / 100, forceAbsolute=True)
            par.postSetValue()
            return True
        return False

    def isLocked(self, index):
        if self.datatable is None:
            return False
        row = index.row()
        column = index.column()
        return self.datatable.isLocked(row, column)

    def realData(self, index):
        if self.datatable is None:
            return 0.0
        row = index.row()
        column = index.column()
        return self.datatable.getValue(row, column)

    def isSumColumn(self, index):
        if self.datatable is None:
            return False
        column = index.column()
        return isinstance(self.datatable, DataOfSkin) and column >= self.datatable.nbDrivers

    def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
        if role == QtCore.Qt.DisplayRole:
            if self.datatable is None:
                return ""
            elif orientation == QtCore.Qt.Horizontal:
                return self.datatable.columnsNames[section]
            else:
                return self.datatable.rowText[section]
        elif role == QtCore.Qt.TextAlignmentRole:
            return QtCore.Qt.AlignCenter
        else:
            return None

    def getColumnText(self, col):
        if self.datatable is None:
            return ""
        if col == len(self.datatable.shortColumnsNames):
            return "total"
        return self.datatable.shortColumnsNames[col]

    def getRowText(self, row):
        if self.datatable is None:
            return ""
        return self.datatable.rowText[row]

    def getColumnSide(self, col):
        if self.datatable is None:
            return "X"

        if col == len(self.datatable.shortColumnsNames):
            return "X"

        driverName = self.datatable.columnsNames[col]
        for letter in "LRM":
            for sub in ["", "Bk", "Fr", "T", "B"]:
                if "_{}{}_".format(letter, sub) in driverName:
                    return letter
        return "X"

    def isSoftOn(self):
        if self.datatable is None:
            return False
        return self.datatable.softOn

    def flags(self, index):
        if self.datatable is None:
            return QtCore.Qt.ItemIsEnabled

        if not index.isValid():
            return QtCore.Qt.ItemIsEnabled
        column = index.column()
        if isinstance(self.datatable, DataOfSkin) and column == self.datatable.nbDrivers:
            result = QtCore.Qt.ItemIsEnabled
        elif self.isLocked(index):
            result = QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable
        else:
            result = QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable | QtCore.Qt.ItemIsEditable
        return QtCore.Qt.ItemFlags(result)


class HighlightDelegate(QtWidgets.QStyledItemDelegate):
    def createEditor(self, parent, option, index):
        editor = QtWidgets.QDoubleSpinBox(parent)
        editor.setMaximum(100)
        editor.setMinimum(0)
        editor.setMinimumWidth(50)
        editor.setButtonSymbols(QtWidgets.QAbstractSpinBox.NoButtons)
        return editor

    def setEditorData(self, editor, index):
        editor.setValue(index.data(role=QtCore.Qt.EditRole))

    def initStyleOption(self, option, index):
        super(HighlightDelegate, self).initStyleOption(option, index)
        if not index.isValid():
            return

        model = index.model()
        pal = option.palette
        view = self.parent()

        realData = model.realData(index)
        isZero = np.isclose(realData, 0.0, atol=TOL)
        isOne = np.isclose(realData, 1.0, atol=TOL)

        hilightColor = pal.color(QtGui.QPalette.Highlight)

        if model.isSumColumn(index):
            bgColor = view.sumColumnBG if isOne else view.sumColumnERROR
            fgColor = view.sumColumnFG
        elif model.isLocked(index):
            bgColor = view.lockedBG
            fgColor = view.lockedFG
        else:
            if not isZero:
                bgColor = view.nonzeroBG
                fgColor = view.nonzeroFG
                hilightColor = view.nonzeroHI
            else:
                bgColor = view.zeroBG
                fgColor = view.zeroFG
                hilightColor = view.zeroHI

        pal.setColor(QtGui.QPalette.Background, bgColor)
        pal.setColor(QtGui.QPalette.Foreground, fgColor)
        pal.setColor(QtGui.QPalette.Text, fgColor)
        pal.setColor(QtGui.QPalette.WindowText, fgColor)
        pal.setColor(QtGui.QPalette.Highlight, hilightColor)
        option.backgroundBrush = QtGui.QBrush(pal.color(QtGui.QPalette.Background))


class VertHeaderView(QtWidgets.QHeaderView):
    regularBG = _autoProp("regularBG")
    greyBG = _autoProp("greyBG")
    sepCOL = _autoProp("sepCOL")

    def __init__(self, mainWindow=None, parent=None):
        self._multiStore = {}
        super(VertHeaderView, self).__init__(QtCore.Qt.Vertical, parent)
        self.mainWindow = mainWindow
        self.setMinimumWidth(20)

        self.setSectionsClickable(True)
        self.setHighlightSections(True)
        self.setSectionResizeMode(QtWidgets.QHeaderView.Fixed)

        self.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self.showMenu)

    def showMenu(self, pos):
        popMenu = QtWidgets.QMenu(self)
        selectionIsEmpty = self.selectionModel().selection().isEmpty()

        selAction = popMenu.addAction("Select Vertices")
        selAction.triggered.connect(self.selectVerts)
        selAction.setEnabled(not selectionIsEmpty)
        popMenu.addSeparator()

        lockAction = popMenu.addAction("Lock Selected")
        lockAction.triggered.connect(self.lockSelectedRows)
        lockAction.setEnabled(not selectionIsEmpty)

        lockAllButSelAction = popMenu.addAction("Lock All But Selected")
        lockAllButSelAction.triggered.connect(self.lockAllButSelectedRows)
        lockAllButSelAction.setEnabled(not selectionIsEmpty)

        unlockAction = popMenu.addAction("Unlock Selected")
        unlockAction.triggered.connect(self.unlockSelectedRows)
        unlockAction.setEnabled(not selectionIsEmpty)

        highliteAction = popMenu.addAction("Highlight Lock Verts")
        highliteAction.triggered.connect(self.highliteLockRows)

        clearLocksAction = popMenu.addAction("Clear All Locks")
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
            theBGBrush = self.regularBG
            if model.isSoftOn():
                col = int(multVal * 255 * 2)
                if col > 255:
                    RCol = 255
                    GCol = col - 255
                else:
                    GCol = 0
                    RCol = col
                theBGBrush = QtGui.QColor(RCol, GCol, 0, 100)

        painter.setBrush(QtGui.QBrush(self.sepCOL))
        painter.drawRect(rect)
        painter.setBrush(QtGui.QBrush(theBGBrush))
        painter.drawRect(rect.adjusted(0, -1, -2, -1))
        painter.restore()
        painter.drawText(rect, QtCore.Qt.AlignCenter, text)

    def getSelectedRows(self):
        sel = self.selectionModel().selection()
        chunks = np.array([], dtype=int)
        for item in sel:
            chunks = np.union1d(chunks, list(range(item.top(), item.bottom() + 1)))

        return chunks

    def _getDatatable(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        return model.datatable

    def selectVerts(self):
        selectedIndices = self.getSelectedRows()
        self._getDatatable().selectVerts(selectedIndices)

    def highliteLockRows(self):
        datatable = self._getDatatable()
        newSel = self.selectionModel().selection()
        newSel.clear()
        nbColumns = datatable.columnCount
        model = self.model()
        for row in range(self.count()):
            if datatable.vertices[row] in datatable.lockedVertices:
                newSel.select(model.index(row, 0), model.index(row, nbColumns - 1))

        self.selectionModel().select(newSel, QtCore.QItemSelectionModel.ClearAndSelect)

    def lockSelectedRows(self):
        selectedIndices = self.getSelectedRows()
        self._getDatatable().lockRows(selectedIndices)

    def lockAllButSelectedRows(self):
        selectedIndices = set(range(self.count()))
        selectedIndices.difference_update(self.getSelectedRows())
        self._getDatatable().lockRows(selectedIndices)

    def unlockSelectedRows(self):
        selectedIndices = self.getSelectedRows()
        self._getDatatable().unLockRows(selectedIndices)

    def clearLocks(self):
        self._getDatatable().unLockRows(list(range(self.count())))


class HorizHeaderView(QtWidgets.QHeaderView):
    totalFG = _autoProp("totalFG")
    totalBG = _autoProp("totalBG")
    lockedBG = _autoProp("lockedBG")
    rightSideObjectBG = _autoProp("rightSideObjectBG")
    leftSideObjectBG = _autoProp("leftSideObjectBG")
    midSideObjectBG = _autoProp("midSideObjectBG")
    unsidedObjectBG = _autoProp("unsidedObjectBG")
    sepCOL = _autoProp("sepCOL")

    def __init__(self, mainWindow=None, colWidth=10, parent=None):
        self._multiStore = {}
        super(HorizHeaderView, self).__init__(QtCore.Qt.Horizontal, parent)
        self.mainWindow = mainWindow
        self.getColors()
        self.colWidth = colWidth

        self._margin = 10
        self._colorDrawHeight = 20
        self.setSectionsClickable(True)
        self.setHighlightSections(True)
        self.setSectionResizeMode(QtWidgets.QHeaderView.Fixed)

        self.letVerticesDraw = True

        self.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self.showMenu)

    def getColors(self):
        self._colors = []
        for i in range(1, 9):
            col = cmds.displayRGBColor("userDefined{0}".format(i), q=True)
            self._colors.append([int(el * 255) for el in col])

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

        obj = model.fullColumnNames()[ind]
        if cmds.getAttr(obj + ".useObjectColor"):
            ocAttr = obj + ".objectColor"
            colorIdx = cmds.getAttr(ocAttr)
            # Why is the joint color index ofsetted by 24??
            els = cmds.colorIndex(colorIdx + 24, query=True)
        else:
            attr = obj + ".wireColorRGB"
            els = cmds.getAttr(attr)[0]
        return [int(255 * el) for el in els]

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

    def _getDatatable(self):
        model = self.model()
        assert isinstance(model, TableModel)
        assert model.datatable is not None
        return model.datatable

    def getSelectedColumns(self):
        sel = self.selectionModel().selection()
        chunks = np.array([], dtype=int)
        for item in sel:
            chunks = np.union1d(chunks, list(range(item.left(), item.right() + 1)))

        selectedIndices = [indCol for indCol in chunks if not self.isSectionHidden(indCol)]

        if isinstance(self._getDatatable(), DataOfSkin):
            lastCol = self.count() - 1
            if lastCol in selectedIndices:
                selectedIndices.remove(lastCol)
        return selectedIndices

    def lockSelectedColumns(self):
        selectedIndices = self.getSelectedColumns()
        self._getDatatable().lockColumns(selectedIndices)
        if self.mainWindow is not None:
            self.mainWindow.refreshPaintEditor()

    def lockAllButSelectedColumns(self):
        datatable = self._getDatatable()

        selectedIndices = set(range(self.count() - 1))
        datatable.unLockColumns(selectedIndices)

        selectedIndices.difference_update(self.getSelectedColumns())
        datatable.lockColumns(selectedIndices)
        if self.mainWindow is not None:
            self.mainWindow.refreshPaintEditor()

    def unlockSelectedColumns(self):
        selectedIndices = self.getSelectedColumns()
        self._getDatatable().unLockColumns(selectedIndices)
        if self.mainWindow is not None:
            self.mainWindow.refreshPaintEditor()

    def selectDeformers(self):
        selectedIndices = self.getSelectedColumns()
        self._getDatatable().selectDeformers(selectedIndices)

    def displayVertices(self, doSelect=True):
        selectedColumns = self.getSelectedColumns()
        self._getDatatable().selectVertsOfColumns(selectedColumns, doSelect=doSelect)

    def clearLocks(self):
        self._getDatatable().unLockColumns(list(range(self.count() - 1)))
        if self.mainWindow is not None:
            self.mainWindow.refreshPaintEditor()

    def enterPaintAttribute(self):
        datatable = self._getDatatable()
        selectedColumns = self.getSelectedColumns()
        colIndex = selectedColumns.pop()
        theAtt = datatable.attributesToPaint[datatable.shortColumnsNames[colIndex]]
        mel.eval('artSetToolAndSelectAttr("artAttrCtx", "{}");'.format(theAtt))

    def showMenu(self, pos):
        popMenu = QtWidgets.QMenu(self)
        selectionIsEmpty = self.selectionModel().selection().isEmpty()

        datatable = self._getDatatable()
        if isinstance(datatable, DataOfSkin):
            selAction = popMenu.addAction("Select Deformers")
            selAction.triggered.connect(self.selectDeformers)
            selAction.setEnabled(not selectionIsEmpty)

        selVertices = popMenu.addAction("Select Vertices")
        selVertices.triggered.connect(partial(self.displayVertices, True))
        selVertices.setEnabled(not selectionIsEmpty)

        if isinstance(datatable, DataOfSkin):
            popMenu.addSeparator()

            lockAction = popMenu.addAction("Lock Selected")
            lockAction.triggered.connect(self.lockSelectedColumns)
            lockAction.setEnabled(not selectionIsEmpty)

            lockAllButSelAction = popMenu.addAction("Lock All But Selected")
            lockAllButSelAction.triggered.connect(self.lockAllButSelectedColumns)
            lockAllButSelAction.setEnabled(not selectionIsEmpty)

            unlockAction = popMenu.addAction("Unlock Selected")
            unlockAction.triggered.connect(self.unlockSelectedColumns)
            unlockAction.setEnabled(not selectionIsEmpty)

            clearLocksAction = popMenu.addAction("Clear All locks")
            clearLocksAction.triggered.connect(self.clearLocks)

            hideColumnIndices = datatable.hideColumnIndices
            columnNames = self.model().columnNames()
            popMenu.addSeparator()
            hideZeroColumnsAction = popMenu.addAction("Hide Zero Columns")
            hideZeroColumnsAction.setCheckable(True)
            if self.mainWindow is not None:
                hideZeroColumnsAction.setChecked(self.mainWindow.hideZeroColumn)
                hideZeroColumnsAction.toggled.connect(self.mainWindow.toggleZeroColumn)

            subMenuFollow = popMenu.addMenu("Show Columns")
            for ind in hideColumnIndices:
                chbox = QtWidgets.QCheckBox(columnNames[ind], subMenuFollow)

                chbox.setChecked(not self.isSectionHidden(ind))
                chbox.toggled.connect(partial(self.toggledColumn, ind, columnNames[ind]))

                checkableAction = QtWidgets.QWidgetAction(subMenuFollow)
                checkableAction.setDefaultWidget(chbox)
                subMenuFollow.addAction(checkableAction)

        else:
            paintAttr = popMenu.addAction("Paint Attribute")
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

        datatable = self._getDatatable()
        isLastColumn = isinstance(datatable, DataOfSkin) and index >= datatable.nbDrivers
        data = self._get_data(index)
        font = self.font()
        descent = self.fontMetrics().descent()

        if isLastColumn:
            painter.save()

            painter.setBrush(QtGui.QBrush(self.sepCOL))
            painter.drawRect(rect.adjusted(0, 0, -1, 0))

            painter.setBrush(QtGui.QBrush(self.totalBG))
            painter.drawRect(rect.adjusted(-1, 0, -1, -2))
            painter.restore()

            painter.setFont(font)
            painter.setPen(self.totalFG)
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
            font.setBold(isBold)
            painter.setFont(font)

            side = self.model().getColumnSide(index)
            defaultBGInd = "RLMX".index(side)
            defaultBG = [
                self.rightSideObjectBG,
                self.leftSideObjectBG,
                self.midSideObjectBG,
                self.unsidedObjectBG,
            ][defaultBGInd]

            theBGBrush = (
                self.lockedBG if self.model().datatable.isColumnLocked(index) else defaultBG
            )

            # Draw the separator color
            painter.setBrush(QtGui.QBrush(self.sepCOL))
            painter.drawRect(rect)

            # Draw the background color
            painter.setBrush(QtGui.QBrush(theBGBrush))
            painter.drawRect(rect.adjusted(-1, 0, -1, -1))

            # Translate to the top-left corner of the swatch
            painter.translate(rect.left(), rect.height() - self._colorDrawHeight)

            # Draw the color swatch
            theColor = self.color(index)
            painter.setBrush(QtGui.QBrush(QtGui.QColor(*theColor)))
            painter.drawRect(0, 0, rect.width() - 2, self._colorDrawHeight - 2)

            # Build a rotated rectangle to draw the text in
            painter.rotate(-90)
            rotRect = QtCore.QRectF(0, 0, rect.height() - self._colorDrawHeight, rect.width())
            # Offset the rectangle a bit to visually center the text
            rotRect = rotRect.adjusted(self._margin, -descent, 0, 0)

            # paint the text
            textOpt = QtGui.QTextOption(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            painter.drawText(rotRect, data, textOpt)

    def sizeHint(self):
        return QtCore.QSize(10, self._get_text_width() + 2 * self._margin + self._colorDrawHeight)

    def _get_text_width(self):
        model = self.model()
        assert isinstance(model, TableModel)
        colNames = model.columnNames()
        ff = self.font()
        ff.setBold(True)
        metrics = QtGui.QFontMetrics(ff)
        allMetrics = [metrics.width(colName) for colName in colNames]
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

    # make the meta properties to hold the data passed in from the stylesheet
    sumColumnBG = _autoProp("sumColumnBG")
    sumColumnFG = _autoProp("sumColumnFG")
    sumColumnERROR = _autoProp("sumColumnERROR")
    lockedBG = _autoProp("lockedBG")
    lockedFG = _autoProp("lockedFG")
    nonzeroBG = _autoProp("nonzeroBG")
    nonzeroFG = _autoProp("nonzeroFG")
    nonzeroHI = _autoProp("nonzeroHI")
    zeroBG = _autoProp("zeroBG")
    zeroFG = _autoProp("zeroFG")
    zeroHI = _autoProp("zeroHI")
    regularBG = _autoProp("regularBG")
    sepCOL = _autoProp("sepCOL")

    def __init__(self, parent, colWidth=10):
        self.ignoreReselect = False
        self._multiStore = {}

        super(FastTableView, self).__init__(parent)
        self.mainWindow = parent

        self._hd = HighlightDelegate(self)
        self.setItemDelegate(self._hd)
        self.HHeaderView = HorizHeaderView(self.mainWindow, colWidth)
        self.VHeaderView = VertHeaderView(self.mainWindow)

        self.setHorizontalHeader(self.HHeaderView)
        self.setVerticalHeader(self.VHeaderView)

        self.verticalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Fixed)

        self._margin = 10
        self._colorDrawHeight = 20

        self._nw_heading = "Vtx"
        self.addRedrawButton()

    def keyPressEvent(self, event):
        txt = event.text()
        isIn = txt and txt in string.ascii_letters
        if isIn:
            return

        super(FastTableView, self).keyPressEvent(event)

    def rmvRedrawButton(self):
        btn = self.findChild(QtWidgets.QAbstractButton)
        btn.removeEventFilter(self)
        self.repaint()

    def addRedrawButton(self):
        btn = self.findChild(QtWidgets.QAbstractButton)
        btn.setText(self._nw_heading)
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

    def drawRotatedText(self, rect):
        thePixmap = QtGui.QPixmap(rect.width(), rect.height())
        thePixmap.fill(self.sepCOL)

        descent = self.fontMetrics().descent()

        data = self.model().datatable.shapeShortName

        painter = QtGui.QPainter()
        painter.begin(thePixmap)
        painter.setBrush(QtGui.QBrush(self.regularBG))

        # Draw a filled rectangle inset from the edges
        painter.drawRect(rect.adjusted(0, 0, -2, -2))

        # Rotate the coordinate system and put 0,0 at the bottom left corner
        painter.rotate(-90)
        painter.translate(-rect.height(), 0)

        # Build a rotated rectangle to draw the text in
        rotRect = QtCore.QRectF(0, 0, rect.height(), rect.width())
        # Offset the rectangle a bit to visually center the text
        rotRect = rotRect.adjusted(self._margin, -descent, 0, 0)

        # paint the text
        painter.setFont(self.font())
        textOpt = QtGui.QTextOption(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
        painter.drawText(rotRect, data, textOpt)

        painter.end()

        return thePixmap

    def eventFilter(self, obj, event):
        # The only QAbstractButton that gets painted by this view is the upper left corner
        # button, so add an event filter to intercept its paint event
        if event.type() != QtCore.QEvent.Paint:
            return False

        if not isinstance(obj, QtWidgets.QAbstractButton):
            return False

        # Look at the QTableCornerButton code from the QTableView.cpp source
        # and re-implement some of that code
        opt = QtWidgets.QStyleOptionHeader()
        opt.initFrom(obj)
        opt.rect = obj.rect()
        opt.position = QtWidgets.QStyleOptionHeader.OnlyOneSection
        painter = QtWidgets.QStylePainter(obj)
        painter.drawItemPixmap(opt.rect, 1, self.drawRotatedText(opt.rect))

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
