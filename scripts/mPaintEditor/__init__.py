from __future__ import absolute_import
import os

PAINT_EDITOR = None
PAINT_EDITOR_ROOT = None
PAINT_EDITOR_CONTEXT_NAME = "brSkinBrushContext"
PAINT_EDITOR_CONTEXT = "brSkinBrushContext1"
PAINT_EDITOR_CONTEXT_OPTIONS = "brSkinBrushContextOptions"


def voidOutPaintEditorContexts():
    """A developer tool that will overwrite old brSkinBrushContext
    instances. You should call this when you unload the plugin
    """
    # Maya doesn't let you delete a context, but we *can* overwrite it
    # so on plugin unload, we can overwrite the existing contexts
    # with an arbitrary context (manipMoveContext was chosen for no reason)
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


def getOrCreatePaintEditorContext():
    """Search for either an existing brSkinBrushContext, or a name
    of a context that doesn't yet exist

    This way we can void-out old contexts when developing the tool
    This is the ONLY place where we should build a new skin brush context
    """
    from maya import cmds

    global PAINT_EDITOR_CONTEXT
    for i in range(1, 50):
        pec = PAINT_EDITOR_CONTEXT_NAME + str(i)
        if cmds.contextInfo(pec, exists=True):
            if cmds.contextInfo(pec, c=True) == PAINT_EDITOR_CONTEXT_NAME:
                print("FOUND EXISTING CONTEXT", pec)
                PAINT_EDITOR_CONTEXT = pec
                return
        else:
            cmds.brSkinBrushContext(pec)
            PAINT_EDITOR_CONTEXT = pec
            print("BUILT NEW CONTEXT", pec)
            return
    raise RuntimeError("You've reloaded brSkinBrush like 50 times... Take a break")


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
