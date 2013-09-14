//
//  ndnrtc-object.cpp
//  ndnrtc
//
//  Copyright 2013 Regents of the University of California
//  For licensing details see the LICENSE file.
//
//  Author:  Peter Gusev 
//  Created: 8/21/13
//

#include "ndnrtc-object.h"

using namespace ndnrtc;

//********************************************************************************
/**
 * @name Paramter class
 */
//********************************************************************************
#pragma mark - construction/destruction
NdnParams::Parameter::Parameter(const ParameterType type, const void *value): type_(type)
{
    value_ = NdnParams::Parameter::copiedValue(type_, value);
}
#pragma mark - public
void NdnParams::Parameter::setTypeAndValue(const ParameterType type, const void *value)
{
    type_ = type;
    
    if (value_ != nullptr)
        free(value_);
    
    value_ = NdnParams::Parameter::copiedValue(type_, value);
}
//********************************************************************************
#pragma mark - private
void* NdnParams::Parameter::copiedValue(const ParameterType type, const void *value)
{
    int valueSize = NdnParams::Parameter::valueByteSize(type, value);
    void *newValue = nullptr;
    
    if (valueSize)
    {
        newValue = malloc(valueSize);   
        memset(newValue, 0, valueSize);
        memcpy(newValue, value, valueSize);
    }
    
    return newValue;
}
int NdnParams::Parameter::valueByteSize(const ParameterType type, const void *value)
{
    int valueSize = 0;
    
    switch (type) {
        case ParameterTypeBool:
            valueSize = sizeof(bool);
            break;
        case ParameterTypeInt:
            valueSize = sizeof(int);
            break;
        case ParameterTypeString:
            valueSize = strlen((const char*)value)+1;
            break;
        case ParameterTypeUndefined: // fall through
        default:
            valueSize = 0;
            break;
    }
    return valueSize;
}
//********************************************************************************
/**
 * @name NdnParams class
 */
//********************************************************************************
#pragma mark - public
void NdnParams::addParams(const ndnrtc::NdnParams& params)
{
    const std::map<std::string, Parameter> &map = params.map();
    std::map<std::string, Parameter>::const_iterator it;
    
    for (it = map.begin(); it != map.end(); ++it)
    {
        std::string name = it->first;
        const Parameter *p = &it->second;
        
        setParam(name, Parameter(p->getType(), p->getValue()));
    }
}
void NdnParams::resetParams(const NdnParams &params)
{
    propertiesMap_.clear();
    addParams(params);
}
//********************************************************************************
#pragma mark - protected
void NdnParams::setParam(const std::string &name, const ndnrtc::NdnParams::Parameter &param)
{
    Parameter *newP = nullptr;
    
    newP = &propertiesMap_[name]; // returns existing or new
    newP->setTypeAndValue(param.getType(), param.getValue());
}
int NdnParams::getParamAsInt(const std::string &paramName, int *param) const
{
    Parameter *p = getParam(paramName);
    
    if (p && p->getType() == NdnParams::ParameterTypeInt)
    {
        *param = *(int*)p->getValue();
        return 0;
    }
    
    return -1;
}
int NdnParams::getParamAsBool(const std::string &paramName, bool *param) const
{
    Parameter *p = getParam(paramName);
    
    if (p && p->getType() == NdnParams::ParameterTypeBool)
    {
        *param = *(bool*)p->getValue();
        return 0;
    }
    
    return -1;
}
int NdnParams::getParamAsString(const std::string &paramName, char **param) const
{
    Parameter *p = getParam(paramName);
    
    if (p && p->getType() == NdnParams::ParameterTypeString)
    {
        *param = (char*)NdnParams::Parameter::copiedValue(p->getType(), p->getValue());
        return 0;
    }
    
    return -1;    
}
//********************************************************************************
#pragma mark - private
NdnParams::Parameter* NdnParams::getParam(const std::string &name) const
{
    std::map<std::string, NdnParams::Parameter>::const_iterator it;
    
    it = propertiesMap_.find(name);
    
    if (it != propertiesMap_.end())
        return (NdnParams::Parameter*)&it->second;
    
    return nullptr;
}
//********************************************************************************
/**
 * @name NdnRtcObject class
 */
#pragma mark - construction/destruction
NdnRtcObject::NdnRtcObject() : observer_(nullptr), params_(nullptr)
{
}
NdnRtcObject::NdnRtcObject(const NdnParams *params) : observer_(nullptr)
{
    params_ = new NdnParams(*params);
}
NdnRtcObject::NdnRtcObject(const NdnParams *params, INdnRtcObjectObserver *observer) : NdnRtcObject(params)
{
    observer_ = observer;
}
NdnRtcObject::~NdnRtcObject()
{
    delete params_;
}

//********************************************************************************
#pragma mark - public

//********************************************************************************
#pragma mark - intefaces realization - INdnRtcObjectObserver
void NdnRtcObject::onErrorOccurred(const char *errorMessage)
{
    if (hasObserver())
        observer_->onErrorOccurred(errorMessage);
    else
        ERR("error occurred: %s", errorMessage);
}

//********************************************************************************
#pragma mark - protected
int NdnRtcObject::notifyError(const int ecode, const char *format, ...)
{
    va_list args;
    
    static char emsg[256];
    
    va_start(args, format);
    vsprintf(emsg, format, args);
    va_end(args);
    
    if (hasObserver())
    {
        TRACE("error occurred: %s",emsg);
        observer_->onErrorOccurred(emsg);
    }
    else
        ERR("%s", emsg);
    
    return ecode;
}

int NdnRtcObject::notifyErrorNoParams()
{
    return notifyError(-1, "no parameters provided");
}

int NdnRtcObject::notifyErrorBadArg(const std::string &paramName)
{
    return notifyError(-1, "bad or non-existent argument: %s", paramName.c_str());
}