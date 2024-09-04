#include <maya/MFnPlugin.h>

#include "wireframeDisplay.h"
#include "version.h"

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// Plugin Registration
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

MStatus initializePlugin(MObject obj) {
    MStatus status;
    MFnPlugin plugin(obj, "Blur Studio", VERSION_STRING, "Any");

    status = plugin.registerNode("wireframeDisplay", wireframeDisplay::id,
                                 &wireframeDisplay::creator, &wireframeDisplay::initialize,
                                 MPxNode::kLocatorNode, &wireframeDisplay::drawDbClassification);
    if (!status) {
        status.perror("registerNode");
        return status;
    }

    status = MHWRender::MDrawRegistry::registerDrawOverrideCreator(
        wireframeDisplay::drawDbClassification, wireframeDisplay::drawRegistrantId,
        wireframeDisplayDrawOverride::Creator);
    if (!status) {
        status.perror("registerDrawOverrideCreator");
        return status;
    }

    return status;
}

MStatus uninitializePlugin(MObject obj) {
    MStatus status;
    MFnPlugin plugin(obj);

    status = MHWRender::MDrawRegistry::deregisterDrawOverrideCreator(
        wireframeDisplay::drawDbClassification, wireframeDisplay::drawRegistrantId);
    if (!status) {
        status.perror("deregisterDrawOverrideCreator");
        return status;
    }

    status = plugin.deregisterNode(wireframeDisplay::id);
    if (!status) {
        status.perror("deregisterNode");
        return status;
    }
    return status;
}
