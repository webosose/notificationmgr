// Copyright (c) 2013-2020 LG Electronics, Inc.
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

#ifndef _LSUTILS_H_
#define _LSUTILS_H_

#include <luna-service2/lunaservice.h>
#include <string>

class LSErrorSafe:
        public LSError
{
public:
    LSErrorSafe() {
        LSErrorInit(this);
    }
    ~LSErrorSafe() {
        LSErrorFree(this);
    }
};

class LSMessageWrapper
{
public:
    LSMessageWrapper(LSMessage *msg)
        : m_message(msg)
    {
        if (m_message) LSMessageRef(m_message);
    }

    ~LSMessageWrapper()
    {
        if (m_message) LSMessageUnref(m_message);
    }

    LSMessageWrapper(const LSMessageWrapper &r)
    {
        m_message = r.m_message;
        if (m_message) LSMessageRef(m_message);
    }

    LSMessageWrapper& operator=(const LSMessageWrapper &r)
    {
        if (m_message == r.m_message)
            return *this;

        if (m_message) LSMessageUnref(m_message);
        m_message = r.m_message;
        if (m_message) LSMessageRef(m_message);
        return *this;
    }

    operator LSMessage*()
    {
        return m_message;
    }

private:
    LSMessage *m_message;
};

class LSUtils
{
public:
    //! Parse caller from msg
    static std::string getCallerId(LSMessage *msg);

    //! Get method including category
    static std::string getMethod(LSMessage *msg, bool includeCategory = true);
};

#endif
