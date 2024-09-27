from __future__ import absolute_import
import os

PAINT_EDITOR = None
PAINT_EDITOR_ROOT = None
PAINT_EDITOR_CONTEXT_NAME = "brSkinBrushContext"
PAINT_EDITOR_CONTEXT_OPTIONS = "brSkinBrushContextOptions"


class GetContext:
    """Maya doesn't let you delete a context, but we *can* overwrite it
    so on plugin unload.

    So we can overwrite the existing contexts with an arbitrary context.
    Then the next time we load the UI, we can create a brand new context
    """

    def __init__(self):
        self.index = 1
        self.name = "brSkinBrushContext1"
        self.needsCreation = False
        self.updateIndex()

    @staticmethod
    def voidAll():
        """A developer tool that will overwrite old brSkinBrushContext
        instances. You should call this when you unload the plugin
        """
        from maya import cmds

        for i in range(1, 50):
            pec = PAINT_EDITOR_CONTEXT_NAME + str(i)
            if not cmds.contextInfo(pec, exists=True):
                break
            if cmds.contextInfo(pec, c=True) == PAINT_EDITOR_CONTEXT_NAME:
                # overwrite the context
                print("VOIDING CONTEXT:", pec)
                cmds.manipMoveContext(pec)

        if cmds.optionVar(exists=PAINT_EDITOR_CONTEXT_OPTIONS):
            print("CLEARING OPTION VAR")
            cmds.optionVar(remove=PAINT_EDITOR_CONTEXT_OPTIONS)

    def updateIndex(self):
        """Search for either an existing brSkinBrushContext, or a name
        of a context that doesn't yet exist

        This way we can void-out old contexts when developing the tool
        This is the ONLY place where we should build a new skin brush context
        """
        from maya import cmds

        for i in range(50):
            pec = PAINT_EDITOR_CONTEXT_NAME + str(i)
            if cmds.contextInfo(pec, exists=True):
                if cmds.contextInfo(pec, c=True) == PAINT_EDITOR_CONTEXT_NAME:
                    self.index = i
                    self.needsCreation = False
                    return
            else:
                self.index = i
                self.needsCreation = True
                return
        raise RuntimeError("You've reloaded the brSkinBrush plugin 50 times")

    def buildName(self):
        self.name = PAINT_EDITOR_CONTEXT_NAME + str(self.index)
        return self.name

    def getLatest(self):
        from maya import cmds

        if self.needsCreation:
            cmds.brSkinBrushContext(self.buildName())
            self.needsCreation = False
        return self.name


GET_CONTEXT = GetContext()


def runMPaintEditor():
    from .utils import rootWindow
    from .paintEditorWidget import SkinPaintWin

    # Keep global references around, otherwise they get GC'd
    global PAINT_EDITOR
    global PAINT_EDITOR_ROOT

    # make and show the UI
    if PAINT_EDITOR_ROOT is None:
        PAINT_EDITOR_ROOT = rootWindow()
    PAINT_EDITOR = SkinPaintWin(parent=PAINT_EDITOR_ROOT)
    PAINT_EDITOR.show()


if __name__ == "__main__":
    import sys

    folder = os.path.dirname(os.path.dirname(__file__))
    if folder not in sys.path:
        sys.path.insert(0, folder)
    runMPaintEditor()
