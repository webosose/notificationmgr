// Copyright (c) 2013-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "JUtil.h"
#include "Utils.h"

JUtil::Error::Error()
    : m_code(Error::None)
{
}

JUtil::Error::ErrorCode JUtil::Error::code()
{
    return m_code;
}

std::string JUtil::Error::detail()
{
    return m_detail;
}

void JUtil::Error::set(ErrorCode code, const char *detail)
{
    m_code = code;
    if (!detail)
    {
        switch(m_code)
        {
            case Error::None:    m_detail = "Success"; break;
            case Error::File_Io: m_detail = "Fail to read file"; break;
            case Error::Schema:  m_detail = "Fail to read schema"; break;
            case Error::Parse:   m_detail = "Fail to parse json"; break;
            default:             m_detail = "Unknown error"; break;
        }
    }
    else
        m_detail = detail;
}

JUtil::JUtil()
{
}

JUtil::~JUtil()
{
}

pbnjson::JValue JUtil::parse(const char *rawData, const std::string &schemaName, Error *error)
{
    pbnjson::JSchema schema = JUtil::instance().loadSchema(schemaName, true);
    if (!schema.isInitialized())
    {
        if (error) error->set(Error::Schema);
        return pbnjson::JValue();
    }

    pbnjson::JInput input(rawData);
    pbnjson::JDomParser parser;
    if (!parser.parse(rawData, schema))
    {
        if (error) error->set(Error::Parse, parser.getError());
        return pbnjson::JValue();
    }

    if (error) error->set(Error::None);
    return parser.getDom();
}

pbnjson::JValue JUtil::parseFile(const std::string &path, const std::string &schemaName, Error *error)
{
    char *rawData = Utils::readFile(path.c_str());
    if (!rawData)
    {
        if (error) error->set(Error::File_Io);
        return pbnjson::JValue();
    }

    pbnjson::JValue parsed = parse(rawData, schemaName, error);

    delete [] rawData;
    return parsed;
}

pbnjson::JSchema JUtil::loadSchema(const std::string& schemaName, bool cache)
{
    if (schemaName.empty())
        return pbnjson::JSchemaFragment("{}");

    if (cache)
    {
        std::map< std::string, pbnjson::JSchema >::iterator it = m_mapSchema.find(schemaName);
        if (it != m_mapSchema.end())
            return it->second;
    }

    pbnjson::JSchema schema = pbnjson::JSchemaFile(kSchemaPath + schemaName + ".schema");
    if (!schema.isInitialized())
        return schema;

    if (cache)
    {
        m_mapSchema.insert( std::pair< std::string, pbnjson::JSchema >(schemaName, schema) );
    }

    return schema;
}

std::string JUtil::jsonToString(pbnjson::JValue json)
{
    return pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
}
