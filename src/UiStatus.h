// Copyright (c) 2015-2018 LG Electronics, Inc.
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

#ifndef __UI_STATUS_H__
#define __UI_STATUS_H__

#include <map>
#include <algorithm>
#include <boost/signals2.hpp>

#include "Singleton.hpp"

class UiStatus : public Singleton<UiStatus>
{
public:
    static const int ENABLE_SYSTEM    = 0x01;
    static const int ENABLE_EXTERNAL  = 0x02;
    static const int ENABLE_UI        = 0x04;
    static const int ENABLE_ALL       = ENABLE_SYSTEM | ENABLE_EXTERNAL | ENABLE_UI;

private:
    class UiComp
    {
    public:
        UiComp();

        void enable(int mask, const char *reason = NULL);
        void disable(int mask, const char *reason = NULL);

        bool isEnabled(int mask = ENABLE_ALL) const;
        std::string reason() const;

        virtual std::string name() { return std::string("uicomp"); }

        boost::signals2::signal<void (bool)> sigStatus;

    private:
        int m_enabled;
        std::string m_reason;
    };

    class UiToast : public UiComp
    {
    public:
        UiToast();

        virtual std::string name() { return std::string("toast"); }

        void setSilence(bool silence);
        bool isSilence() const;

    private:
        bool m_silence;
    };

    class UiAlert : public UiComp
    {
    public:
        virtual std::string name() { return std::string("alert"); }
    };

    class UiInput : public UiComp
    {
    public:
        virtual std::string name() { return std::string("input"); }
    };

    class UiPrompt : public UiComp
    {
    public:
        virtual std::string name() { return std::string("prompt"); }
    };

public:

    UiStatus();

    void enable(int mask, const char *reason = NULL);
    void disable(int mask, const char *reason = NULL);

    bool isEnabled(int mask = ENABLE_ALL) const;

    UiToast& toast() { return *(std::dynamic_pointer_cast<UiToast>(m_comps["toast"])); }
    UiAlert& alert() { return *(std::dynamic_pointer_cast<UiAlert>(m_comps["alert"])); }
    UiInput& input() { return *(std::dynamic_pointer_cast<UiInput>(m_comps["input"])); }
    UiPrompt& prompt() { return *(std::dynamic_pointer_cast<UiPrompt>(m_comps["prompt"])); }

private:
    std::map<std::string, std::shared_ptr<UiComp> > m_comps;
};

#endif
