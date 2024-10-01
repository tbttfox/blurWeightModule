from __future__ import absolute_import
import os
import sys
import time
import datetime
from Qt.QtWidgets import QApplication, QSplashScreen, QDialog, QMainWindow
from maya import cmds
from contextlib import contextmanager


def getUiFile(fileVar, subFolder="ui", uiName=None):
    uiFolder, filename = os.path.split(fileVar)
    if uiName is None:
        uiName = os.path.splitext(filename)[0]
    return os.path.join(uiFolder, subFolder, uiName + ".ui")


@contextmanager
def GlobalContext(
    message="processing",
    raise_error=True,
    openUndo=True,
    suspendRefresh=False,
    doPrint=True,
):
    """A context for undos, refreshes, and timing

    Arguments:
        message (str): A message to print with the timing data, and
            the name of the chunk. Defaults to "processing"
        raise_error (bool): Whether to raise any exceptions caught in the
            context (True), or capture them and print (False), Defaults to True
        openUndo (bool): Whether to wrap the context in an undo bead.
            Defaults to True
        suspendRefresh (bool): Turn off UI refreshing while running this context
            Defaults to False
        doPrint (bool): Print the time and message
            Defaults to True
    """
    startTime = time.time()
    cmds.waitCursor(state=True)
    if openUndo:
        cmds.undoInfo(openChunk=True, chunkName=message)
    if suspendRefresh:
        cmds.refresh(suspend=True)

    try:
        yield

    except Exception as e:
        if raise_error:
            import traceback

            traceback.print_exc()
            raise
        else:
            sys.stderr.write("%s" % e)

    finally:
        if cmds.waitCursor(query=True, state=True):
            cmds.waitCursor(state=False)
        if openUndo:
            cmds.undoInfo(closeChunk=True)
        if suspendRefresh:
            cmds.refresh(suspend=False)
            cmds.refresh()
        completionTime = time.time() - startTime
        timeRes = str(datetime.timedelta(seconds=int(completionTime))).split(":")
        if doPrint:
            result = "{0} hours {1} mins {2} secs".format(*timeRes)
            print("{0} executed in {1}[{2:.2f} secs]".format(message, result, completionTime))


def rootWindow():
    """
    Returns the currently active QT main window
    Only works for QT UIs like Maya
    """
    # for MFC apps there should be no root window
    window = None
    if QApplication.instance():
        inst = QApplication.instance()
        window = inst.activeWindow()
        # Ignore QSplashScreen s, they should never be considered the root window.
        if isinstance(window, QSplashScreen):
            return None
        # If the application does not have focus try to find A top level widget
        # that doesn t have a parent and is a QMainWindow or QDialog
        if window is None:
            windows = []
            dialogs = []
            for w in QApplication.instance().topLevelWidgets():
                if w.parent() is None:
                    if isinstance(w, QMainWindow):
                        windows.append(w)
                    elif isinstance(w, QDialog):
                        dialogs.append(w)
            if windows:
                window = windows[0]
            elif dialogs:
                window = dialogs[0]
        # grab the root window
        if window:
            while True:
                parent = window.parent()
                if not parent:
                    break
                if isinstance(parent, QSplashScreen):
                    break
                window = parent
    return window
