// ---------------------------------------------------------------------
//
//  pluginMain.cpp
//  brSkinBrush
//
//  Created by Guillaume Babin using code from ingo on 11/18/18.
//  https://github.com/IngoClemens/brSkinBrush?fbclid=IwAR0VxH-zX51EwtPtX4-bvAoESL7YhjYC3_BJcyHbwV1qrUMx3uVvCVOwGFg
//  Copyright (c) 2018 ingo. All rights reserved.
//
// ---------------------------------------------------------------------

#include <string>

#include <maya/MFnPlugin.h>
#include <maya/MFnPlugin.h>
#include <maya/MUserEventMessage.h>


#include "functions.h"
#include "skinBrushTool.h"
#include "version.h"

// ---------------------------------------------------------------------
// initialization
// ---------------------------------------------------------------------

MStatus initializePlugin(MObject obj) {
    MStatus status;
    MFnPlugin plugin(obj, "Blur Studio", VERSION_STRING, "Any");

    status = plugin.registerContextCommand("brSkinBrushContext", SkinBrushContextCmd::creator,
                                           "brSkinBrushCmd", skinBrushTool::creator);
    if (status != MStatus::kSuccess) {
        status.perror("Register brSkinBrushContext failed.");
    }
    else {
        MUserEventMessage::registerUserEvent("brSkinBrush_influencesReordered");
        MUserEventMessage::registerUserEvent("brSkinBrush_pickedInfluence");
        MUserEventMessage::registerUserEvent("brSkinBrush_updateDisplayStrength");
        MUserEventMessage::registerUserEvent("brSkinBrush_updateDisplaySize");
        MUserEventMessage::registerUserEvent("brSkinBrush_afterPaint");
        MUserEventMessage::registerUserEvent("brSkinBrush_cleanCloseUndo");
        MUserEventMessage::registerUserEvent("brSkinBrush_cleanOpenUndo");
        MUserEventMessage::registerUserEvent("brSkinBrush_toolOffCleanup");

    }

    return status;
}

MStatus uninitializePlugin(MObject obj) {
    MStatus status;
    MFnPlugin plugin(obj, "Blur Studio", VERSION_STRING, "Any");

    status = plugin.deregisterContextCommand("brSkinBrushContext", "brSkinBrushCmd");
    if (status != MStatus::kSuccess) {
        status.perror("Deregister brSkinBrushContext failed.");
    }
    else {
        MUserEventMessage::deregisterUserEvent("brSkinBrush_influencesReordered");
        MUserEventMessage::deregisterUserEvent("brSkinBrush_pickedInfluence");
        MUserEventMessage::deregisterUserEvent("brSkinBrush_updateDisplayStrength");
        MUserEventMessage::deregisterUserEvent("brSkinBrush_updateDisplaySize");
        MUserEventMessage::deregisterUserEvent("brSkinBrush_afterPaint");
        MUserEventMessage::deregisterUserEvent("brSkinBrush_cleanCloseUndo");
        MUserEventMessage::deregisterUserEvent("brSkinBrush_cleanOpenUndo");
        MUserEventMessage::deregisterUserEvent("brSkinBrush_toolOffCleanup");
    }

    return status;
}

// ---------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2018 Ingo Clemens, brave rabbit
// brSkinBrush and brTransferWeights are under the terms of the MIT
// License
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Author: Ingo Clemens    www.braverabbit.com
// ---------------------------------------------------------------------
