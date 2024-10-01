import os
from Qt import QtGui


def getIcon(iconNm):
    fileVar = os.path.realpath(__file__)
    uiFolder = os.path.dirname(fileVar)
    iconPth = os.path.join(uiFolder, "img", iconNm + ".png")
    return QtGui.QIcon(iconPth)


ICONS = {
    "lockedIcon": getIcon("lock-gray-locked"),
    "unLockIcon": getIcon("lock-gray-unlocked"),
    "lock": getIcon("lock-48"),
    "unlock": getIcon("unlock-48"),
    "del": getIcon("delete_sign-16"),
    "fromScene": getIcon("arrow-045"),
    "pinOn": getIcon("pinOn"),
    "pinOff": getIcon("pinOff"),
    "gaussian": getIcon("circleGauss"),
    "poly": getIcon("circlePoly"),
    "solid": getIcon("circleSolid"),
    "curveNone": getIcon("brSkinBrushNone"),
    "curveLinear": getIcon("brSkinBrushLinear"),
    "curveSmooth": getIcon("brSkinBrushSmooth"),
    "curveNarrow": getIcon("brSkinBrushNarrow"),
    "clearText": getIcon("clearText"),
    "square": getIcon("rect"),
    "refresh": getIcon("arrow-circle-045-left"),
    "eye": getIcon("eye"),
    "eye-half": getIcon("eye-half"),
    "plus": getIcon("plus-button"),
    "minus": getIcon("minus-button"),
    "removeUnused": getIcon("arrow-transition-270--red"),
    "randomColor": getIcon("color-swatch"),
}
