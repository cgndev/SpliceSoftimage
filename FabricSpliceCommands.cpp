
#include <xsi_application.h>
#include <xsi_context.h>
#include <xsi_status.h>

#include <xsi_command.h>
#include <xsi_argument.h>
#include <xsi_factory.h>
#include <xsi_longarray.h>
#include <xsi_doublearray.h>
#include <xsi_math.h>
#include <xsi_customproperty.h>
#include <xsi_ppglayout.h>
#include <xsi_menu.h>
#include <xsi_selection.h>
#include <xsi_project.h>
#include <xsi_scene.h>
#include <xsi_utils.h>
#include <xsi_customoperator.h>

// project includes
#include "FabricSplicePlugin.h"
#include "FabricSpliceDialogs.h"
#include "FabricSpliceCommands.h"
#include "FabricSpliceBaseInterface.h"
#include "FabricSpliceRenderPass.h"

using namespace XSI;

void xsiUpdateOp(unsigned int objectID)
{
  CustomOperator op(Application().GetObjectFromID(objectID));
  if(!op.IsValid())
    return;

  LONG evalID = op.GetParameterValue("evalID");
  op.PutParameterValue("evalID", (evalID + 1) % 1000);
}

SICALLBACK fabricSplice_Init(CRef & in_ctxt)
{
  Context ctxt( in_ctxt );
  Command oCmd;
  oCmd = ctxt.GetSource();
  oCmd.PutDescription(L"Edits a Fabric:Splice Operator");
  oCmd.SetFlag(siNoLogging,false);
  oCmd.EnableReturnValue( true ) ;
  ArgumentArray oArgs = oCmd.GetArguments();
  oArgs.Add(L"action");
  oArgs.Add(L"reference");
  oArgs.Add(L"data");
  oArgs.Add(L"auxiliary");
  return CStatus::OK;
}

SICALLBACK fabricSplice_Execute(CRef & in_ctxt)
{
  FabricSplice::Logging::AutoTimer("fabricSplice_Execute");

  xsiClearError();

  CStatus status = CStatus::OK;
  xsiInitializeSplice();

  Context ctxt( in_ctxt );
  CValueArray args = ctxt.GetAttribute(L"Arguments");

  try
  {
    CString actionStr = CString(args[0]);
    CString referenceStr = CString(args[1]);
    CString dataStr = CString(args[2]);
    CString auxiliaryStr = CString(args[3]);
    FabricCore::Variant scriptArgs;

    if(actionStr == "constructClient")
    {
      bool clientCreated = FabricSplice::ConstructClient().isValid();
      ctxt.PutAttribute(L"ReturnValue", clientCreated);

      FabricSplice::setDCCOperatorSourceCodeCallback(&getSourceCodeForOperator);
      
      return xsiErrorOccured();
    }
    else if(actionStr == "destroyClient")
    {
      bool clientDestroyed = FabricSplice::DestroyClient();
      ctxt.PutAttribute(L"ReturnValue", clientDestroyed);
      return xsiErrorOccured();
    }
    else if(actionStr == "getClientContextID")
    {
      CString clientContextID = FabricSplice::GetClientContextID();
      ctxt.PutAttribute(L"ReturnValue", clientContextID);
      return xsiErrorOccured();
    }
    else if(actionStr == "registerKLType")
    {
      CString rtStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "rt").c_str();
      CString extStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "extension", "", true).c_str();
      if(!extStr.IsEmpty())
        FabricSplice::DGGraph::loadExtension(extStr.GetAsciiString());
      FabricSplice::DGGraph::loadExtension(rtStr.GetAsciiString());
      return xsiErrorOccured();
    }
    else if(actionStr.IsEqualNoCase("startProfiling"))
    {
      FabricSplice::Logging::enableTimers();
      for(unsigned int i=0;i<FabricSplice::Logging::getNbTimers();i++)
      {
        FabricSplice::Logging::resetTimer(FabricSplice::Logging::getTimerName(i));
      }    
      return xsiErrorOccured();
    }
    else if(actionStr.IsEqualNoCase("stopProfiling"))
    {
      for(unsigned int i=0;i<FabricSplice::Logging::getNbTimers();i++)
      {
        FabricSplice::Logging::logTimer(FabricSplice::Logging::getTimerName(i));
      }    
      FabricSplice::Logging::disableTimers();
      return xsiErrorOccured();
    }

    if(actionStr.IsEqualNoCase(L"newsplice") || actionStr.IsEqualNoCase(L"loadsplice"))
    {
      scriptArgs = FabricSplice::Scripting::parseScriptingArguments(
        actionStr.GetAsciiString(), "", 
        referenceStr.GetAsciiString(), dataStr.GetAsciiString());
    } 
    else
    {
      scriptArgs = FabricSplice::Scripting::parseScriptingArguments(
        actionStr.GetAsciiString(), referenceStr.GetAsciiString(), 
        dataStr.GetAsciiString(), auxiliaryStr.GetAsciiString());
    }

    // fetch the relevant arguments
    actionStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "action").c_str();

    // optional arguments
    referenceStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "reference", "", true).c_str();
    auxiliaryStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "auxiliary", "", true).c_str();

    // get the operator from string or ID
    CRef opRef;
    opRef.Set(referenceStr);

    // if we have a valid operator, let's try to get the base interface
    FabricSpliceBaseInterface * interf = FabricSpliceBaseInterface::getInstanceByObjectID(ProjectItem(opRef).GetObjectID());
    if(actionStr.IsEqualNoCase("newSplice"))
    {
      // parse args
      CString targetsStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "targets").c_str();
      CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();
      CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode", "DGNode", true).c_str();

      FabricSplice::Port_Mode portMode = FabricSplice::Port_Mode_OUT;
      CString portModeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portMode", "", true).c_str();
      if(!portModeStr.IsEmpty())
      {
        if(portModeStr.IsEqualNoCase(L"io"))
          portMode = FabricSplice::Port_Mode_IO;
      }

      CRefArray targetRefs = getCRefArrayFromCString(targetsStr);
      if(targetRefs.GetCount() == 0)
      {
        xsiLogErrorFunc("'targets' script argument '"+targetsStr+"' is not valid.");
        return CStatus::InvalidArgument;
      }

      CString targetDataType = getSpliceDataTypeFromRefArray(targetRefs);
      if(targetDataType.IsEmpty())
      {
        xsiLogErrorFunc("'targets' script argument uses unsupported data type.");
        return CStatus::InvalidArgument;
      }

      interf = new FabricSpliceBaseInterface();

      status = interf->addXSIPort(targetRefs, portNameStr, targetDataType, portMode, dgNodeStr);
      if(!status.Succeeded())
        return status;

      status = interf->updateXSIOperator();
      if(!status.Succeeded())
        return status;

      CRef ofRef = Application().GetObjectFromID((LONG)interf->getObjectID());
      ctxt.PutAttribute(L"ReturnValue", ofRef.GetAsText());
    }
    else if(actionStr.IsEqualNoCase("loadSplice"))
    {
      // parse args
      CString fileNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "fileName").c_str();
      bool hideUI = FabricSplice::Scripting::consumeBooleanArgument(scriptArgs, "hideUI", false, true);

      interf = new FabricSpliceBaseInterface();
      CStatus loadResult = interf->loadFromFile(fileNameStr, scriptArgs, hideUI);
      if(!loadResult.Succeeded())
      {
        if(loadResult == CStatus::NotImpl)
        {
          delete(interf);
          return CStatus::Unexpected;
        }
        else if(!hideUI)
        {
          ImportSpliceDialog dialog(fileNameStr);
          interf->setObjectID(dialog.getProp().GetObjectID());
          dialog.show(L"Define Splice Ports");
          return xsiErrorOccured();
        }
        return CStatus::Unexpected;
      }

      CRef ofRef = Application().GetObjectFromID((LONG)interf->getObjectID());
      ctxt.PutAttribute(L"ReturnValue", ofRef.GetAsText());
    }   
    else if(actionStr.IsEqualNoCase("isRendererEnabled"))
    {
      ctxt.PutAttribute(L"ReturnValue", isRTRPassEnabled());
    }
    else if(actionStr.IsEqualNoCase("toggleRenderer"))
    {
      enableRTRPass(!isRTRPassEnabled());
    }
    else
    {
      if(interf == NULL)
      {
        xsiLogErrorFunc("No valid spliceOp specified (arg0).");
        return CStatus::InvalidArgument;
      }
      
      if(actionStr.IsEqualNoCase("saveSplice"))
      {
        CString fileNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "fileName").c_str();
        status = interf->saveToFile(fileNameStr);
        if(!status.Succeeded())
          return status;
      }   
      else if(actionStr.IsEqualNoCase("addDGNode"))
      {
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode").c_str();
        interf->getSpliceGraph().constructDGNode(dgNodeStr.GetAsciiString());
        return xsiErrorOccured();
      }
      else if(actionStr.IsEqualNoCase("removeDGNode"))
      {
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode").c_str();
        interf->getSpliceGraph().removeDGNode(dgNodeStr.GetAsciiString());
        return xsiErrorOccured();
      }
      else if(actionStr.IsEqualNoCase("setDGNodeDependency"))
      {
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode", "DGNode", true).c_str();
        CString dependencyStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dependency").c_str();
        interf->getSpliceGraph().setDGNodeDependency(dgNodeStr.GetAsciiString(), dependencyStr.GetAsciiString());
        return xsiErrorOccured();
      }
      else if(actionStr.IsEqualNoCase("removeDGNodeDependency"))
      {
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode", "DGNode", true).c_str();
        CString dependencyStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dependency").c_str();
        interf->getSpliceGraph().removeDGNodeDependency(dgNodeStr.GetAsciiString(), dependencyStr.GetAsciiString());
        return xsiErrorOccured();
      }
      else if(actionStr.IsEqualNoCase("addParameter"))
      {
        // parse args
        CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();
        CString dataTypeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dataType").c_str();
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode", "DGNode", true).c_str();
        CString portModeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portMode", "in", true).c_str();
        CString extStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "extension", "", true).c_str();

        FabricCore::Variant defaultValue;
        if(auxiliaryStr.Length() > 0)
          defaultValue = FabricCore::Variant::CreateFromJSON(auxiliaryStr.GetAsciiString());

        status = interf->addXSIParameter(processNameCString(portNameStr), dataTypeStr, portModeStr, processNameCString(dgNodeStr), defaultValue, extStr);
        if(!status.Succeeded())
          return status;

        // add all additional arguments as flags on the port
        FabricSplice::DGPort port = interf->getSpliceGraph().getDGPort(portNameStr.GetAsciiString());
        if(port.isValid())
        {
          for(FabricCore::Variant::DictIter keyIter(scriptArgs); !keyIter.isDone(); keyIter.next())
          {
            std::string key = keyIter.getKey()->getStringData();
            FabricCore::Variant value = *keyIter.getValue();
            if(value.isNull())
              continue;
            port.setOption(key.c_str(), value);
          }
        }

        
        status = interf->updateXSIOperator();
        if(!status.Succeeded())
          return status;
      }   
      else if(actionStr.IsEqualNoCase("addInputPort") || actionStr.IsEqualNoCase("addOutputPort") || actionStr.IsEqualNoCase("addIOPort"))
      {
        // parse args
        CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();
        CString iceAttrStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "iceAttribute", "", true).c_str();
        CString dataTypeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dataType", "", !iceAttrStr.IsEmpty()).c_str();
        CString targetsStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "targets").c_str();
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode", "DGNode", true).c_str();

        CRefArray targetRefs = getCRefArrayFromCString(targetsStr);
        if(targetRefs.GetCount() == 0)
        {
          xsiLogErrorFunc("data json targets '"+targetsStr+"' is not valid.");
          return CStatus::InvalidArgument;
        }

        FabricSplice::Port_Mode portMode = FabricSplice::Port_Mode_IN;
        if(actionStr.IsEqualNoCase("addOutputPort"))
          portMode = FabricSplice::Port_Mode_OUT;
        else if(actionStr.IsEqualNoCase("addIOPort"))
          portMode = FabricSplice::Port_Mode_IO;

        CString targetDataType;
        if(iceAttrStr.IsEmpty())
        {
          targetDataType = getSpliceDataTypeFromRefArray(targetRefs, dataTypeStr);
          if(targetDataType.IsEmpty())
          {
            xsiLogErrorFunc("Target(s) used for port uses an unsupported data type. Please match the target(s) used with the dataType '"+dataTypeStr+"'.");
            return CStatus::InvalidArgument;
          }
        }
        else
        {
          CString errorMessageStr;
          targetDataType = getSpliceDataTypeFromICEAttribute(targetRefs, iceAttrStr, errorMessageStr);
          if(targetDataType.IsEmpty())
          {
            xsiLogErrorFunc(errorMessageStr);
            return CStatus::InvalidArgument;
          }
        }

        if(!dataTypeStr.IsEmpty())
        {
          if(dataTypeStr != targetDataType && dataTypeStr != targetDataType + "[]")
          {
            xsiLogErrorFunc("Unable to connect port to targets. The data types do not match. Port Data Type:'" + dataTypeStr + "', Target Data Type:'" + targetDataType + "'.");
            return CStatus::Unexpected;
          }
          targetDataType = dataTypeStr;
        }

        if(iceAttrStr.IsEmpty())
        {
          status = interf->addXSIPort(targetRefs, processNameCString(portNameStr), targetDataType, portMode, processNameCString(dgNodeStr));
          if(!status.Succeeded())
            return status;
        }
        else
        {
          status = interf->addXSIICEPort(targetRefs, processNameCString(portNameStr), targetDataType, iceAttrStr, processNameCString(dgNodeStr));
          if(!status.Succeeded())
            return status;
        }

        // add all additional arguments as flags on the port
        FabricSplice::DGPort port = interf->getSpliceGraph().getDGPort(portNameStr.GetAsciiString());
        if(port.isValid())
        {
          for(FabricCore::Variant::DictIter keyIter(scriptArgs); !keyIter.isDone(); keyIter.next())
          {
            std::string key = keyIter.getKey()->getStringData();
            FabricCore::Variant value = *keyIter.getValue();
            if(value.isNull())
              continue;
            port.setOption(key.c_str(), value);
          }
        }
        status = interf->updateXSIOperator();
        if(!status.Succeeded())
          return status;
      }   
      else if(actionStr.IsEqualNoCase("addInternalPort"))
      {
        // parse args
        CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();
        CString dataTypeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dataType").c_str();
        CString portModeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portMode", "io", true).c_str();
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode", "DGNode", true).c_str();
        CString extStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "extension", "", true).c_str();
        bool autoInitObjects = FabricSplice::Scripting::consumeBooleanArgument(scriptArgs, "autoInitObjects", true, true);

        FabricSplice::Port_Mode portMode = FabricSplice::Port_Mode_IO;
        if(portModeStr.IsEqualNoCase(L"in"))
          portMode = FabricSplice::Port_Mode_IN;
        if(portModeStr.IsEqualNoCase(L"out"))
          portMode = FabricSplice::Port_Mode_OUT;

        FabricCore::Variant defaultValue;
        if(auxiliaryStr.Length() > 0)
          defaultValue = FabricCore::Variant::CreateFromJSON(auxiliaryStr.GetAsciiString());

        status = interf->addSplicePort(processNameCString(portNameStr), dataTypeStr, portMode, processNameCString(dgNodeStr), autoInitObjects, defaultValue, extStr);
        if(!status.Succeeded())
          return status;

        // add all additional arguments as flags on the port
        FabricSplice::DGPort port = interf->getSpliceGraph().getDGPort(portNameStr.GetAsciiString());
        if(port.isValid())
        {
          for(FabricCore::Variant::DictIter keyIter(scriptArgs); !keyIter.isDone(); keyIter.next())
          {
            std::string key = keyIter.getKey()->getStringData();
            FabricCore::Variant value = *keyIter.getValue();
            if(value.isNull())
              continue;
            port.setOption(key.c_str(), value);
          }
        }

        xsiUpdateOp(interf->getObjectID());
      }   
      else if(actionStr.IsEqualNoCase("removePort"))
      {
        // parse args
        CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();
        status = interf->removeSplicePort(portNameStr);
        if(!status.Succeeded())
          return status;
        status = interf->updateXSIOperator();
        if(!status.Succeeded())
          return status;
      }   
      else if(actionStr.IsEqualNoCase("reroutePort"))
      {
        // parse args
        CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();
        status = interf->rerouteXSIPort(portNameStr, scriptArgs);
        if(!status.Succeeded())
          return status;
        status = interf->updateXSIOperator();
        if(!status.Succeeded())
          return status;
      }   
      else if(actionStr.IsEqualNoCase("addKLOperator"))
      {
        // parse args
        CString opNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "opName").c_str();
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode", "DGNode", true).c_str();
        CString entryStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "entry", "", true).c_str();
        CString klCode = auxiliaryStr;
        FabricCore::Variant portMap = FabricSplice::Scripting::consumeVariantArgument(scriptArgs, "portMap", FabricCore::Variant::CreateDict(), true);

        opNameStr = processNameCString(opNameStr);

        if(klCode.IsEmpty())
          klCode = interf->getSpliceGraph().generateKLOperatorSourceCode(opNameStr.GetAsciiString()).getStringData();

        status = interf->addKLOperator(opNameStr, klCode, processNameCString(entryStr), processNameCString(dgNodeStr), portMap);
        if(!status.Succeeded())
          return status;

        xsiUpdateOp(interf->getObjectID());
      }   
      else if(actionStr.IsEqualNoCase("setKLOperatorCode"))
      {
        // parse args
        CString opNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "opName").c_str();
        CString klCode = auxiliaryStr;
        CString entryStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "entry", "", true).c_str();
        if(klCode.IsEmpty())
        {
          xsiLogErrorFunc("No kl specified.");
          return CStatus::InvalidArgument;
        }
        status = interf->setKLOperatorCode(processNameCString(opNameStr), klCode, processNameCString(entryStr));
        if(!status.Succeeded())
          return status;
        xsiUpdateOp(interf->getObjectID());
      }   
      else if(actionStr.IsEqualNoCase("getKLOperatorCode"))
      {
        // parse args
        CString opNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "opName").c_str();
        CString code = interf->getKLOperatorCode(processNameCString(opNameStr));
        ctxt.PutAttribute(L"ReturnValue", code);
      }   
      else if(actionStr.IsEqualNoCase("setKLOperatorFile"))
      {
        // parse args
        CString opNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "opName").c_str();
        CString fileNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "fileName").c_str();
        CString entryStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "entry", "", true).c_str();

        status = interf->setKLOperatorFile(processNameCString(opNameStr), fileNameStr, entryStr);
        if(!status.Succeeded())
          return status;
        xsiUpdateOp(interf->getObjectID());
      }   
      else if(actionStr.IsEqualNoCase("setKLOperatorEntry"))
      {
        // parse args
        CString opNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "opName").c_str();
        CString entryStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "entry").c_str();

        status = interf->setKLOperatorEntry(processNameCString(opNameStr), entryStr);
        if(!status.Succeeded())
          return status;
        xsiUpdateOp(interf->getObjectID());
      }   
      else if(actionStr.IsEqualNoCase("setKLOperatorIndex"))
      {
        // parse args
        CString opNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "opName").c_str();
        int opIndex = FabricSplice::Scripting::consumeIntegerArgument(scriptArgs, "index");

        status = interf->setKLOperatorIndex(processNameCString(opNameStr), opIndex);
        if(!status.Succeeded())
          return status;
        xsiUpdateOp(interf->getObjectID());
      }   
      else if(actionStr.IsEqualNoCase("removeKLOperator"))
      {
        // parse args
        CString opNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "opName").c_str();
        CString dgNodeStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "dgNode", "DGNode", true).c_str();

        status = interf->removeKLOperator(opNameStr, dgNodeStr);
        if(!status.Succeeded())
          return status;
        xsiUpdateOp(interf->getObjectID());
      }   
      else if(actionStr.IsEqualNoCase("getPortInfo"))
      {
        ctxt.PutAttribute(L"ReturnValue", interf->getDGPortInfo());
      }   
      else if(actionStr.IsEqualNoCase("setPortPersistence"))
      {
        // parse args
        CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();
        bool persistence = FabricSplice::Scripting::consumeBooleanArgument(scriptArgs, "persistence");

        interf->getSpliceGraph().setMemberPersistence(processNameCString(portNameStr).GetAsciiString(), persistence);
      }   
      else if(actionStr.IsEqualNoCase("getPortData"))
      {
        // parse args
        CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();

        FabricSplice::DGPort port = interf->getSpliceGraph().getDGPort(processNameCString(portNameStr).GetAsciiString());
        if(!port.isValid())
        {
          xsiLogErrorFunc("port '"+portNameStr+"' does not exist.");
          return CStatus::InvalidArgument;
        }
        if(port.getSliceCount() != 1)
        {
          xsiLogErrorFunc("port '"+portNameStr+"' has multiple slices.");
          return CStatus::InvalidArgument;
        }
        FabricCore::Variant portDataVar = port.getVariant();
        CString portData = portDataVar.getJSONEncoding().getStringData();
        ctxt.PutAttribute(L"ReturnValue", portData);
      }   
      else if(actionStr.IsEqualNoCase("setPortData"))
      {
        // parse args
        CString portNameStr = FabricSplice::Scripting::consumeStringArgument(scriptArgs, "portName").c_str();

        FabricSplice::DGPort port = interf->getSpliceGraph().getDGPort(processNameCString(portNameStr).GetAsciiString());
        if(!port.isValid())
        {
          xsiLogErrorFunc("port '"+portNameStr+"' does not exist.");
          return CStatus::InvalidArgument;
        }
        if(port.getSliceCount() != 1)
        {
          xsiLogErrorFunc("port '"+portNameStr+"' has multiple slices.");
          return CStatus::InvalidArgument;
        }
        if(port.getMode() == FabricSplice::Port_Mode_OUT)
        {
          xsiLogErrorFunc("port '"+portNameStr+"' is an output port.");
          return CStatus::InvalidArgument;
        }

        CString portData = auxiliaryStr;
        FabricCore::Variant portDataVar = FabricCore::Variant::CreateFromJSON(portData.GetAsciiString());
        port.setVariant(portDataVar);
        interf->getSpliceGraph().setMemberPersistence(processNameCString(portNameStr).GetAsciiString(), true);
      }   
      else
      {
        xsiLogErrorFunc("action argument uses invalid value '"+actionStr+"'");
        return CStatus::InvalidArgument;
      }
    }
  }
  catch(FabricSplice::Exception e)
  {
    return CStatus::Unexpected;
  }

  return xsiErrorOccured();
}

