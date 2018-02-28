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

#ifndef __UTILS_h__
#define __UTILS_h__

#include <sstream>
#include <iostream>
#include <stdio.h>
#include <sys/time.h>
#include <string>
#include <glib.h>

namespace Utils
{
    bool verifyFileExist(const char *pathAndFile);
    bool getLS2ServiceDomainPart(const std::string& url, std::string& domainPart);
    char* readFile(const char* filePath);
    std::string extractTimestampFromId(const std::string& id);
    void createTimestamp(std::string& timestamp);
    bool isValidURI(const std::string& uri);
    bool isEscapeChar(char c);
    std::string extractSourceIdFromCaller(const std::string& id);

    //! Make std::string for type T
    template <class T>
    static std::string toString(const T &arg)
    {
        std::ostringstream out;
        out << arg;
        return (out.str());
    }

    namespace Private
    {
        // abstract class for async call
        class IAsyncCall
        {
        public:
            virtual ~IAsyncCall() { }
            virtual void Call() = 0;
        };

        // implementaion for async call
        template <typename T>
        class AsyncCall : public IAsyncCall
        {
        public:
            AsyncCall(T _func) : func(_func) {}

            void Call() { func(); }
        private:
            T func;
        };

        //! It's called when get response async call
        gboolean cbAsync(gpointer data);
    }

    //! Call function asynchronously
    template <typename T>
    bool async(T function)
    {
        Private::AsyncCall<T> *p = new Private::AsyncCall<T>(function);
        g_timeout_add(0, Private::cbAsync, (gpointer)p);
        return true;
    }
}
#endif
