from __future__ import print_function
from __future__ import absolute_import

from maya import cmds
from six.moves import range
from Qt import QtGui, QtWidgets

from .icons import ICONS


# -------------------------------------------------------------------------------
# INFLUENCE ITEM
# -------------------------------------------------------------------------------
class InfluenceTree(QtWidgets.QTreeWidget):
    blueBG = QtGui.QBrush(QtGui.QColor(112, 124, 137))
    redBG = QtGui.QBrush(QtGui.QColor(134, 119, 127))
    yellowBG = QtGui.QBrush(QtGui.QColor(144, 144, 122))
    regularBG = QtGui.QBrush(QtGui.QColor(130, 130, 130))

    def getDeformerColor(self, driverName):
        try:
            for letter, col in [
                ("L", self.redBG),
                ("R", self.blueBG),
                ("M", self.yellowBG),
            ]:
                if "_{0}_".format(letter) in driverName:
                    return col
            return self.regularBG
        except Exception:
            return self.regularBG

    def paintEnd(self):
        self.setStyleSheet("")
        self.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection)

    def paintStart(self):
        self.setStyleSheet("QWidget {border : 2px solid red}\n")
        self.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection)
        selItems = self.selectedItems()
        if selItems:
            self.clearSelection()
            self.setCurrentItem(selItems[0])

    def __init__(self, *args):
        self.isOn = False
        super(InfluenceTree, self).__init__(*args)
        self.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection)
        self.setIndentation(5)
        self.setColumnCount(5)
        self.header().hide()
        self.setColumnWidth(0, 20)
        self.hideColumn(2)  # column 2 is side alpha name
        self.hideColumn(3)  # column 3 is the default indices
        self.hideColumn(4)  # column 4 is the sorted by weight picked indices

    def enterEvent(self, event):
        self.isOn = True
        super(InfluenceTree, self).enterEvent(event)

    def leaveEvent(self, event):
        self.isOn = False
        super(InfluenceTree, self).leaveEvent(event)


class InfluenceTreeWidgetItem(QtWidgets.QTreeWidgetItem):
    _colors = [
        (161, 105, 48),
        (159, 161, 48),
        (104, 161, 48),
        (48, 161, 93),
        (48, 161, 161),
        (48, 103, 161),
        (111, 48, 161),
        (161, 48, 105),
    ]

    def getColors(self):
        self._colors = []
        for i in range(1, 9):
            col = cmds.displayRGBColor("userDefined{0}".format(i), query=True)
            self._colors.append([int(el * 255) for el in col])

    def __init__(self, influence, index, col, skinCluster):
        self.isZeroDfm = False
        shortName = influence.split(":")[-1]
        # now sideAlpha
        spl = shortName.split("_")
        if len(spl) > 2:
            spl.append(spl.pop(1))
        sideAlphaName = "_".join(spl)

        super(InfluenceTreeWidgetItem, self).__init__(
            [
                "",
                shortName,
                sideAlphaName,
                "{:09d}".format(index),
                "{:09d}".format(index),
            ]
        )
        self._influence = influence
        self._index = index
        self._skinCluster = skinCluster
        self.regularBG = col
        self._indexColor = None

        self.currentColor = self.color()

        self.setBackground(1, self.regularBG)
        self.darkBG = QtGui.QBrush(QtGui.QColor(120, 120, 120))
        self.setDisplay()

    def setDisplay(self):
        self.setIcon(0, self.colorIcon())
        self.setIcon(1, self.lockIcon())
        if self.isLocked():
            self.setBackground(1, self.darkBG)
        else:
            self.setBackground(1, self.regularBG)

    def resetBindPose(self):
        inConn = cmds.listConnections(self._skinCluster + ".bindPreMatrix[{0}]".format(self._index))
        if not inConn:
            mat = cmds.getAttr(self._influence + ".worldInverseMatrix")
            cmds.setAttr(
                self._skinCluster + ".bindPreMatrix[{0}]".format(self._index),
                mat,
                type="matrix",
            )

    def setColor(self, col):
        self.currentColor = col
        self._indexColor = None
        cmds.setAttr(self._influence + ".wireColorRGB", *col)
        self.setIcon(0, self.colorIcon())

    def color(self):
        wireColor = cmds.getAttr(self._influence + ".wireColorRGB")[0]
        if wireColor == (0.0, 0.0, 0.0):
            objColor = cmds.getAttr(self._influence + ".objectColor")
            wireColor = cmds.displayRGBColor("userDefined{0}".format(objColor + 1), query=True)

        ret = [int(255 * el) for el in wireColor]
        return ret

    def lockIcon(self):
        return ICONS["lockedIcon"] if self.isLocked() else ICONS["unLockIcon"]

    def colorIcon(self):
        pixmap = QtGui.QPixmap(24, 24)
        pixmap.fill(QtGui.QColor(*self.color()))
        return QtGui.QIcon(pixmap)

    def setLocked(self, locked, autoHide=False):
        cmds.setAttr(self._influence + ".lockInfluenceWeights", locked)
        if locked:
            self.setSelected(False)
        if autoHide and locked:
            self.setHidden(True)
        self.setDisplay()

    def isLocked(self):
        return cmds.getAttr(self._influence + ".lockInfluenceWeights")

    def influence(self):
        return self._influence

    def showWeights(self, value):
        self.setText(2, str(value))
